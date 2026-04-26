#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include "debug.h"

#define MAX_LYRIC_LINES 3000
#define BUFFER_SIZE 65536
#define CMD_BUFFER 16384 // Doubled to prevent truncation warnings

typedef struct {
    char title[512], artist[512], album[512], player[64], art_url[4096], status[32];
    char genre[256], composer[512];
    char *lyrics[MAX_LYRIC_LINES];
    int lyric_count;
} Metadata;

Metadata m;
char last_art_url[4096] = {0}; // Art "Hash"
int scroll_pos = 0;

void cleanup(int sig) {
    (void)sig;
    endwin();
    printf("\033[?25h\033[0m"); 
    exit(0);
}

void clear_m() {
    for (int i = 0; i < m.lyric_count; i++) if (m.lyrics[i]) free(m.lyrics[i]);
    m.lyric_count = 0;
    scroll_pos = 0;
    memset(&m, 0, sizeof(Metadata));
}

void render_art(int x, int y, int w, int h) {
    if (strlen(m.art_url) < 5 || strcmp(m.art_url, "null") == 0) {
        mvprintw(y + (h/2), x + (w/2) - 8, "[ No Artwork ]");
        last_art_url[0] = '\0';
        return;
    }

    // Optimization: If art URL is same as last time, don't re-run the pipe
    // Note: This assumes terminal wasn't cleared/resized.
    static int last_w = 0, last_h = 0;
    if (strcmp(m.art_url, last_art_url) == 0 && w == last_w && h == last_h) {
        return; 
    }

    char cmd[CMD_BUFFER];
    char *path = (strncmp(m.art_url, "file://", 7) == 0) ? m.art_url + 7 : m.art_url;
    
    // Using (int) casts to silence compiler range warnings
    snprintf(cmd, sizeof(cmd),
        "magick \"%s\" -thumbnail %dx%d^ -gravity center -extent %dx%d webp:- | "
        "jp2a -b --width=%d --color-depth=24 - | "
        "sed 's/^/\\x1b[%dG/'", 
        path, (int)w*2, (int)h*2, (int)w*2, (int)h*2, (int)w, (int)x + 1);

    // Position cursor for ncurses, then manual ANSI jump
    move(y, x);
    refresh();
    printf("\x1b[%d;%dH", y + 1, x + 1);
    
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[4096];
        while (fgets(line, sizeof(line), fp)) {
            // Using raw write to stdout to avoid ncurses string sanitization
            fwrite(line, 1, strlen(line), stdout);
        }
        pclose(fp);
        strncpy(last_art_url, m.art_url, sizeof(last_art_url)-1);
        last_w = w; last_h = h;
    }
    fflush(stdout);
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int split = max_x / 2;

    erase(); // Clear without flicker
    if (strlen(m.title) == 0) {
        attron(A_BOLD | COLOR_PAIR(1));
        mvprintw(max_y/2, (max_x/2) - 8, "YAMS : STANDBY");
        attroff(A_BOLD | COLOR_PAIR(1));
        refresh();
        return;
    }

    // Header Area
    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(1, 2, "[%s] %s", m.status, m.player);
    mvprintw(2, 2, "%.*s", split - 4, m.title);
    attroff(A_BOLD);
    
    attron(COLOR_PAIR(2));
    mvprintw(3, 2, "Artist:   %.*s", split - 12, m.artist);
    mvprintw(4, 2, "Album:    %.*s", split - 12, m.album);
    if (strlen(m.genre) > 0)    mvprintw(5, 2, "Genre:    %.*s", split - 12, m.genre);
    if (strlen(m.composer) > 0) mvprintw(6, 2, "Composer: %.*s", split - 12, m.composer);
    attroff(COLOR_PAIR(2));
    
    mvhline(8, 2, ACS_HLINE, split - 4);

    // Lyrics Column
    attron(COLOR_PAIR(3));
    int lyric_h = max_y - 2;
    for (int i = 0; i < lyric_h && (i + scroll_pos) < m.lyric_count; i++) {
        mvprintw(i + 1, split + 2, "%.*s", max_x - split - 4, m.lyrics[i + scroll_pos]);
    }
    attroff(COLOR_PAIR(3));

    refresh(); // Push text first
    render_art(2, 9, split - 6, max_y - 11);
}

void parse_block(char *block) {
    clear_m();
    char *saveptr;
    char *token = strtok_r(block, "¶", &saveptr);
    while (token) {
        if (strncmp(token, "TITLE|", 6) == 0) strncpy(m.title, token + 6, 511);
        else if (strncmp(token, "ARTIST|", 7) == 0) strncpy(m.artist, token + 7, 511);
        else if (strncmp(token, "ALBUM|", 6) == 0) strncpy(m.album, token + 6, 511);
        else if (strncmp(token, "GENRE|", 6) == 0) strncpy(m.genre, token + 6, 255);
        else if (strncmp(token, "COMP|", 5) == 0) strncpy(m.composer, token + 5, 511);
        else if (strncmp(token, "PLAYER|", 7) == 0) strncpy(m.player, token + 7, 63);
        else if (strncmp(token, "STATUS|", 7) == 0) strncpy(m.status, token + 7, 31);
        else if (strncmp(token, "ART|", 4) == 0) strncpy(m.art_url, token + 4, 4095);
        else if (strncmp(token, "LYRICS|", 7) == 0) {
            char *lyr_data = strdup(token + 7);
            char *l_saveptr;
            char *line = strtok_r(lyr_data, "\n", &l_saveptr);
            while (line && m.lyric_count < MAX_LYRIC_LINES) {
                m.lyrics[m.lyric_count++] = strdup(line);
                line = strtok_r(NULL, "\n", &l_saveptr);
            }
            free(lyr_data);
        }
        token = strtok_r(NULL, "¶", &saveptr);
    }
}

int main() {
    signal(SIGINT, cleanup);
    initscr(); start_color(); use_default_colors();
    curs_set(0); noecho(); keypad(stdscr, TRUE); timeout(50);
    init_pair(1, COLOR_CYAN, -1);
    init_pair(2, COLOR_GREEN, -1);
    init_pair(3, 250, -1);

    const char *fmt = "TITLE|{{title}}¶ARTIST|{{artist}}¶ALBUM|{{album}}¶GENRE|{{genre}}¶COMP|{{composer}}¶PLAYER|{{playerName}}¶STATUS|{{status}}¶ART|{{mpris:artUrl}}¶LYRICS|{{xesam:asText}}¶[[END]]¶";
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "playerctl metadata --follow --format '%s'", fmt);
    
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return 1;

    int fd = fileno(pipe);
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    char full_buffer[BUFFER_SIZE] = {0};
    int total_bytes = 0;

    draw_dashboard();

    while (1) {
        int ch = getch();
        if (ch == 'j' || ch == KEY_DOWN) { scroll_pos++; draw_dashboard(); }
        if (ch == 'k' || ch == KEY_UP) { if (scroll_pos > 0) scroll_pos--; draw_dashboard(); }
        if (ch == 'q') break;

        char temp[2048];
        int n = read(fd, temp, sizeof(temp) - 1);
        if (n > 0) {
            temp[n] = '\0';
            if (total_bytes + n < BUFFER_SIZE) {
                strcat(full_buffer, temp);
                total_bytes += n;
            }
            char *end_marker = strstr(full_buffer, "[[END]]");
            if (end_marker) {
                parse_block(full_buffer);
                draw_dashboard();
                memset(full_buffer, 0, BUFFER_SIZE);
                total_bytes = 0;
            }
        } else if (n == 0) {
            pclose(pipe);
            pipe = popen(cmd, "r");
            fd = fileno(pipe);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }
    cleanup(0);
    return 0;
}
