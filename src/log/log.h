#pragma once


#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>

typedef struct
{
    char *m_name;
} LOG_FACILITY, *PLOG_FACILITY;

#define _LF(category, name, display) extern PLOG_FACILITY plf##name;
#include ".\log_facilities.def"
#undef _LF

// List of status
enum { LOG_TRACE, // 0
       LOG_DEBUG, // 1
       LOG_GAME,  // 2
       LOG_INFO,  // 3
       LOG_WARN,  // 4
       LOG_ERROR, // 5
       LOG_FATAL  // 6
};

// Macro to ease logging
#define log_game(facility, ...)  logGeneric(LOG_GAME,  __FILE__, __LINE__, facility, __VA_ARGS__)
#define log_trace(facility, ...) logGeneric(LOG_TRACE, __FILE__, __LINE__, facility, __VA_ARGS__)
#define log_debug(facility, ...) logGeneric(LOG_DEBUG, __FILE__, __LINE__, facility, __VA_ARGS__)
#define log_info(facility, ...)  logGeneric(LOG_INFO,  __FILE__, __LINE__, facility, __VA_ARGS__)
#define log_warn(facility, ...)  logGeneric(LOG_WARN,  __FILE__, __LINE__, facility, __VA_ARGS__)
#define log_error(facility, ...) logGeneric(LOG_ERROR, __FILE__, __LINE__, facility, __VA_ARGS__)
#define log_fatal(facility, ...) logGeneric(LOG_FATAL, __FILE__, __LINE__, facility, __VA_ARGS__)



// Thread-safe logging functions
int logGeneric(int level, const char *file, int line, PLOG_FACILITY facility, const char *message, ...);

// Logging system initialization and cleanup
void logInit();
void logShutdown();

// Internal helper functions
int logSanityChecks(int level, const char *message);
void logInitTimer();
void logGetElapsedTime(long *seconds, long *milliseconds);
void logSetMinLevel(int level);
void KittenSetLogBasename(char *baseName);
