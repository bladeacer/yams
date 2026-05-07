#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <ncurses.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "art.h"
#include "metadata.h"
#include "utils.h"
#include "debug.h"

#define ART_DEBOUNCE_MS 200

void trim_trailing_ws(char *s) {
    char *end = s + strlen(s) - 1;
    while (end >= s && (isspace((unsigned char)*end))) {
        *end = '\0';
        end--;
    }
}

pid_t render_art(int x, int y, int w, int h) {
    char *input_url = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    if (!input_url || strlen(input_url) < 5) return -1;

    char decoded_path[4096];
    url_decode(decoded_path, (strncmp(input_url, "file://", 7) == 0) ? input_url + 7 : input_url);
    char *safe_path = sh_escape(decoded_path);
    const char* final_path = (strncmp(input_url, "http", 4) == 0) ? input_url : safe_path;

    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        char cmd[16384];
        char tmp_raw[256];
        snprintf(tmp_raw, sizeof(tmp_raw), "/dev/shm/yams_art_%d.png", getpid());
        const char *jp2a_chars = "    ...',;:clodxkO0KXNWM";

        snprintf(cmd, sizeof(cmd), 
                 "ffmpeg -hide_banner -loglevel error -y -i '%s' -map 0:v:0 -frames:v 1 '%s' > /dev/null 2>&1", 
                 final_path, tmp_raw);
        
        if (system(cmd) == 0) {
            char draw_cmd[16384];
            if (get_preferred_renderer() == 1) {
                snprintf(draw_cmd, sizeof(draw_cmd),
                        "magick \"%s\" -fuzz 10%% -trim +repage -thumbnail %dx%d! -gravity center -extent %dx%d jpg:- 2>/dev/null | "
                        "jp2a --chars=\"%s\" --width=%d --height=%d --colors - 2>/dev/null",
                        tmp_raw, (int)(w * 1.5), h, (int)(w * 1.5), h, jp2a_chars, w, h);
            } else {
                snprintf(draw_cmd, sizeof(draw_cmd),
                        "magick \"%s\" -fuzz 10%% -trim +repage -thumbnail %dx%d! -gravity center -extent %dx%d jpg:- 2>/dev/null | "
                        "chafa --format symbols --symbols \"[%s]\" --fg-only --fill none --bg black --probe off "
                        "-c 256 --optimize 0 --dither ordered --dither-intensity 0.3 --stretch --size %dx%d - 2>/dev/null",
                        tmp_raw, (int)(w * 1.5), h, (int)(w * 1.5), h, jp2a_chars, w, h);
            }

            printf("\x1b[s");
            FILE *fp = popen(draw_cmd, "r");
            if (fp) {
                char line[16384];
                int line_offset = 0;
                while (fgets(line, sizeof(line), fp)) {
                    if (line_offset < h) {
                        printf("\x1b[%d;%dH%s", y + 1 + line_offset, x + 1, line);
                        line_offset++;
                    }
                }
                pclose(fp);
            }
            printf("\x1b[u\x1b[0m");
            fflush(stdout);
        }

        remove(tmp_raw);
        _exit(0); 
    }

    return pid;
}
