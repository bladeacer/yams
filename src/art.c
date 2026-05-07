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
    url_decode(decoded_path, (strncmp(input_url, "file://", 7) == 0) ? input_url + 7 : input_url);
    char *safe_path = sh_escape(decoded_path);
    const char* final_path = (strncmp(input_url, "http", 4) == 0) ? input_url : safe_path;

    const char *tmp_raw = "/tmp/yams_raw.png";
    const char *tmp_proc = "/tmp/yams_proc.webp";

    char cmd[8192];
    int res;

    snprintf(cmd, sizeof(cmd), 
             "ffmpeg -hide_banner -loglevel error -y -i '%s' "
             "-map 0:v:0 -map_metadata -1 -frames:v 1 '%s' > /dev/null 2>&1", 
             final_path, tmp_raw);
    
    res = system(cmd);
    if (res != 0) {
        DBG("Art Error: ffmpeg failed to extract art from %s", final_path);
        return;
    }

    snprintf(cmd, sizeof(cmd),
            "magick \"%s\" -fuzz 10%% -trim +repage "
            "-write mpr:img -delete 0 "
            "mpr:img -set option:avg '%%[pixel:p{0,0}]' "
            "-background '%%[avg]' -thumbnail %dx%d! -gravity center -extent %dx%d \"%s\" > /dev/null 2>&1",
            tmp_raw, (int)(w * 1.5), h, (int)(w * 1.5), h, tmp_proc);

    res = system(cmd);
    if (res != 0) {
        DBG("Art Error: Magick failed to process art");
        return;
    }

    snprintf(cmd, sizeof(cmd), 
             "jp2a -b --width=%d --height=%d --colors \"%s\" | sed 's/^/\\x1b[%dG/'", 
             w, h, tmp_proc, x + 1);

    def_prog_mode(); 
    printf("\x1b[%d;%dH", y + 1, x + 1);
    fflush(stdout);

    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[8192];
        while (fgets(line, sizeof(line), fp)) {
            fputs(line, stdout);
        }
        pclose(fp);
    }

    printf("\x1b[0m");
    fflush(stdout);
    reset_prog_mode();

    remove(tmp_raw);
    remove(tmp_proc);
}
