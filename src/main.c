#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <locale.h>
#include "ui.h"
#include "metadata.h"
#include "utils.h"

#define BUFFER_SIZE 262144
#define RESIZE_DELAY_MS 200

int main() {
    setlocale(LC_ALL, "");
    signal(SIGINT, cleanup_ui);
    init_ui();

    const char *fmt = "TITLE|{{title}}¶ARTIST|{{artist}}¶ALBUM|{{album}}¶GENRE|{{xesam:genre}}¶COMP|{{xesam:composer}}¶PATH|{{xesam:url}}¶PLAYER|{{playerName}}¶STATUS|{{status}}¶ART|{{mpris:artUrl}}¶LYRICS|{{xesam:asText}}¶[[END]]¶";
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "playerctl metadata --follow --format '%s'", fmt);
    
    FILE *pipe = popen(cmd, "r");
    int fd = fileno(pipe);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

    char *full_buffer = calloc(1, BUFFER_SIZE);
    long last_resize_time = 0;

    while (1) {
        int ch = getch();
        bool should_draw = false;

        if (ch == 'q') break;

        if (ch == KEY_RESIZE) {
            resize_pending = true;
            last_resize_time = get_ms();
            // Clear immediately on resize to prevent ghosting
            erase(); refresh(); 
        }

        if (resize_pending && (get_ms() - last_resize_time > RESIZE_DELAY_MS)) {
            resize_pending = false;
            should_draw = true;
        }

        if (ch == 'j' || ch == KEY_DOWN) { 
            scroll_pos++; 
            should_draw = true; 
        }
        if (ch == 'k' || ch == KEY_UP) { 
            if (scroll_pos > 0) scroll_pos--; 
            should_draw = true; 
        }

        // Handle incoming metadata
        char temp[8192];
        int n = read(fd, temp, sizeof(temp) - 1);
        if (n > 0) {
            temp[n] = '\0';
            strcat(full_buffer, temp);
            if (strstr(full_buffer, "[[END]]")) {
                parse_block(full_buffer);
                should_draw = true;
                full_buffer[0] = '\0';
            }
        }

        if (should_draw) {
            draw_dashboard();
        }
    }

    free(full_buffer);
    cleanup_ui(0);
    return 0;
}
