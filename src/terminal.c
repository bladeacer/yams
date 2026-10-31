// terminal.c
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "debug.h"
#include "utils.h"

void init_ui() {
    initscr();
    set_escdelay(25);
    cbreak();
    noecho();
    noqiflush();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);

    mousemask(0, NULL);
    mouseinterval(-1);
    intrflush(stdscr, FALSE);

    // Disable bracketed paste and mouse reporting
    printf("\033[?2004l");
    printf("\033[?1000l\033[?1002l\033[?1003l\033[?1006l");
    fflush(stdout);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, 250, -1);
        init_pair(4, COLOR_MAGENTA, -1);
    }
    DBG("Terminal initialized: %dx%d", COLS, LINES);
}

void cleanup_ui(void) {
    endwin();
    printf("\033[2J\033[H\033[?25h\033[0m");
    fflush(stdout);
}
