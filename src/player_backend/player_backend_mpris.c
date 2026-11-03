#include "player_backend/player_backend_mpris.h"
#include "config.h"
#include "debug.h"
#include "subprocess.h"
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static pid_t ctrl_pid = -1;

static void reap_ctrl(void) {
  if (ctrl_pid <= 0)
    return;

  int status;
  pid_t ret;
  do {
    ret = waitpid(ctrl_pid, &status, WNOHANG);
  } while (ret == -1 && errno == EINTR);

  if (ret == ctrl_pid) {
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
      DBG("playerctl control command exited with status %d",
          WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
      DBG("playerctl control command killed by signal %d", WTERMSIG(status));
    ctrl_pid = -1;
  } else if (ret == -1 && errno == ECHILD) {
    ctrl_pid = -1;
  }
}

static void run_cmd(const char *action, const char *param) {
  reap_ctrl();

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

  /* posix_spawn avoids the fork()/atfork overhead of a threaded process
   * (chafa/glib run worker threads here) and returns as soon as the child is
   * launched, so the keypress never waits for playerctl itself. */
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                   O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                   O_WRONLY, 0);

  pid_t pid = -1;
  int rc = posix_spawnp(&pid, "playerctl", &actions, NULL, args, environ);
  posix_spawn_file_actions_destroy(&actions);

  if (rc != 0) {
    DBG("run_cmd: posix_spawnp failed: %s", strerror(rc));
    return;
  }

  ctrl_pid = pid;
}

static void mpris_reap_child(pid_t pid) {
  if (pid == ctrl_pid)
    ctrl_pid = -1;
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
  char *const args[] = {"playerctl", "-l", NULL};
  CommandOutput out;

  /* On capture failure keep the stream alive rather than tearing it down. */
  if (!capture_command(args, &out))
    return true;

  if (out.exited && out.exit_status != 0)
    DBG("playerctl -l exited with status %d", out.exit_status);

  bool found = false;
  for (size_t i = 0; i < out.len; i++) {
    char c = out.data[i];
    if (c != '\n' && c != '\r' && c != '\0') {
      found = true;
      break;
    }
  }

  command_output_free(&out);
  return found;
}

static void mpris_cleanup(void) {
  if (ctrl_pid > 0) {
    int status;
    pid_t ret;
    do {
      ret = waitpid(ctrl_pid, &status, WNOHANG);
    } while (ret == -1 && errno == EINTR);
    if (ret == 0)
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
    .reap_child = mpris_reap_child,
    .cleanup = mpris_cleanup,
};

PlayerBackend *mpris_backend_init(void) { return &mpris_backend; }
