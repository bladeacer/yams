#include "art.h"
#include "art_manager.h"
#include "config.h"
#include "debug.h"
#include "metadata.h"
#include "player_backend/player_backend.h"
#include "ui.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <libavutil/log.h>
#include <locale.h>
#include <ncurses.h>
#include <panel.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUFFER_SIZE 131072
static char poll_buffer[BUFFER_SIZE];
static size_t buf_idx = 0;

FILE *log_file = NULL;
int player_fd = -1;
long last_resize_time = 0;
static pid_t metadata_pid = -1;
static bool help_active = false;
static long last_player_fail_ms = 0;
long last_draw_time = 0;
static const char *meta_fmt = NULL;
static long last_metadata_ms = 0;
static PlayerBackend *backend = NULL;

static bool process_metadata_stream(void);
static void maintain_subprocesses(const char *fmt, long now);

void handle_exit(int sig) {
  DBG("handle_exit called with signal %d", sig);
  if (player_fd != -1)
    close(player_fd);
  if (metadata_pid > 0)
    kill(metadata_pid, SIGKILL);
  if (backend && backend->cleanup)
    backend->cleanup();
  free_art();
  close_debug();
  cleanup_ui(sig);
}

void handle_winch(int sig) {
  (void)sig;
  resize_pending = true;
  last_resize_time = get_ms();
}

static bool query_initial_metadata(const char *fmt) {
  (void)fmt;
  process_metadata_stream();
  return true;
}

static void strip_data_art(char *block) {
  char *p = block;
  while ((p = strstr(p, "\tART|data:image")) != NULL) {
    char *val_start = p + 5;
    char *field_end = strchr(val_start, '\t');
    if (field_end) {
      size_t trailing = strlen(field_end);
      memmove(val_start + 1, field_end, trailing + 1);
    } else {
      val_start[1] = '\0';
    }
    val_start[0] = '@';
    p = val_start + 1;
  }
}

static bool process_metadata_stream() {
  bool changed = false;
  char *start = poll_buffer;

  DBG("process_metadata_stream: buf_idx=%zu, buffer=[%s]", buf_idx,
      poll_buffer);

  while (1) {
    char *end_tag = strstr(start, "[[END]]");
    if (!end_tag)
      break;

    DBG("  found [[END]] at offset %td", end_tag - poll_buffer);

    char *target = strstr(start, "PLAYER|");
    if (target && target < end_tag) {
      *end_tag = '\0';
      strip_data_art(target);
      DBG("  calling parse_block for block at offset %td: [%s]",
          target - poll_buffer, target);
      if (parse_block(target)) {
        changed = true;
      }
    } else {
      DBG("  skipping block (no PLAYER| marker)");
    }

    start = end_tag + 7;
  }

  size_t processed_len = start - poll_buffer;
  if (processed_len > 0) {
    size_t remaining = buf_idx - processed_len;
    if (remaining > 0) {
      memmove(poll_buffer, poll_buffer + processed_len, remaining);
    }
    buf_idx = remaining;
    poll_buffer[buf_idx] = '\0';
  }

  return changed;
}

void show_seek_input() {
  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x);

  int h = 3, w = 30;
  int sy = (max_y - h) / 2;
  int sx = (max_x - w) / 2;

  WINDOW *swin = newwin(h, w, sy, sx);
  PANEL *spanel = new_panel(swin);

  wbkgd(swin, COLOR_PAIR(3));
  box(swin, 0, 0);
  mvwprintw(swin, 0, 2, " Set Seek (sec) ");

  keypad(swin, TRUE);
  nodelay(swin, FALSE);
  curs_set(1);

  update_panels();
  doupdate();

  char input[10] = {0};
  int idx = 0;
  const int field_x = 2;
  const int field_y = 1;

  while (true) {
    wmove(swin, field_y, field_x + idx);
    int ch = wgetch(swin);

    if (ch == 27) {
      idx = 0;
      break;
    }

    if (ch == '\n' || ch == KEY_ENTER) {
      break;
    }

    if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
      if (idx > 0) {
        idx--;
        input[idx] = '\0';
        mvwaddch(swin, field_y, field_x + idx, ' ');
      }
    } else if (ch >= '0' && ch <= '9' && idx < (int)sizeof(input) - 1) {
      input[idx] = ch;
      mvwaddch(swin, field_y, field_x + idx, ch);
      idx++;
      input[idx] = '\0';
    }

    wrefresh(swin);
  }

  curs_set(0);
  del_panel(spanel);
  delwin(swin);

  update_panels();
  doupdate();

  if (idx > 0) {
    int val = atoi(input);
    if (val > 0)
      set_seek_interval(val);
  }

  reset_art();
  resize_pending = true;
}

static void handle_keypress(int ch, long now);
void show_help_overlay(long now);

static void handle_keypress(int ch, long now) {
  if (ch == ERR)
    return;

  char seek_cmd[64];

  DBG("Keystroke: ID=%d Char='%c'", ch, (ch >= 32 && ch <= 126) ? ch : '?');

  switch (ch) {
  case 'q':
    handle_exit(0);
    break;
  case ' ':
    backend->play_pause();
    break;
  case 'j':
    snprintf(seek_cmd, sizeof(seek_cmd), "%d-", get_seek_interval());
    backend->seek(seek_cmd);
    resize_pending = true;
    break;
  case 'k':
    snprintf(seek_cmd, sizeof(seek_cmd), "%d+", get_seek_interval());
    backend->seek(seek_cmd);
    resize_pending = true;
    break;
  case 'a':
    show_art_mode_selector();
    break;
  case 'p': {
    char old_player[64];
    strncpy(old_player, get_player_name(), sizeof(old_player) - 1);
    old_player[sizeof(old_player) - 1] = '\0';
    show_player_selector();
    if (strcmp(get_player_name(), old_player) != 0) {
      if (metadata_pid > 0)
        kill(metadata_pid, SIGKILL);
      if (player_fd != -1) {
        close(player_fd);
        player_fd = -1;
      }
      metadata_pid = -1;
      buf_idx = 0;
      poll_buffer[0] = '\0';
      clear_metadata();
      last_metadata_ms = 0;
      expire_art_debounce();
      if (meta_fmt) {
        metadata_pid = backend->spawn_stream(meta_fmt, &player_fd);
        if (player_fd != -1) {
          fcntl(player_fd, F_SETFL, fcntl(player_fd, F_GETFL, 0) | O_NONBLOCK);
        }
        query_initial_metadata(meta_fmt);
      }
    }
    resize_pending = true;
    break;
  }
  case 'c': {
    char *info = get_cache_info();
    char *msg = clear_art_cache();
    char buf[512];
    snprintf(buf, sizeof(buf), "%s  |  %s", info, msg);
    free(info);
    free(msg);
    show_status_msg(buf);
    resize_pending = true;
    break;
  }
  case '+':
    set_seek_interval(get_seek_interval() + 5);
    {
      char buf[64];
      snprintf(buf, sizeof(buf), "Seek interval: %ds", get_seek_interval());
      show_status_msg(buf);
    }
    resize_pending = true;
    break;
  case '-':
    set_seek_interval(get_seek_interval() - 5);
    {
      char buf[64];
      snprintf(buf, sizeof(buf), "Seek interval: %ds", get_seek_interval());
      show_status_msg(buf);
    }
    resize_pending = true;
    break;

  case 'h':
  case KEY_LEFT:
    backend->prev();
    resize_pending = true;
    last_draw_time = 0;
    break;
  case 'l':
  case KEY_RIGHT:
    backend->next();
    resize_pending = true;
    last_draw_time = 0;
    break;
  case 'r':
    DBG("Manual refresh requested");
    reset_art();
    expire_art_debounce();
    resize_pending = true;
    last_resize_time = now - 1000;
    break;

  case 's':
    show_seek_input();
    break;

  case 27:
  case KEY_MOUSE:
  case KEY_PPAGE:
  case KEY_NPAGE:
  case KEY_SR:
  case KEY_SF:
  case 259:
  case 258:
  case 410:
    break;

  case '?':
    if (!help_active)
      show_help_overlay(now);
    break;

  default:
    if (!help_active && ch >= 32 && ch <= 126) {
      show_help_overlay(now);
    }
    break;
  }
}

void show_help_overlay(long now) {
  if (help_active)
    return;
  help_active = true;

  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x);

  int h = 17, w = 45;
  int start_y = (max_y - h) / 2;
  int start_x = (max_x - w) / 2;

  WINDOW *help_win = newwin(h, w, start_y, start_x);
  PANEL *help_panel = new_panel(help_win);

  wbkgd(help_win, COLOR_PAIR(3));
  box(help_win, 0, 0);

  wattron(help_win, A_BOLD | COLOR_PAIR(4));
  mvwprintw(help_win, 1, (w - 10) / 2, "YAMS HELP");
  wattroff(help_win, A_BOLD | COLOR_PAIR(4));
  mvwhline(help_win, 2, 1, ACS_HLINE, w - 2);

  int ty = 4, tx = 6;
#define HELP_L(k, d)                                                           \
  {                                                                            \
    wattron(help_win, COLOR_PAIR(4) | A_BOLD);                                 \
    mvwprintw(help_win, ty, tx, k);                                            \
    wattroff(help_win, COLOR_PAIR(4) | A_BOLD);                                \
    mvwprintw(help_win, ty++, tx + 8, d);                                      \
  }

  HELP_L("q", "Quit Application");
  HELP_L("r", "Refresh");
  HELP_L("c", "Clear Art Cache");
  HELP_L("SPC", "Play / Pause");
  HELP_L("h/<-", "Previous Track");
  HELP_L("l/->", "Next Track");
  HELP_L("j/k", "Seek Interval");
  HELP_L("-/+", "Adjust Seek Interval");
  HELP_L("s", "Set Seek Interval");
  HELP_L("a", "Art Mode Selector");
  HELP_L("p", "Player Selector");
  HELP_L("ESC/?", "Close this menu");

  update_panels();
  doupdate();

  int ch;
  nodelay(stdscr, FALSE);
  while (true) {
    fd_set help_fds;
    FD_ZERO(&help_fds);
    FD_SET(STDIN_FILENO, &help_fds);
    struct timeval h_tv = {0, 250000};

    int h_act = select(STDIN_FILENO + 1, &help_fds, NULL, NULL, &h_tv);

    if (h_act > 0) {
      ch = wgetch(help_win);
      if (ch == 27 || ch == '?' || ch == 'q')
        break;
      handle_keypress(ch, now);
      update_panels();
      doupdate();
    }
  }
  nodelay(stdscr, TRUE);

  del_panel(help_panel);
  delwin(help_win);

  reset_art();
  resize_pending = true;
  help_active = false;
}

static void handle_player_data() {
  char chunk[16384];
  ssize_t n;
  bool data_read = false;
  static bool art_skip = false;

  while ((n = read(player_fd, chunk, sizeof(chunk))) > 0) {
    // backends that own their own stream format (e.g. macOS JSON) handle
    // parsing directly; the legacy playerctl path is used when NULL
    if (backend && backend->parse_chunk) {
      if (backend->parse_chunk(chunk, (size_t)n))
        resize_pending = true;
      data_read = true;
      continue;
    }

    if (art_skip) {
      char *tab = memchr(chunk, '\t', (size_t)n);
      if (tab) {
        size_t skip = (size_t)(tab - chunk);
        n -= (ssize_t)skip;
        memmove(chunk, tab, (size_t)n);
        art_skip = false;
        DBG("  art_skip: found \\t, kept %zd bytes (preserving separator)", n);
      } else {
        DBG("  art_skip: no \\t, skipping %zd bytes", n);
        continue;
      }
    }

    size_t off = 0;
    while (n > 0) {
      if ((size_t)n - off < 15)
        break;
      char *p =
          (char *)memmem(chunk + off, (size_t)n - off, "\tART|data:image", 15);
      if (!p)
        break;

      size_t pos = (size_t)(p - chunk);
      char *val = p + 5;
      size_t vrem = (size_t)n - (pos + 5);
      char *tab = (char *)memchr(val, '\t', vrem);

      if (tab) {
        size_t ulen = (size_t)(tab - val);
        size_t after = (size_t)n - (pos + 5 + ulen);
        val[0] = '@';
        if (after > 0)
          memmove(val + 1, tab, after);
        n -= (ssize_t)(ulen - 1);
        off = pos + 6;
        DBG("  stripped data URI at %zu, chunk now %zd bytes", pos, n);
      } else {
        size_t keep = pos + 6;
        chunk[pos + 5] = '@';
        n = (ssize_t)keep;
        art_skip = true;
        DBG("  truncated data URI at %zu, chunk now %zd, art_skip set", pos, n);
        break;
      }
    }

    if (buf_idx + n >= BUFFER_SIZE - 1) {
      DBG("OVERFLOW: buf_idx=%zu, incoming=%zd, BUFFER_SIZE=%d", buf_idx, n,
          BUFFER_SIZE);
      DBG("  buffer before: [%s]", poll_buffer);

      char *first = strstr(poll_buffer, "[[END]]");
      if (first) {
        size_t trailing = buf_idx - (first - poll_buffer + 7);
        memmove(poll_buffer, first + 7, trailing);
        buf_idx = trailing;
        DBG("  trimmed at [[END]], buf_idx now %zu", buf_idx);
      } else {
        DBG("  no [[END]], resetting buffer");
        buf_idx = 0;
      }
      poll_buffer[buf_idx] = '\0';

      if (buf_idx + n >= BUFFER_SIZE - 1) {
        DBG("  still overflows, full reset");
        buf_idx = 0;
        poll_buffer[0] = '\0';
      }
      DBG("  buffer after: [%s]", poll_buffer);
    }
    memcpy(poll_buffer + buf_idx, chunk, (size_t)n);
    buf_idx += (size_t)n;
    poll_buffer[buf_idx] = '\0';
    data_read = true;
  }

  if (data_read) {
    last_metadata_ms = get_ms();
    if (!backend || !backend->parse_chunk) {
      if (process_metadata_stream())
        resize_pending = true;
    }
  }

  if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
    close(player_fd);
    player_fd = -1;
    art_skip = false;
    clear_metadata();
    resize_pending = true;
  }
}

static void maintain_subprocesses(const char *fmt, long now) {
  if (player_fd != -1 && metadata_pid > 0) {
    int wstatus;
    pid_t ret = waitpid(metadata_pid, &wstatus, WNOHANG);
    if (ret == metadata_pid) {
      DBG("metadata_pid exited, stopping subprocess");
      close(player_fd);
      player_fd = -1;
      metadata_pid = -1;
      clear_metadata();
      resize_pending = true;
    } else if (last_metadata_ms > 0 && (now - last_metadata_ms) > 2000) {
      if (backend && !backend->has_player()) {
        DBG("no playerctl players found, forcing cleanup");
        kill(metadata_pid, SIGKILL);
        close(player_fd);
        player_fd = -1;
        metadata_pid = -1;
        last_metadata_ms = 0;
        clear_metadata();
        resize_pending = true;
      }
    }
  }

  if (player_fd == -1 && (now - last_player_fail_ms > 2000)) {
    DBG("Attempting playerctl restart...");
    metadata_pid = backend->spawn_stream(fmt, &player_fd);
    if (player_fd != -1) {
      fcntl(player_fd, F_SETFL, fcntl(player_fd, F_GETFL) | O_NONBLOCK);
    }
    last_player_fail_ms = now;
  }
}

int main() {
  av_log_set_level(AV_LOG_QUIET);
  setlocale(LC_ALL, "");
  if (getenv("YAMS_EXIT_IMMEDIATELY")) {
    init_ui();
    cleanup_ui(0);
    return 0;
  }
  init_debug();
  signal(SIGINT, handle_exit);
  signal(SIGTERM, handle_exit);
  signal(SIGWINCH, handle_winch);

  init_ui();
  nodelay(stdscr, TRUE);

  backend = player_backend_init();

  const char *fmt =
      "PLAYER|{{playerName}}	STATUS|{{status}}	"
      "TITLE|{{xesam:title}}	"
      "ARTIST|{{xesam:artist}}	ALBUM|{{xesam:album}}	"
      "PATH|{{xesam:url}}	"
      "LENGTH|{{mpris:length}}	GENRE|{{xesam:genre}}	"
      "COMPOSER|{{xesam:composer}}	"
      "ART|{{mpris:artUrl}}	LYRICS|{{xesam:asText}}	[[END]]";
  meta_fmt = fmt;

  metadata_pid = backend->spawn_stream(fmt, &player_fd);

  if (player_fd != -1)
    fcntl(player_fd, F_SETFL, fcntl(player_fd, F_GETFL, 0) | O_NONBLOCK);

  query_initial_metadata(fmt);
  resize_pending = true;

  bool debounce_triggered = false;

  while (1) {
    long now = get_ms();
    long elapsed_since_change = now - get_last_metadata_change_time();

    if (elapsed_since_change >= get_art_debounce() && !debounce_triggered) {
      resize_pending = true;
      debounce_triggered = true;
    }
    if (elapsed_since_change < 50)
      debounce_triggered = false;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    int max_fd = STDIN_FILENO;

    if (player_fd != -1) {
      FD_SET(player_fd, &fds);
      if (player_fd > max_fd)
        max_fd = player_fd;
    }

    maintain_subprocesses(fmt, now);

    struct timeval tv;

    if (resize_pending) {
      tv.tv_sec = 0;
      tv.tv_usec = 10000;
    } else if (elapsed_since_change < get_art_debounce()) {
      tv.tv_sec = 0;
      tv.tv_usec = 50000;
    } else {
      tv.tv_sec = 1;
      tv.tv_usec = 0;
    }

    int activity = select(max_fd + 1, &fds, NULL, NULL, &tv);

    if (activity < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    if (activity > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
      int ch;
      while ((ch = getch()) != ERR)
        handle_keypress(ch, now);
      resize_pending = true;
    }

    if (activity > 0 && player_fd != -1 && FD_ISSET(player_fd, &fds)) {
      handle_player_data();
    }

    if (resize_pending) {
      struct winsize size;
      if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_row != LINES || size.ws_col != COLS) {
          resizeterm(size.ws_row, size.ws_col);
          erase();
          resize_pending = true;
        }
      }

      draw_dashboard();
      last_draw_time = now;
      resize_pending = false;
    }

    usleep(10000);
  }

  draw_dashboard();

  handle_exit(0);
  return 0;
}
