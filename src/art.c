#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <ncurses.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include "art.h"
#include "metadata.h"
#include "utils.h"
#include "debug.h"

#define ART_DEBOUNCE_MS 200

static char cmd_buf[8192];
static char path_buf[2048];

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

    if (strncmp(input_url, "file://", 7) == 0) {
        url_decode(path_buf, input_url + 7);
    } else {
        strncpy(path_buf, input_url, sizeof(path_buf) - 1);
    }

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        char tmp_raw[128];
        snprintf(tmp_raw, sizeof(tmp_raw), "/dev/shm/yams_%d.jpg", getpid());
        const char *jp2a_chars = "   ...',;:clodxkO0KXNWM";

        // Step 1: Extract frame
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "ffmpeg -v quiet -y -i \"%s\" -map 0:v:0 -frames:v 1 -f mjpeg \"%s\"",
                 path_buf, tmp_raw);

        if (system(cmd_buf) != 0) _exit(1);

        int scaled_w = (int)(w * 1.5);

        // Step 2: Build the draw command
        if (get_preferred_renderer() == 1) {
            // JP2A: needs scaled_w for magick (thumbnail/extent) and w/h for jp2a
            snprintf(cmd_buf, sizeof(cmd_buf),
                     "magick \"%s\" -fuzz 10%% -trim +repage -thumbnail %dx%d! "
                     "-gravity center -extent %dx%d jpg:- 2>/dev/null | "
                     "jp2a --chars=\"%s\" --width=%d --height=%d --colors - 2>/dev/null",
                     tmp_raw, scaled_w, h, scaled_w, h, jp2a_chars, w, h);
        } else {
            // Chafa: needs scaled_w for magick (thumbnail/extent) and w/h for chafa size
            snprintf(cmd_buf, sizeof(cmd_buf),
                     "magick \"%s\" -fuzz 10%% -trim +repage -thumbnail %dx%d! "
                     "-gravity center -extent %dx%d jpg:- 2>/dev/null | "
                     "chafa --format symbols --symbols vhalf --fg-only --fill none "
                     "--bg black --probe off -c 256 --stretch --size %dx%d - 2>/dev/null",
                     tmp_raw, scaled_w, h, scaled_w, h, w, h);
        }

        // Step 3: Draw
        printf("\x1b[s"); 
        FILE *fp = popen(cmd_buf, "r");
        if (fp) {
            char line[4096];
            int row = 0;
            while (fgets(line, sizeof(line), fp) && row < h) {
                printf("\x1b[%d;%dH%s", y + 1 + row, x + 1, line);
                row++;
            }
            pclose(fp);
        }

        printf("\x1b[u\x1b[0m");
        fflush(stdout);

        unlink(tmp_raw);
        _exit(0);
    }

    return pid;
}
