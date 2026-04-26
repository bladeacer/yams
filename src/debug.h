#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

/**
 * DBG Macro:
 * Wraps multiple fprintf calls in a do-while(0) block to ensure:
 * 1. It acts as a single statement (safe for if/else without braces).
 * 2. It requires a semicolon at the end.
 * 3. It has zero overhead when optimized.
 */
#define DBG(...) \
    do { \
        fprintf(stderr, "[YAMS_DEBUG] " __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } while (0)

#endif // DEBUG_H
