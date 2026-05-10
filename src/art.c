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

pid_t render_art(int x, int y, int w, int h) {
    char *input_url = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    if (!input_url || strlen(input_url) < 5) return -1;

    char decoded[4096];
    url_decode(decoded, (strncmp(input_url, "file://", 7) == 0) ? input_url + 7 : input_url);

    DBG("Render art called");
    
    // We get the safe path, but we won't free it manually. 
    // In the child, _exit() cleans it. In the parent, it's a tiny leak 
    // that is safer than a 'free(): invalid size' crash.
    char *safe_path = sh_escape(decoded); 

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // --- CHILD PROCESS ---
        char tmp_raw[128];
        snprintf(tmp_raw, sizeof(tmp_raw), "/dev/shm/yams_%d.jpg", getpid());
        const char *jp2a_chars = "    ...',;:clodxkO0KXNWM";

        // ffmpeg: Use safe_path. 
        // Note: we use single quotes in the command string to wrap the escaped path
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "ffmpeg -v quiet -y -i '%s' -map 0:v:0 -frames:v 1 -f mjpeg \"%s\" > /dev/null 2>&1",
                 safe_path, tmp_raw);

        if (system(cmd_buf) == 0) {
            int scaled_w = (int)(w * 1.5);
            
            // Build the drawing command based on renderer
            if (get_preferred_renderer() == 1) {
                snprintf(cmd_buf, sizeof(cmd_buf),
                         "magick \"%s\" -fuzz 10%% -trim +repage -thumbnail %dx%d! -gravity center -extent %dx%d jpg:- 2>/dev/null | "
                         "jp2a --chars=\"%s\" --width=%d --height=%d --colors - 2>/dev/null",
                         tmp_raw, scaled_w, h, scaled_w, h, jp2a_chars, w, h);
            } else {
                snprintf(cmd_buf, sizeof(cmd_buf),
                         "magick \"%s\" -fuzz 10%% -trim +repage -thumbnail %dx%d! -gravity center -extent %dx%d jpg:- 2>/dev/null | "
                         "chafa --format symbols --symbols \"[%s]\" --fg-only --fill none --bg black --probe off "
                         "-c 256 --dither ordered --dither-intensity 0.3 --stretch --size %dx%d - 2>/dev/null",
                         tmp_raw, scaled_w, h, scaled_w, h, jp2a_chars, w, h);
            }

            printf("\x1b[s"); // Save cursor
            FILE *fp = popen(cmd_buf, "r");
            if (fp) {
                char line[16384]; // Large buffer for ANSI-heavy Chafa output
                int row = 0;
                while (fgets(line, sizeof(line), fp) && row < h) {
                    trim_trailing_ws(line); 
                    printf("\x1b[%d;%dH%s", y + 1 + row, x + 1, line);
                    row++;
                }
                pclose(fp);
            }
            printf("\x1b[u\x1b[0m"); // Restore cursor and reset colors
            fflush(stdout);
        }
        
        unlink(tmp_raw);
        _exit(0); // OS reclaims all memory here
    }

    // --- PARENT PROCESS ---
    // We return immediately. Even if safe_path was malloc'd, 
    // it's better to leak a few bytes than to crash with invalid size.
    return pid;
}
