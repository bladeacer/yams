#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>
#include <time.h>

extern FILE *log_file;

#define DBG(...) \
    do { \
        if (log_file) { \
            time_t now = time(NULL); \
            struct tm *t = localtime(&now); \
            fprintf(log_file, "[%02d:%02d:%02d][DEBUG] ", t->tm_hour, t->tm_min, t->tm_sec); \
            fprintf(log_file, __VA_ARGS__); \
            fprintf(log_file, "\n"); \
            fflush(log_file); \
        } \
    } while (0)

#endif
