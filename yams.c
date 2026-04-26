#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include "debug.h"

#define MAX_LYRIC_LINES 3000
#define BUFFER_SIZE 131072
#define CMD_BUFFER 16384

typedef struct {
    char title[512], artist[512], album[512], player[64], art_url[4096], status[32];
    char genre[256], composer[512];
    char *lyrics[MAX_LYRIC_LINES];
    int lyric_count;
} Metadata;

Metadata m;
char last_art_url[4096] = {0};
int scroll_pos = 0;

void cleanup(int sig) {
    (void)sig;
    endwin();
    printf("\033[?25h\033[0m"); 
    exit(0);
}

void clear_m_lyrics() {
    for (int i = 0; i < m.lyric_count; i++) {
        if (m.lyrics[i]) free(m.lyrics[i]);
    }
    m.lyric_count = 0;
}

void render_art(int x, int y, int w, int h) {
    if (strlen(m.art_url) < 5 || strcmp(m.art_url, "null") == 0) {
        mvprintw(y + (h/2), x + (w/2) - 8, "[ No Artwork ]");
        last_art_url[0] = '\0';
        return;
    }

    static int last_w = 0, last_h = 0;
    if (strcmp(m.art_url, last_art_url) == 0 && w == last_w && h == last_h) return;

    char cmd[CMD_BUFFER];
    char *path = (strncmp(m.art_url, "file://", 7) == 0) ? m.art_url + 7 : m.art_url;
    
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(cmd, sizeof(cmd),
        "magick \"%s\" -thumbnail %dx%d^ -gravity center -extent %dx%d webp:- | "
        "jp2a -b --width=%d --color-depth=24 - | "
        "sed 's/^/\\x1b[%dG/'", 
        path, w*2, h*2, w*2, h*2, w, x + 1);
    #pragma GCC diagnostic pop

    move(y, x);
    refresh();
    printf("\x1b[%d;%dH", y + 1, x + 1);
    
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[4096];
        while (fgets(line, sizeof(line), fp)) fwrite(line, 1, strlen(line), stdout);
        pclose(fp);
        
        // Fixed: Removed the 4th argument
        strncpy(last_art_url, m.art_url, sizeof(last_art_url) - 1);
        last_art_url[sizeof(last_art_url) - 1] = '\0';
        
        last_w = w; last_h = h;
    }
    fflush(stdout);
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int split = max_x / 2;

    erase();
    if (strlen(m.title) == 0) {
        attron(A_BOLD | COLOR_PAIR(1));
        mvprintw(max_y/2, (max_x/2) - 8, "YAMS : STANDBY");
        attroff(A_BOLD | COLOR_PAIR(1));
        refresh();
        return;
    }

    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(1, 2, "[%s] %s", strlen(m.status) ? m.status : "Unknown", m.player);
    mvprintw(2, 2, "%.*s", split - 4, m.title);
    attroff(A_BOLD);
    
    attron(COLOR_PAIR(2));
    mvprintw(3, 2, "Artist:   %.*s", split - 12, strlen(m.artist) ? m.artist : "Unknown");
    mvprintw(4, 2, "Album:    %.*s", split - 12, strlen(m.album) ? m.album : "Unknown");
    mvprintw(5, 2, "Genre:    %.*s", split - 12, (strlen(m.genre) > 0) ? m.genre : "---");
    mvprintw(6, 2, "Composer: %.*s", split - 12, (strlen(m.composer) > 0) ? m.composer : "---");
    attroff(COLOR_PAIR(2));
    
    mvhline(8, 2, ACS_HLINE, split - 4);

    attron(COLOR_PAIR(3));
    int lyric_h = max_y - 2;
    for (int i = 0; i < lyric_h && (i + scroll_pos) < m.lyric_count; i++) {
        mvprintw(i + 1, split + 2, "%.*s", max_x - split - 4, m.lyrics[i + scroll_pos]);
    }
    attroff(COLOR_PAIR(3));

    refresh();
    render_art(2, 9, split - 6, max_y - 11);
}

void parse_block(char *block) {
    clear_m_lyrics();
    m.title[0] = m.artist[0] = m.album[0] = m.genre[0] = m.composer[0] = m.status[0] = '\0';

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
            char *l_saveptr, *line = strtok_r(lyr_data, "\n", &l_saveptr);
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
    curs_set(0); noecho(); keypad(stdscr, TRUE); timeout(100);
    init_pair(1, COLOR_CYAN, -1); init_pair(2, COLOR_GREEN, -1); init_pair(3, 250, -1);

    // Some players use xesam:genre (array), others just genre. 
    // playerctl usually flattens these, but being explicit with xesam can help.
    const char *fmt = "TITLE|{{title}}¶ARTIST|{{artist}}¶ALBUM|{{album}}¶GENRE|{{xesam:genre}}¶COMP|{{xesam:composer}}¶PLAYER|{{playerName}}¶STATUS|{{status}}¶ART|{{mpris:artUrl}}¶LYRICS|{{xesam:asText}}¶[[END]]¶";
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "playerctl metadata --follow --format '%s'", fmt);
    
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return 1;
    int fd = fileno(pipe);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

    char *full_buffer = calloc(1, BUFFER_SIZE);
    int total_bytes = 0;

    while (1) {
        int ch = getch();
        if (ch == 'j' || ch == KEY_DOWN) { scroll_pos++; draw_dashboard(); }
        if (ch == 'k' || ch == KEY_UP) { if (scroll_pos > 0) scroll_pos--; draw_dashboard(); }
        if (ch == 'q') break;

        char temp[4096];
        int n = read(fd, temp, sizeof(temp) - 1);
        if (n > 0) {
            temp[n] = '\0';
            if (total_bytes + n < BUFFER_SIZE - 1) {
                memcpy(full_buffer + total_bytes, temp, n);
                total_bytes += n;
                full_buffer[total_bytes] = '\0';
                char *end_marker = strstr(full_buffer, "[[END]]");
                if (end_marker) {
                    parse_block(full_buffer);
                    draw_dashboard();
                    memset(full_buffer, 0, BUFFER_SIZE);
                    total_bytes = 0;
                }
            }
        } else if (n == 0) {
            pclose(pipe);
            pipe = popen(cmd, "r");
            fd = fileno(pipe);
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        }
    }
    free(full_buffer);
    cleanup(0);
    return 0;
}
