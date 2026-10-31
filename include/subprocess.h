#ifndef SUBPROCESS_H
#define SUBPROCESS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char *data;      // NUL-terminated stdout, or NULL when not captured
  size_t len;      // length of stdout, excluding the terminator
  int exit_status; // exit code, or -1 when the child was not reaped
  bool exited;     // true when waitpid() returned the child status
} CommandOutput;

/* Run argv with stdout captured and stderr discarded. Returns false only when
 * the subprocess could not be started or its output could not be buffered. */
bool capture_command(char *const argv[], CommandOutput *out);
void command_output_free(CommandOutput *out);

#endif
