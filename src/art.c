#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ncurses.h>
#include "art.h"
#include "metadata.h"
#include "utils.h"
#include "debug.h"

void render_art(int x, int y, int w, int h) {
    // Determine source: Prefer art_url, fallback to file_path
    char *input_url = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    if (!input_url || strlen(input_url) < 5) return;

    char decoded_path[4096];
    const char *raw_path = input_url;
    
    // Check if it's a local file vs remote
    bool is_remote = (strncmp(input_url, "http", 4) == 0);
    
    if (!is_remote && strncmp(input_url, "file://", 7) == 0) {
        raw_path = input_url + 7;
    }

    url_decode(decoded_path, raw_path);
    char *safe_path = sh_escape(decoded_path);
    DBG("Rendering Art from: %s", safe_path);
    char cmd[16384];

    /*
     * TODO: 
     * 1. await ffmpeg call, validate result in-memory
     * 2. image hash, if image same as previous one do not run the rest but use previous image.
     * 3. await imagemagick call with validation like ffmpeg
     * 4. await call jp2a
     * 5. at each step, if fail it skips since polling update means the UI will try again later.
    */

    /* * PIPELINE STRATEGY:
     * 1. FFMPEG: Extracts 1 frame. If it's a remote URL, it handles the network stream.
     * 2. IMAGEMAGICK: 
     * - Trims whitespace/borders.
     * - Samples top-left pixel for a "theme" background color.
     * - Resizes and extents to fit the UI aspect ratio.
     * 3. JP2A: Converts to high-fidelity 24-bit ASCII.
     */
    snprintf(cmd, sizeof(cmd), 
            "ffmpeg -hide_banner -loglevel error -i \"%s\" -vframes 1 -f mjpeg - 2>/dev/null | "
            "magick - -fuzz 15%% -trim +repage "
            "-write mpr:img -delete 0 "
            "mpr:img -set option:avg '%%[pixel:p{0,0}]' "
            "-background '%%[avg]' -thumbnail %dx%d! -gravity center -extent %dx%d webp:- | " 
            "jp2a -b --width=%d --height=%d --color-depth=24 - | sed 's/^/\\x1b[%dG/'", 
            safe_path, 
            (int)(w * 1.5), h, // Intermediate Magick size
            (int)(w * 1.5), h,
            w, h,              // Final JP2A size
            x + 1);

    def_prog_mode(); 
    printf("\x1b[%d;%dH", y + 1, x + 1);
    fflush(stdout);

    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[4096];
        while (fgets(line, sizeof(line), fp)) fputs(line, stdout);
        pclose(fp);
    }
    printf("\x1b[0m");
    fflush(stdout);
    reset_prog_mode();
}

