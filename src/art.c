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

static char cmd_buf[8192];

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
    char *input_url = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    if (!input_url || strlen(input_url) < 5) return -1;

    char decoded[4096];
    url_decode(decoded, (strncmp(input_url, "file://", 7) == 0) ? input_url + 7 : input_url);
    char *safe_path = sh_escape(decoded); 
    DBG("Render art called for: %s", safe_path);

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        const char *jp2a_chars = "    ...',;:clodxkO0KXNWM";
        int scaled_w = (int)(w * 1.5);
        char img_src[4096];
        int use_ff = needs_ffmpeg(decoded, input_url);

        if (use_ff) {
            snprintf(img_src, sizeof(img_src), "/dev/shm/yams_frame_%d.png", getpid());
            char ff_cmd[8192];
            snprintf(ff_cmd, sizeof(ff_cmd), 
                     "ffmpeg -v quiet -y -i '%s' -map 0:v:0 -frames:v 1 \"%s\" > /dev/null 2>&1",
                     (strncmp(input_url, "http", 4) == 0) ? input_url : safe_path, img_src);
            if (system(ff_cmd) != 0) _exit(1);
        } else {
            snprintf(img_src, sizeof(img_src), "%s", decoded);
        }

        if (get_preferred_renderer() == 1) { // jp2a
            // jp2a doesn't handle transparency well; convert to jpg stream
            snprintf(cmd_buf, sizeof(cmd_buf),
                     "magick \"%s\" -fuzz 10%% -trim +repage -background black -alpha remove jpg:- 2>/dev/null | "
                     "jp2a --chars=\"%s\" --width=%d --height=%d --colors -",
                     img_src, jp2a_chars, w, h);
        } else { // chafa
            // Keep PNG stream to preserve transparency for Chafa
            // We use scaled_w in the --size flag to preserve your 1.5x width preference
            snprintf(cmd_buf, sizeof(cmd_buf),
                     "magick \"%s\" -fuzz 10%% -trim +repage png:- 2>/dev/null | "
                     "chafa --format symbols --symbols \"[%s]\" --fg-only --fill none --bg black "
                     "--stretch --size %dx%d -",
                     img_src, jp2a_chars, scaled_w, h);
        }

        printf("\x1b[s");
        FILE *fp = popen(cmd_buf, "r");
        if (fp) {
            char line[16384];
            int row = 0;
            while (fgets(line, sizeof(line), fp) && row < h) {
                trim_trailing_ws(line);
                printf("\x1b[%d;%dH%s", y + 1 + row, x + 1, line);
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
