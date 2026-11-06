#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>
#include <time.h>

extern FILE *log_file;

#ifndef NDEBUG
    #define init_debug() do { log_file = fopen("yams.log", "a"); } while(0)
    #define close_debug() do { if(log_file) fclose(log_file); } while(0)
    #define DBG(...) \
        do { \
            if (log_file) { \
                time_t dbg_now = time(NULL); \
                struct tm *t = localtime(&dbg_now); \
                fprintf(log_file, "[%02d:%02d:%02d][DEBUG] ", t->tm_hour, t->tm_min, t->tm_sec); \
                fprintf(log_file, __VA_ARGS__); \
                fprintf(log_file, "\n"); \
                fflush(log_file); \
            } \
        } while (0)
#else
    #define init_debug() ((void)0)
    #define close_debug() ((void)0)
    #define DBG(...) ((void)0)
#endif

#endif
