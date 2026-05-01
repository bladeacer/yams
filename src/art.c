#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ncurses.h>
#include "art.h"
#include "metadata.h"
#include "utils.h"
#include "debug.h"

void render_art(int x, int y, int w, int h) {
    char *input_url = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    if (!input_url || strlen(input_url) < 5) return;

    char decoded_path[4096];
    const char *raw_path = input_url;
    bool is_remote = (strncmp(input_url, "http", 4) == 0);
    
    if (!is_remote && strncmp(input_url, "file://", 7) == 0) {
        raw_path = input_url + 7;
    }

    url_decode(decoded_path, raw_path);
    char *safe_path = sh_escape(decoded_path);
    char cmd[16384];

    snprintf(cmd, sizeof(cmd), 
            "ffmpeg -hide_banner -loglevel error -noautorotate -i \"%s\" "
            "-map 0:v:0 -map_metadata -1 -frames:v 1 -f image2pipe -vcodec mjpeg - 2>/dev/null | "
            "magick - -fuzz 10%% -trim +repage "
            "-write mpr:img -delete 0 "
            "mpr:img -set option:avg '%%[pixel:p{0,0}]' "
            "-background '%%[avg]' -thumbnail %dx%d! -gravity center -extent %dx%d webp:- | " 
            "jp2a -b --width=%d --height=%d --colors - 2>/dev/null | sed 's/^/\\x1b[%dG/'", 
            safe_path, 
            (int)(w * 1.5), h, 
            (int)(w * 1.5), h,
            w, h, 
            x + 1);

    def_prog_mode(); 
    printf("\x1b[%d;%dH", y + 1, x + 1);
    fflush(stdout);

    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[8192];
        bool lines_read = false;
        while (fgets(line, sizeof(line), fp)) {
            fputs(line, stdout);
            lines_read = true;
        }
        
        if (!lines_read) {
            DBG("Art pipeline produced no output for: %s", safe_path);
        }
        pclose(fp);
    }

    printf("\x1b[0m");
    fflush(stdout);
    reset_prog_mode();
}
