#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "metadata.h"
#include "player_backend/player_backend_macos.h"

static pid_t s_perl_pid = -1;
static pid_t ctrl_pid = -1;
static int64_t s_elapsed_us = 0;
static int64_t s_timestamp_us = 0;

#define LINE_BUF_CAP 65536
static char s_line_buf[LINE_BUF_CAP];
static size_t s_line_len = 0;

static const char *get_script_path(void) {
  const char *env = getenv("YAMS_ADAPTER_SCRIPT");
  if (env)
    return env;
  static char buf[PATH_MAX];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0)
    return "mediaremote-adapter.pl";
  char *slash = strrchr(buf, '/');
  if (slash)
    snprintf(slash + 1, sizeof(buf) - (size_t)(slash + 1 - buf),
             "mediaremote-adapter.pl");
  return buf;
}

static const char *get_framework_path(void) {
  const char *env = getenv("YAMS_ADAPTER_FRAMEWORK");
  if (env)
    return env;
  static char buf[PATH_MAX];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0)
    return "MediaRemoteAdapter.framework";
  char *slash = strrchr(buf, '/');
  if (slash)
    snprintf(slash + 1, sizeof(buf) - (size_t)(slash + 1 - buf),
             "MediaRemoteAdapter.framework");
  return buf;
}

static void run_perl_cmd(const char *subcmd_fmt, ...) {
  char subcmd[128];
  va_list ap;
  va_start(ap, subcmd_fmt);
  vsnprintf(subcmd, sizeof(subcmd), subcmd_fmt, ap);
  va_end(ap);

  if (ctrl_pid > 0) {
    waitpid(ctrl_pid, NULL, WNOHANG);
    ctrl_pid = -1;
  }

  char *tokens[6];
  int tc = 0;
  char *p = subcmd;
  while (*p && tc < 6) {
    while (*p == ' ')
      p++;
    if (!*p)
      break;
    tokens[tc++] = p;
    while (*p && *p != ' ')
      p++;
    if (*p)
      *p++ = '\0';
  }

  // build args: /usr/bin/perl script framework [tokens...]
  char *args[12];
  int ai = 0;
  args[ai++] = "/usr/bin/perl";
  args[ai++] = (char *)get_script_path();
  args[ai++] = (char *)get_framework_path();
  for (int i = 0; i < tc; i++)
    args[ai++] = tokens[i];
  args[ai] = NULL;

  pid_t pid = fork();
  if (pid == 0) {
    int dn = open("/dev/null", O_WRONLY);
    dup2(dn, STDOUT_FILENO);
    dup2(dn, STDERR_FILENO);
    close(dn);
    execvp("/usr/bin/perl", args);
    _exit(1);
  } else if (pid > 0) {
    ctrl_pid = pid;
  }
}

static int macos_play_pause(void) {
  run_perl_cmd("send 2");
  return 0;
}
static int macos_next(void) {
  run_perl_cmd("send 4");
  return 0;
}
static int macos_prev(void) {
  run_perl_cmd("send 5");
  return 0;
}

static int macos_seek(const char *param) {
  // param: "N+" (forward) or "N-" (backward), N is seconds
  int secs = atoi(param);
  bool fwd = (param[strlen(param) - 1] == '+');

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  int64_t now_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

  int64_t pos = s_elapsed_us + (now_us - s_timestamp_us);
  pos += (fwd ? 1 : -1) * (int64_t)secs * 1000000;
  if (pos < 0)
    pos = 0;

  run_perl_cmd("seek %lld", (long long)pos);
  return 0;
}

static pid_t macos_spawn_stream(const char *fmt, int *out_fd) {
  (void)fmt;
  int pipe_fds[2];
  if (pipe(pipe_fds) == -1)
    return -1;

  pid_t pid = fork();
  if (pid == 0) {
    close(pipe_fds[0]);
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[1]);
    int dn = open("/dev/null", O_WRONLY);
    dup2(dn, STDERR_FILENO);
    close(dn);
    char *args[] = {"/usr/bin/perl",
                    (char *)get_script_path(),
                    (char *)get_framework_path(),
                    "stream",
                    "--no-diff",
                    "--micros",
                    NULL};
    execvp("/usr/bin/perl", args);
    _exit(1);
  }

  close(pipe_fds[1]);
  if (out_fd)
    *out_fd = pipe_fds[0];
  s_perl_pid = pid;
  return pid;
}

static NSString *str_or_empty(id val) {
  if (!val || val == [NSNull null])
    return @"";
  if ([val isKindOfClass:[NSString class]])
    return val;
  return [val description];
}

static bool process_json_line(char *line) {
  @autoreleasepool {
    NSData *data = [NSData dataWithBytes:line length:strlen(line)];
    NSError *err;
    id root = [NSJSONSerialization JSONObjectWithData:data
                                              options:0
                                                error:&err];
    if (!root || ![root isKindOfClass:[NSDictionary class]])
      return false;

    NSDictionary *obj = root;
    NSString *type = obj[@"type"];
    if (!type || ![type isEqualToString:@"data"])
      return false;

    id payload_val = obj[@"payload"];
    if (!payload_val || payload_val == [NSNull null])
      return false;
    if (![payload_val isKindOfClass:[NSDictionary class]])
      return false;
    NSDictionary *payload = payload_val;

    // update position tracking for seek
    id el = payload[@"elapsedTimeMicros"];
    id ts = payload[@"timestampEpochMicros"];
    if (el && el != [NSNull null])
      s_elapsed_us = [el longLongValue];
    if (ts && ts != [NSNull null])
      s_timestamp_us = [ts longLongValue];

    NSString *bundle = str_or_empty(payload[@"bundleIdentifier"]);
    id playing_val = payload[@"playing"];
    BOOL playing = (playing_val && playing_val != [NSNull null])
                       ? [playing_val boolValue]
                       : NO;
    NSString *title = str_or_empty(payload[@"title"]);
    NSString *artist = str_or_empty(payload[@"artist"]);
    NSString *album = str_or_empty(payload[@"album"]);

    char dur_str[32] = "";
    id dur = payload[@"durationMicros"];
    if (dur && dur != [NSNull null])
      snprintf(dur_str, sizeof(dur_str), "%lld",
               (long long)[dur longLongValue]);

    static char s_art_path[PATH_MAX] = "";
    id artData = payload[@"artworkData"];
    id artMime = payload[@"artworkMimeType"];
    if (artData && artData != [NSNull null] &&
        [artData isKindOfClass:[NSString class]]) {
      NSData *img = [[NSData alloc] initWithBase64EncodedString:artData
                                                        options:0];
      if (img) {
        const char *ext = "jpg";
        if (artMime && artMime != [NSNull null]) {
          NSString *mime = artMime;
          if ([mime containsString:@"png"])
            ext = "png";
          else if ([mime containsString:@"webp"])
            ext = "webp";
        }
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd)))
          strlcpy(cwd, ".", sizeof(cwd));
        snprintf(s_art_path, sizeof(s_art_path), "%s/yams_macos_art.%s", cwd,
                 ext);
        [img writeToFile:[NSString stringWithUTF8String:s_art_path]
              atomically:NO];
      }
    }

    char block[4096];
    snprintf(block, sizeof(block),
             "PLAYER|%s\tSTATUS|%s\tTITLE|%s\tARTIST|%s\tALBUM|%s\t"
             "PATH|\tLENGTH|%s\tGENRE|\tCOMPOSER|\tART|%s%s\tLYRICS|\t[[END]]",
             [bundle UTF8String], playing ? "Playing" : "Paused",
             [title UTF8String], [artist UTF8String], [album UTF8String],
             dur_str, s_art_path[0] ? "file://" : "", s_art_path);

    return parse_block(block);
  }
}

static bool macos_parse_chunk(const char *data, size_t n) {
  bool changed = false;
  for (size_t i = 0; i < n; i++) {
    char c = data[i];
    if (c == '\n') {
      if (s_line_len > 0) {
        s_line_buf[s_line_len] = '\0';
        if (process_json_line(s_line_buf))
          changed = true;
        s_line_len = 0;
      }
    } else {
      if (s_line_len < LINE_BUF_CAP - 1)
        s_line_buf[s_line_len++] = c;
    }
  }
  return changed;
}

static bool macos_has_player(void) {
  int pipe_fds[2];
  if (pipe(pipe_fds) == -1)
    return true;

  pid_t pid = fork();
  if (pid == -1) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return true;
  }
  if (pid == 0) {
    close(pipe_fds[0]);
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[1]);
    int dn = open("/dev/null", O_WRONLY);
    dup2(dn, STDERR_FILENO);
    close(dn);
    char *args[] = {"/usr/bin/perl", (char *)get_script_path(),
                    (char *)get_framework_path(), "get", NULL};
    execvp("/usr/bin/perl", args);
    _exit(1);
  }

  close(pipe_fds[1]);
  char first = 0;
  ssize_t nr = read(pipe_fds[0], &first, 1);
  // drain remaining output
  char drain[4096];
  while (read(pipe_fds[0], drain, sizeof(drain)) > 0) {
  }
  close(pipe_fds[0]);
  int wstatus;
  waitpid(pid, &wstatus, 0);

  return nr > 0 && first == '{';
}

static void macos_cleanup(void) {
  if (ctrl_pid > 0) {
    kill(ctrl_pid, SIGKILL);
    ctrl_pid = -1;
  }
  s_perl_pid = -1;
}

static PlayerBackend macos_backend = {
    .play_pause = macos_play_pause,
    .next = macos_next,
    .prev = macos_prev,
    .seek = macos_seek,
    .spawn_stream = macos_spawn_stream,
    .has_player = macos_has_player,
    .parse_chunk = macos_parse_chunk,
    .get_metadata = NULL,
    .get_status = NULL,
    .set_volume = NULL,
    .cleanup = macos_cleanup,
};

PlayerBackend *macos_backend_init(void) { return &macos_backend; }

#endif // __APPLE__
