#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <ncurses.h>
#include "art.h"
#include "metadata.h"
#include "utils.h"
#include "debug.h"

void trim_trailing_ws(char *s) {
    char *end = s + strlen(s) - 1;
    while (end >= s && (isspace((unsigned char)*end))) {
        *end = '\0';
        end--;
    }
}

void render_art(int x, int y, int w, int h) {
    char *input_url = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    if (!input_url || strlen(input_url) < 5) return;

    char decoded_path[4096];
    url_decode(decoded_path, (strncmp(input_url, "file://", 7) == 0) ? input_url + 7 : input_url);
    char *safe_path = sh_escape(decoded_path);
    const char* final_path = (strncmp(input_url, "http", 4) == 0) ? input_url : safe_path;

    const char *tmp_raw = "/dev/shm/yams_raw.png";
    char cmd[16384];

    // 1. Extraction remains the same
    snprintf(cmd, sizeof(cmd), 
             "ffmpeg -hide_banner -loglevel error -y -i '%s' -map 0:v:0 -map_metadata -1 -frames:v 1 '%s'", 
             final_path, tmp_raw);
    if (system(cmd) != 0) return;

    if (get_preferred_renderer() == 1) {
        snprintf(cmd, sizeof(cmd),
                "magick \"%s\" -fuzz 10%% -trim +repage "
                "-thumbnail %dx%d! -gravity center -extent %dx%d jpg:- | "
                "jp2a -b --width=%d --height=%d --colors '%s'",
                tmp_raw, (int)(w * 1.5), h, (int)(w * 1.5), h, jp2a_chars);
    } else {
        const char *jp2a_chars = " '.,-~:;=!*#$@"; 
        
        snprintf(cmd, sizeof(cmd),
                "magick \"%s\" -fuzz 10%% -trim +repage "
                "-thumbnail %dx%d! -gravity center -extent %dx%d jpg:- | "
                "chafa --format symbols --symbols \"%s\" --fg-only --fill none --bg black --probe off "
                "-c 256 --optimize 0 --dither none --stretch --size %dx%d -",
                tmp_raw, (int)(w * 1.5), h, (int)(w * 1.5), h, jp2a_chars, w, h);
    }

    def_prog_mode(); 
    printf("\x1b[s"); 
    fflush(stdout);

    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[16384]; 
        int line_offset = 0;
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\r\n")] = 0;
            trim_trailing_ws(line);
            printf("\x1b[%d;%dH%s", y + 1 + line_offset, x + 1, line);
            line_offset++;
            if (line_offset >= h) break;
        }
        pclose(fp);
    }
    
    printf("\x1b[u\x1b[0m"); 
    fflush(stdout);
    reset_prog_mode();
    remove(tmp_raw);
}
