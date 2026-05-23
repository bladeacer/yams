<!-- 1. debug build flag -->
<!--  DONE: NDEBUG toggle via meson_options.txt 'logging' option.  -->

<!-- 2. proper tests instead of fucked up shell scripts around other CLI tools -->
<!--  DONE: Tests using cmocka (test_metadata, test_config, test_utils).  -->

<!-- 3. wrap nix flakes around all the tools needed (playerctl, hyperfine, whatever) -->
<!--  DONE: flake.nix provides meson, cmake, ncurses, ninja, playerctl, ffmpeg, hyperfine, chafa.  -->

<!-- 4. separate .c and .h files into src/*.c and lib/*.h -->
<!--  DONE: Headers in include/, sources in src/, library code in lib/.  -->

5. ~~make use of chafa's internal API for the art rendering. Note the flags that should be passed into the code like in the old code snippet. chafa API usage url: https://hpjansson.org/chafa/ref/chafa-using.html~~
   DONE: art.c uses Chafa C API for BOTH braille and ASCII modes.
   ASCII mode: pre-resizes source image to 1.5× terminal width via stb_image_resize
     before feeding pixels to Chafa, compensating for terminal cell aspect ratio.
   Braille mode: feeds original pixels directly.
   Luminance sampling, dithering, and color quantization left entirely to Chafa.

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <ncurses.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include "metadata.h"
#include "art.h"
#include "utils.h"
#include "debug.h"

void trim_trailing_ws(char *s) {
    if (!s) return;
    char *end = s + strlen(s) - 1;
    while (end >= s && (isspace((unsigned char)*end))) {
        *end = '\0';
        end--;
    }
}

static int needs_ffmpeg(const char *path, const char *orig_url) {
    if (!path || !orig_url) return 0;
    
    if (strncmp(orig_url, "http", 4) == 0) return 1;

    const char *ext = strrchr(path, '.');
    if (!ext) return 0;

    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 || 
        strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".webp") == 0) {
        return 0;
    }

    return 1;
}

pid_t render_art(int x, int y, int w, int h) {
    char *input_url = (strlen(GET_STR(m.art_url_off)) > 5) ? GET_STR(m.art_url_off) : GET_STR(m.file_path_off);
    if (!input_url || strlen(input_url) < 5) return -1;

    char decoded[4096];
    url_decode(decoded, (strncmp(input_url, "file://", 7) == 0) ? input_url + 7 : input_url);
    char *safe_path = sh_escape(decoded); 

    DBG("--- Art Render Request ---");

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // Reset signals in the child to default
        signal(SIGCHLD, SIG_DFL);
        setpgid(0, 0);

        int use_ff = needs_ffmpeg(decoded, input_url);
        char img_src[1024] = {0};
        char magick_input[4200] = {0};
        
        if (use_ff) {
            snprintf(img_src, sizeof(img_src), "/dev/shm/yams_%d.png", getpid());
            char ff_cmd[10240]; 
            
            /* CRITICAL CHANGES:
               1. -nostdin: Prevents ffmpeg from hanging while waiting for terminal input.
               2. timeout 5: Kills the command if it takes longer than 5 seconds.
            */
            snprintf(ff_cmd, sizeof(ff_cmd), 
                     "timeout 5 ffmpeg -v quiet -y -nostdin -an -i \"%s\" -frames:v 1 \"%s\" > /dev/null 2>&1",
                     (strncmp(input_url, "http", 4) == 0) ? input_url : decoded, img_src);
            
            DBG("[Child] Running FFmpeg...");
            /* int ret = system(ff_cmd); */
            /* DBG("[Child] FFmpeg returned: %d", ret); */

            if (access(img_src, F_OK) != 0) {
                DBG("[Child] Primary failed, trying copy fallback...");
                snprintf(ff_cmd, sizeof(ff_cmd), 
                         "timeout 5 ffmpeg -v quiet -y -nostdin -i \"%s\" -an -vcodec copy \"%s\" > /dev/null 2>&1",
                         decoded, img_src);
                system(ff_cmd);
            }

            if (access(img_src, F_OK) == 0) {
                snprintf(magick_input, sizeof(magick_input), "%s", img_src);
            } else {
                DBG("[Child] Extraction failed. Exiting.");
                _exit(1);
            }
        } else {
            snprintf(magick_input, sizeof(magick_input), "'%s'", safe_path);
        }

        char large_cmd[12288];
        const char *jp2a_chars = "    ...',;:clodxkO0KXNWM";
        int scaled_w = (int)(w * 1.5);

        if (get_preferred_renderer() == 1) {
            snprintf(large_cmd, sizeof(large_cmd),
                     "magick %s -fuzz 10%% -trim +repage -thumbnail %dx%d! "
                     "-gravity center -background black -extent %dx%d -alpha remove jpg:- 2>/dev/null | "
                     "jp2a --chars=\"%s\" --width=%d --height=%d --colors - 2>/dev/null",
                     magick_input, scaled_w, h, scaled_w, h, jp2a_chars, w, h);
        } else {
            snprintf(large_cmd, sizeof(large_cmd),
                     "magick %s -fuzz 10%% -trim +repage -thumbnail %dx%d! "
                     "-gravity center -background black -extent %dx%d png:- 2>/dev/null | "
                     "chafa --format symbols --symbols \"[%s]\" --fg-only --fill none --bg black "
                     "--probe off -c 256 --optimize 0 --dither ordered --dither-intensity 0.3 "
                     "--stretch --size %dx%d - 2>/dev/null",
                     magick_input, scaled_w, h, scaled_w, h, jp2a_chars, w, h);
        }

        DBG("[Child] Pipe Start: %s", large_cmd);
        printf("\x1b[s");
        FILE *fp = popen(large_cmd, "r");
        if (fp) {
            char line[16384];
            int row = 0;
            while (fgets(line, sizeof(line), fp) && row < h) {
                trim_trailing_ws(line);
                printf("\x1b[%d;%dH%s", y + 1 + row, x + 2, line);
                row++;
            }
            pclose(fp);
        }


        printf("\x1b[u\x1b[0m");
        fflush(stdout);

        if (use_ff) unlink(img_src);
        _exit(0);
    }
    return pid;
}

```

6. hopefully move off CLI calls, just API to mpris which is p doable and can be its own small C library too
