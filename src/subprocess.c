#include "subprocess.h"
#include "debug.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CAPTURE_INITIAL 4096

static void output_reset(CommandOutput *out) {
  out->data = NULL;
  out->len = 0;
  out->exit_status = -1;
  out->exited = false;
}

bool capture_command(char *const argv[], CommandOutput *out) {
  output_reset(out);
  if (!argv || !argv[0])
    return false;

  int pipe_fds[2];
  if (pipe(pipe_fds) == -1) {
    DBG("capture_command: pipe failed: %s", strerror(errno));
    return false;
  }

  pid_t pid = fork();
  if (pid == -1) {
    DBG("capture_command: fork failed: %s", strerror(errno));
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return false;
  }

  if (pid == 0) {
    close(pipe_fds[0]);
    if (dup2(pipe_fds[1], STDOUT_FILENO) == -1)
      _exit(127);
    if (pipe_fds[1] != STDOUT_FILENO)
      close(pipe_fds[1]);

    int devnull = open("/dev/null", O_WRONLY);
    if (devnull == -1)
      _exit(127);
    if (dup2(devnull, STDERR_FILENO) == -1)
      _exit(127);
    close(devnull);

    execvp(argv[0], argv);
    _exit(127);
  }

  close(pipe_fds[1]);

  size_t cap = CAPTURE_INITIAL;
  size_t len = 0;
  char *buf = malloc(cap);
  if (!buf) {
    close(pipe_fds[0]);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    return false;
  }

  while (true) {
    if (len + 1 >= cap) {
      size_t new_cap = cap * 2;
      char *tmp = realloc(buf, new_cap);
      if (!tmp) {
        DBG("capture_command: realloc failed");
        free(buf);
        close(pipe_fds[0]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        output_reset(out);
        return false;
      }
      buf = tmp;
      cap = new_cap;
    }

    ssize_t n = read(pipe_fds[0], buf + len, cap - len - 1);
    if (n > 0) {
      len += (size_t)n;
      continue;
    }
    if (n == 0)
      break;
    if (errno == EINTR)
      continue;

    DBG("capture_command: read failed: %s", strerror(errno));
    break;
  }

  close(pipe_fds[0]);
  buf[len] = '\0';

  int status = 0;
  pid_t reaped;
  do {
    reaped = waitpid(pid, &status, 0);
  } while (reaped == -1 && errno == EINTR);

  if (reaped == pid) {
    out->exited = true;
    if (WIFEXITED(status)) {
      out->exit_status = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      DBG("capture_command: child %d killed by signal %d", (int)pid,
          WTERMSIG(status));
    }
  } else {
    DBG("capture_command: waitpid failed: %s", strerror(errno));
  }

  out->data = buf;
  out->len = len;
  return true;
}

void command_output_free(CommandOutput *out) {
  if (!out)
    return;
  free(out->data);
  output_reset(out);
}
