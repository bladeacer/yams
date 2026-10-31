#include "art.h"
#include "art_manager.h"
#include "config.h"
#include "debug.h"
#include "metadata.h"
#include "player_backend/player_backend.h"
#include "stream.h"
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
static void maintain_subprocesses(long now);

volatile sig_atomic_t exit_requested = 0;
static volatile sig_atomic_t exit_signal = 0;
static volatile sig_atomic_t cleanup_started = 0;

/* Signal handlers must only touch sig_atomic_t state. All teardown happens in
 * handle_exit() from the main loop. If a second signal arrives while cleanup is
 * already running, fall back to the only async-signal-safe exit. */
static void handle_exit_signal(int sig) {
  if (cleanup_started)
    _exit(128 + sig);
  exit_signal = sig;
  exit_requested = 1;
}

static void handle_winch(int sig) {
  (void)sig;
  resize_pending = true;
}

static volatile sig_atomic_t child_event = 0;

static void handle_child(int sig) {
  (void)sig;
  child_event = 1;
}

static void handle_exit(int sig) {
  (void)sig;
  cleanup_started = 1;
  DBG("handle_exit called with signal %d", sig);
  if (player_fd != -1) {
    close(player_fd);
    player_fd = -1;
  }
  if (metadata_pid > 0) {
    kill(metadata_pid, SIGKILL);
    metadata_pid = -1;
  }
  if (backend && backend->cleanup)
    backend->cleanup();
  free_art();
  close_debug();
  cleanup_ui();
}

static void install_signal_handlers(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0; /* let select() return EINTR so the main loop sees flags */

  sa.sa_handler = handle_exit_signal;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  sa.sa_handler = handle_winch;
  sigaction(SIGWINCH, &sa, NULL);

  sa.sa_handler = handle_child;
  sigaction(SIGCHLD, &sa, NULL);
}

static void stop_metadata_stream(void) {
  if (metadata_pid > 0) {
    kill(metadata_pid, SIGKILL);
    metadata_pid = -1;
  }
  if (player_fd != -1) {
    close(player_fd);
    player_fd = -1;
  }
  buf_idx = 0;
  poll_buffer[0] = '\0';
}

static void start_metadata_stream(void) {
  if (!backend || !meta_fmt)
    return;

  metadata_pid = backend->spawn_stream(meta_fmt, &player_fd);
  if (player_fd != -1)
    fcntl(player_fd, F_SETFL, fcntl(player_fd, F_GETFL, 0) | O_NONBLOCK);
}

static void restart_metadata_stream(void) {
  stop_metadata_stream();
  clear_metadata();
  last_metadata_ms = 0;
  expire_art_debounce();
  start_metadata_stream();
}

/* Reap every terminated child once per SIGCHLD. The handler only sets a flag,
 * so all waitpid() calls happen here, off the signal path. */
static void reap_children(void) {
  int status;
  pid_t pid;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (pid == metadata_pid) {
      if (WIFEXITED(status))
        DBG("metadata stream %d exited with status %d", (int)pid,
            WEXITSTATUS(status));
      else if (WIFSIGNALED(status))
        DBG("metadata stream %d killed by signal %d", (int)pid,
            WTERMSIG(status));
      metadata_pid = -1;
      if (player_fd != -1) {
        close(player_fd);
        player_fd = -1;
      }
      clear_metadata();
      resize_pending = true;
    } else if (backend && backend->reap_child) {
      backend->reap_child(pid);
    } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
      DBG("child %d exited with status %d", (int)pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      DBG("child %d killed by signal %d", (int)pid, WTERMSIG(status));
    }
  }
}

static void strip_data_art(char *block) {
  size_t len = strlen(block);
  bool skip = false;
  stream_strip_data_art(block, &len, &skip);
  block[len] = '\0';
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
  if (h > max_y)
    h = max_y;
  if (w > max_x)
    w = max_x;
  if (h < 2 || w < 6)
    return;

  int sy = (max_y - h) / 2;
  int sx = (max_x - w) / 2;

  WINDOW *swin = newwin(h, w, sy, sx);
  if (!swin)
    return;
  PANEL *spanel = new_panel(swin);
  if (!spanel) {
    delwin(swin);
    return;
  }

  wbkgd(swin, COLOR_PAIR(3));
  box(swin, 0, 0);
  mvwprintw(swin, 0, 2, " Set Seek (sec) ");

  keypad(swin, TRUE);
  wtimeout(swin, 100);
  curs_set(1);

  update_panels();
  doupdate();

  char input[10] = {0};
  int idx = 0;
  const int field_x = 2;
  const int field_y = 1;

  while (true) {
    if (exit_requested) {
      idx = 0;
      break;
    }

    if (field_y >= h)
      break;

    if (resize_pending) {
      struct winsize size;
      if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_row != LINES || size.ws_col != COLS)
          resizeterm(size.ws_row, size.ws_col);
        max_y = size.ws_row;
        max_x = size.ws_col;
      }
      int nh = 3, nw = 30;
      if (nh > max_y)
        nh = max_y;
      if (nw > max_x)
        nw = max_x;
      if (nh < 2 || nw < 6) {
        idx = 0;
        break;
      }
      h = nh;
      w = nw;
      wresize(swin, h, w);
      mvwin(swin, (max_y - h) / 2, (max_x - w) / 2);
      werase(swin);
      wbkgd(swin, COLOR_PAIR(3));
      box(swin, 0, 0);
      mvwprintw(swin, 0, 2, " Set Seek (sec) ");
      if (idx > 0)
        mvwprintw(swin, field_y, field_x, "%s", input);
      update_panels();
      doupdate();
      resize_pending = false;
    }

    wmove(swin, field_y, field_x + idx);
    int ch = wgetch(swin);

    if (ch == KEY_RESIZE || ch == 410) {
      resize_pending = true;
      continue;
    }

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
    } else if (ch >= '0' && ch <= '9' && idx < (int)sizeof(input) - 1 &&
               field_x + idx < w - 1) {
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
    exit_requested = 1;
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
      restart_metadata_stream();
    }
    resize_pending = true;
    break;
  }
  case 'c': {
    char *info = get_cache_info();
    char *msg = clear_art_cache();
    char buf[512];
    if (info && msg)
      snprintf(buf, sizeof(buf), "%s  |  %s", info, msg);
    else if (info)
      snprintf(buf, sizeof(buf), "%s", info);
    else if (msg)
      snprintf(buf, sizeof(buf), "%s", msg);
    else
      snprintf(buf, sizeof(buf), "Art cache unavailable");
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

static void draw_help_window(WINDOW *win, int h, int w) {
  static const struct {
    const char *key;
    const char *desc;
  } help_lines[] = {
      {"q", "Quit Application"},   {"r", "Refresh"},
      {"c", "Clear Art Cache"},    {"SPC", "Play / Pause"},
      {"h/<-", "Previous Track"},  {"l/->", "Next Track"},
      {"j/k", "Seek Interval"},    {"-/+", "Adjust Seek Interval"},
      {"s", "Set Seek Interval"},  {"a", "Art Mode Selector"},
      {"p", "Player Selector"},    {"ESC/?", "Close this menu"},
  };
  const int line_count = (int)(sizeof(help_lines) / sizeof(help_lines[0]));

  werase(win);
  wbkgd(win, COLOR_PAIR(3));
  if (h < 2 || w < 2)
    return;
  box(win, 0, 0);

  int title_x = (w - 10) / 2;
  if (title_x < 1)
    title_x = 1;
  wattron(win, A_BOLD | COLOR_PAIR(4));
  mvwprintw(win, 1, title_x, "YAMS HELP");
  wattroff(win, A_BOLD | COLOR_PAIR(4));

  if (h > 3 && w > 3)
    mvwhline(win, 2, 1, ACS_HLINE, w - 2);

  int ty = 4;
  int tx = (w >= 34) ? 6 : 2;
  for (int i = 0; i < line_count && ty < h - 1; i++, ty++) {
    wattron(win, COLOR_PAIR(4) | A_BOLD);
    mvwprintw(win, ty, tx, "%s", help_lines[i].key);
    wattroff(win, COLOR_PAIR(4) | A_BOLD);
    if (tx + 8 < w - 1)
      mvwprintw(win, ty, tx + 8, "%s", help_lines[i].desc);
  }
}

void show_help_overlay(long now) {
  if (help_active)
    return;
  help_active = true;

  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x);

  int h = max_y < 17 ? max_y : 17;
  int w = max_x < 45 ? max_x : 45;
  if (h < 3 || w < 10) {
    help_active = false;
    return;
  }

  int start_y = (max_y - h) / 2;
  int start_x = (max_x - w) / 2;

  WINDOW *help_win = newwin(h, w, start_y, start_x);
  if (!help_win) {
    help_active = false;
    return;
  }
  PANEL *help_panel = new_panel(help_win);
  if (!help_panel) {
    delwin(help_win);
    help_active = false;
    return;
  }

  keypad(help_win, TRUE);
  draw_help_window(help_win, h, w);
  update_panels();
  doupdate();

  wtimeout(help_win, 100);
  bool redraw = false;
  while (true) {
    if (exit_requested)
      break;

    if (resize_pending) {
      struct winsize size;
      if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_row != LINES || size.ws_col != COLS)
          resizeterm(size.ws_row, size.ws_col);
        max_y = size.ws_row;
        max_x = size.ws_col;
      }
      int nh = max_y < 17 ? max_y : 17;
      int nw = max_x < 45 ? max_x : 45;
      if (nh < 3 || nw < 10)
        break;
      if (nh != h || nw != w) {
        h = nh;
        w = nw;
        wresize(help_win, h, w);
        mvwin(help_win, (max_y - h) / 2, (max_x - w) / 2);
        redraw = true;
      }
      resize_pending = false;
    }

    if (redraw) {
      draw_help_window(help_win, h, w);
      update_panels();
      doupdate();
      redraw = false;
    }

    fd_set help_fds;
    FD_ZERO(&help_fds);
    FD_SET(STDIN_FILENO, &help_fds);
    struct timeval h_tv = {0, 250000};

    int h_act = select(STDIN_FILENO + 1, &help_fds, NULL, NULL, &h_tv);

    if (h_act < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    if (h_act > 0) {
      int ch = wgetch(help_win);
      if (ch == ERR)
        continue;
      if (ch == KEY_RESIZE || ch == 410) {
        resize_pending = true;
        continue;
      }
      if (ch == 27 || ch == '?' || ch == 'q')
        break;
      handle_keypress(ch, now);
      redraw = true;
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
    // backends that own their own stream format handle
    // parsing directly; the legacy playerctl path is used when NULL
    if (backend && backend->parse_chunk) {
      if (backend->parse_chunk(chunk, (size_t)n))
        resize_pending = true;
      data_read = true;
      continue;
    }

    size_t len = (size_t)n;
    stream_strip_data_art(chunk, &len, &art_skip);
    buf_idx = stream_buffer_append(poll_buffer, sizeof(poll_buffer), buf_idx,
                                   chunk, len);
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
    stop_metadata_stream();
    art_skip = false;
    clear_metadata();
    resize_pending = true;
  }
}

static void maintain_subprocesses(long now) {
  if (player_fd != -1 && metadata_pid > 0 && last_metadata_ms > 0 &&
      (now - last_metadata_ms) > 2000) {
    if (backend && !backend->has_player()) {
      DBG("no playerctl players found, forcing cleanup");
      stop_metadata_stream();
      last_metadata_ms = 0;
      clear_metadata();
      resize_pending = true;
    }
  }

  if (player_fd == -1 && (now - last_player_fail_ms > 2000)) {
    DBG("Attempting playerctl restart...");
    start_metadata_stream();
    last_player_fail_ms = now;
  }
}

int main() {
  av_log_set_level(AV_LOG_QUIET);
  setlocale(LC_ALL, "");
  if (getenv("YAMS_EXIT_IMMEDIATELY")) {
    init_ui();
    cleanup_ui();
    return 0;
  }
  init_debug();
  install_signal_handlers();

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

  resize_pending = true;

  bool debounce_triggered = false;

  while (1) {
    if (exit_requested)
      break;

    if (child_event) {
      child_event = 0;
      reap_children();
    }

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

    maintain_subprocesses(now);

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
      DBG("select failed: %s", strerror(errno));
      usleep(100000);
      continue;
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

  handle_exit((int)exit_signal);
  return 0;
}
