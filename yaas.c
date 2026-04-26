#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define MAX_LYRIC_LINES 2000
#define BUFFER_SIZE 20480
#define CMD_BUFFER 8192

typedef struct {
    char title[512];
    char artist[512];
    char album[512];
    char genre[256];
    char player[64];
    char art_url[4096]; 
    char *lyrics[MAX_LYRIC_LINES];
    int lyric_count;
} Metadata;

Metadata m;

void cleanup(int sig) {
    (void)sig;
    endwin();
    printf("\033[?25h"); 
    exit(0);
}

void clear_m() {
    for (int i = 0; i < m.lyric_count; i++) {
        if (m.lyrics[i]) free(m.lyrics[i]);
    }
    m.lyric_count = 0;
    memset(&m, 0, sizeof(Metadata));
}

void render_art(int width) {
    if (strlen(m.art_url) < 5) return;
    
    // 8KB buffer ensures the compiler is satisfied with potential string lengths
    char cmd[CMD_BUFFER]; 
    char *path = (strncmp(m.art_url, "file://", 7) == 0) ? m.art_url + 7 : m.art_url;
    
    if (strncmp(m.art_url, "http", 4) == 0) {
        snprintf(cmd, sizeof(cmd), 
            "curl -s \"%s\" | magick - -resize 1024x1024\\> webp:- | jp2a -b --width=%d --color-depth=24 -", 
            m.art_url, width);
    } else {
        snprintf(cmd, sizeof(cmd), 
            "magick \"%s\" -resize 1024x1024\\> webp:- | jp2a -b --width=%d --color-depth=24 -", 
            path, width);
    }

    move(7, 1);
    refresh();
    
    // Suppress system() output if needed, but we want the ASCII art on stdout
    if (system(cmd) == -1) {
        mvprintw(7, 1, "Error: Could not render art.");
    }
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int split = max_x / 2;

    clear();
    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(0, 1, "[ %s ] %s", m.player, m.title);
    attroff(A_BOLD);
    mvprintw(1, 1, "Genre: %s", m.genre);
    attroff(COLOR_PAIR(1));
    
    mvhline(2, 0, ACS_HLINE, split - 2);
    
    attron(COLOR_PAIR(2));
    mvprintw(4, 1, "Artist: %s", m.artist);
    mvprintw(5, 1, "Album:  %s", m.album);
    attroff(COLOR_PAIR(2));

    attron(COLOR_PAIR(3));
    mvprintw(0, split + 2, "[ LYRICS ]");
    for (int i = 0; i < (max_y - 3) && i < m.lyric_count; i++) {
        mvprintw(i + 1, split + 2, "%.*s", split - 4, m.lyrics[i]);
    }
    attroff(COLOR_PAIR(3));

    refresh();
    render_art(split - 6);
}

void parse_block(char *block) {
    clear_m();
    char *saveptr;
    char *token = strtok_r(block, "¶", &saveptr);
    while (token) {
        if (strncmp(token, "TITLE|", 6) == 0) {
            strncpy(m.title, token + 6, sizeof(m.title) - 1);
        } else if (strncmp(token, "ARTIST|", 7) == 0) {
            strncpy(m.artist, token + 7, sizeof(m.artist) - 1);
        } else if (strncmp(token, "ALBUM|", 6) == 0) {
            strncpy(m.album, token + 6, sizeof(m.album) - 1);
        } else if (strncmp(token, "GENRE|", 6) == 0) {
            strncpy(m.genre, token + 6, sizeof(m.genre) - 1);
        } else if (strncmp(token, "PLAYER|", 7) == 0) {
            strncpy(m.player, token + 7, sizeof(m.player) - 1);
        } else if (strncmp(token, "ART|", 4) == 0) {
            strncpy(m.art_url, token + 4, sizeof(m.art_url) - 1);
        } else if (strncmp(token, "LYRICS|", 7) == 0) {
            char *lyr_ptr = token + 7;
            char *line = strtok(lyr_ptr, "\n");
            while (line && m.lyric_count < MAX_LYRIC_LINES) {
                m.lyrics[m.lyric_count++] = strdup(line);
                line = strtok(NULL, "\n");
            }
        }
        token = strtok_r(NULL, "¶", &saveptr);
    }
}

int main() {
    signal(SIGINT, cleanup);
    initscr();
    start_color();
    use_default_colors();
    curs_set(0); 
    
    init_pair(1, COLOR_CYAN, -1);
    init_pair(2, COLOR_GREEN, -1);
    init_pair(3, 250, -1); 

    FILE *pipe = popen("playerctl metadata --follow --format "
                       "'TITLE|{{title}}¶ARTIST|{{artist}}¶ALBUM|{{album}}¶GENRE|{{genre}}¶PLAYER|{{playerName}}¶ART|{{mpris:artUrl}}¶LYRICS|{{asText}}¶[[END]]¶'", "r");

    if (!pipe) {
        endwin();
        fprintf(stderr, "Fatal: Could not start playerctl.\n");
        return 1;
    }

    char buffer[BUFFER_SIZE];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        if (strstr(buffer, "[[END]]")) {
            parse_block(buffer);
            draw_dashboard();
        }
    }
    
    cleanup(0);
    return 0;
}
