#include "player_backend/player_backend_mpris.h"
#include "config.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t ctrl_pid = -1;

static void reap_ctrl(void) {
  if (ctrl_pid > 0) {
    waitpid(ctrl_pid, NULL, WNOHANG);
    ctrl_pid = -1;
  }
}

static void run_cmd(const char *action, const char *param) {
  reap_ctrl();
  pid_t pid = fork();
  if (pid == 0) {
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);

    const char *player = get_player_name();
    char *args[6];
    int i = 0;
    args[i++] = "playerctl";
    if (player[0]) {
      args[i++] = "--player";
      args[i++] = (char *)player;
    }
    args[i++] = (char *)action;
    if (param)
      args[i++] = (char *)param;
    args[i] = NULL;
    execvp("playerctl", args);
    _exit(1);
  } else if (pid > 0) {
    ctrl_pid = pid;
  }
}

static int mpris_play_pause(void) {
  run_cmd("play-pause", NULL);
  return 0;
}

static int mpris_next(void) {
  run_cmd("next", NULL);
  return 0;
}

static int mpris_prev(void) {
  run_cmd("previous", NULL);
  return 0;
}

static int mpris_seek(const char *param) {
  run_cmd("position", param);
  return 0;
}

static pid_t mpris_spawn_stream(const char *fmt, int *out_fd) {
  int pipe_fds[2];
  if (out_fd && pipe(pipe_fds) == -1)
    return -1;

  pid_t pid = fork();
  if (pid == 0) {
    prctl(PR_SET_PDEATHSIG, SIGTERM);

    if (out_fd) {
      close(pipe_fds[0]);
      dup2(pipe_fds[1], STDOUT_FILENO);
      close(pipe_fds[1]);
    } else {
      int devnull = open("/dev/null", O_WRONLY);
      dup2(devnull, STDOUT_FILENO);
      close(devnull);
    }

    int devnull_err = open("/dev/null", O_WRONLY);
    dup2(devnull_err, STDERR_FILENO);
    close(devnull_err);

    const char *player = get_player_name();
    char *args[8];
    int i = 0;
    args[i++] = "playerctl";
    if (player[0]) {
      args[i++] = "--player";
      args[i++] = (char *)player;
    }
    args[i++] = "metadata";
    args[i++] = "--follow";
    args[i++] = "--format";
    args[i++] = (char *)fmt;
    args[i] = NULL;
    execvp("playerctl", args);
    _exit(1);
  }

  if (out_fd) {
    close(pipe_fds[1]);
    *out_fd = pipe_fds[0];
  }
  return pid;
}

static bool mpris_has_player(void) {
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
    dup2(pipe_fds[1], STDOUT_FILENO);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDERR_FILENO);
    close(devnull);
    char *args[] = {"playerctl", "-l", NULL};
    execvp("playerctl", args);
    _exit(1);
  }
  close(pipe_fds[1]);
  char buf[128];
  ssize_t n = read(pipe_fds[0], buf, sizeof(buf) - 1);
  close(pipe_fds[0]);
  int w;
  waitpid(pid, &w, 0);
  if (n <= 0)
    return false;
  buf[n] = '\0';
  for (ssize_t i = 0; i < n; i++) {
    if (buf[i] != '\n' && buf[i] != '\0')
      return true;
  }
  return false;
}

static void mpris_cleanup(void) {
  if (ctrl_pid > 0) {
    kill(ctrl_pid, SIGKILL);
    ctrl_pid = -1;
  }
}

static PlayerBackend mpris_backend = {
    .play_pause = mpris_play_pause,
    .next = mpris_next,
    .prev = mpris_prev,
    .seek = mpris_seek,
    .spawn_stream = mpris_spawn_stream,
    .has_player = mpris_has_player,
    .get_metadata = NULL,
    .get_status = NULL,
    .set_volume = NULL,
    .cleanup = mpris_cleanup,
};

PlayerBackend *mpris_backend_init(void) { return &mpris_backend; }
