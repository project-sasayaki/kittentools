#include <windows.h>
#include <process.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include "log.h"
#include "../config/config.h"


// Thread Local Storage for per-thread buffer (eliminates lock contention)
static DWORD g_logTlsIndex = TLS_OUT_OF_INDEXES;

// Minimum level to output logs, may be nice to replace it with a list in the future
// #define LOG_MIN_LEVEL 1

// Maximum queue size to prevent memory overflow
#define LOG_QUEUE_MAX_SIZE 1024

// Define a simple memory pool for log messages to reduce allocations
#define LOG_MESSAGE_POOL_SIZE 64
#define LOG_MESSAGE_MAX_LENGTH 1024

// Global variable to store the start time of the program
static struct timespec logStartTime = {0, 0};
static int g_logLevel = LOG_TRACE;

/* Optional sync log file: opened in logInit, every logDirectOutput writes a
 * plain-text (ANSI-stripped) copy + fflush so crashes never lose log tail. */
static FILE *g_logFile = NULL;


char logBasename[261] = {0};

// Initialize TLS on startup
static void logInitTls(void) {
    if (g_logTlsIndex == TLS_OUT_OF_INDEXES) {
        g_logTlsIndex = TlsAlloc();
    }
}

// Get per-thread output buffer
static char* logGetThreadBuffer(size_t* pSize) {
    *pSize = LOG_MESSAGE_MAX_LENGTH + 256;
    
    if (g_logTlsIndex == TLS_OUT_OF_INDEXES) {
        static char fallback[LOG_MESSAGE_MAX_LENGTH + 256];
        return fallback;
    }
    
    char* buffer = (char*)TlsGetValue(g_logTlsIndex);
    if (buffer == NULL) {
        buffer = (char*)malloc(LOG_MESSAGE_MAX_LENGTH + 256);
        if (buffer != NULL) {
            TlsSetValue(g_logTlsIndex, buffer);
        }
    }
    return buffer;
}

#define _LF(category, name, display)                                                                                                       \
    LOG_FACILITY lf##name = {                                                                                                              \
        .m_name = display,                                                                                                                 \
    };                                                                                                                                     \
    PLOG_FACILITY plf##name = &lf##name;
#include "log_facilities.def"
#undef _LF


void KittenSetLogBasename(char *baseName)
{
    strcpy_s(logBasename, sizeof logBasename, baseName);
}

static const char *log_style[] = {
    "\033[90m",     // TRACE
    "\x1b[36m",     // DEBUG : Cyan
    "\033[96m",     // GAME
    "\033[97m",     // INFO
    "\033[33m",     // WARN
    "\033[91m",     // ERROR
    "\x1b[35m",     // FATAL : Magenta
};

static const char *log_names[] = {"T", "D", "G", "I", "W", "E", "F"};

static int logFormatHeader(char* buffer, size_t bufferSize, PLOG_FACILITY facility, int level) {
    size_t offset = 0;
    
    if (level >= LOG_TRACE && level <= LOG_FATAL) {
        size_t styleLen = strlen(log_style[level]);
        if (offset + styleLen < bufferSize) {
            memcpy(buffer + offset, log_style[level], styleLen);
            offset += styleLen;
        }
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    offset += sprintf_s(buffer + offset, bufferSize - offset,
        "[%04d/%02d/%02d %02d:%02d:%02d] ",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    if (logBasename[0] && offset + strlen(logBasename) + 2 < bufferSize) {
        offset += sprintf(buffer + offset, "%s:", logBasename);
    }

    if (level >= LOG_TRACE && level <= LOG_FATAL && offset + 2 < bufferSize) {
        offset += sprintf(buffer + offset, "%s:", log_names[level]);
    }

    if (facility && facility->m_name && offset + strlen(facility->m_name) + 2 < bufferSize) {
        offset += sprintf(buffer + offset, "%s:", facility->m_name);
    }

    return (int)offset;
}

static void logWriteFileStripped(const char *buf, size_t len)
{
    if (!g_logFile)
        return;
    size_t i = 0;
    while (i < len)
    {
        if ((unsigned char)buf[i] == 0x1b && i + 1 < len && buf[i + 1] == '[')
        {
            i += 2;
            while (i < len)
            {
                char c = buf[i++];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                    break;
            }
        }
        else
        {
            fputc(buf[i++], g_logFile);
        }
    }
    fflush(g_logFile);
}

static void logDirectOutput(PLOG_FACILITY facility, int level, const char *message)
{
    size_t msgLen = strlen(message);
    int endsWithNewline = (msgLen > 0 && (message[msgLen - 1] == '\n' || message[msgLen - 1] == '\r'));
    
    // Use per-thread buffer for single write operation
    size_t bufferSize;
    char* outputBuffer = logGetThreadBuffer(&bufferSize);
    size_t offset = 0;
    
    // Format header directly into buffer
    offset += logFormatHeader(outputBuffer, bufferSize - offset, facility, level);
    
    // Copy message (with truncation safety)
    size_t copyLen = (offset + msgLen < bufferSize) ? msgLen : (bufferSize - offset - 8);
    memcpy(outputBuffer + offset, message, copyLen);
    offset += copyLen;
    
    // Add color reset and newline in single operation
    const char reset[] = "\x1b[0m";
    const char newline[] = "\n";
    
    if (offset + sizeof(reset) - 1 < bufferSize) {
        memcpy(outputBuffer + offset, reset, sizeof(reset) - 1);
        offset += sizeof(reset) - 1;
    }
    
    if (!endsWithNewline && offset + 1 < bufferSize) {
        memcpy(outputBuffer + offset, newline, 1);
        offset += 1;
    }
    
    // Windows: Direct WriteFile call for better performance
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdOut != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten;
        WriteFile(hStdOut, outputBuffer, (DWORD)offset, &bytesWritten, NULL);
    }
    /* Sync write a plain-text copy to kitten.log (flushed immediately). */
    logWriteFileStripped(outputBuffer, offset);
}

int logGeneric(int level, const char *file, int line, PLOG_FACILITY facility, const char *message, ...)
{
    (void)file;
    (void)line;

    // Init return value and sanity check inputs
    int ret = logSanityChecks(level, message);
    if (ret < 1)
    {
        // Something went wrong :(
        return ret;
    }

    // Use stack buffer as primary buffer
    char buffer[LOG_MESSAGE_MAX_LENGTH];
    va_list args;
    va_start(args, message);
    int size = vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, message, args);
    va_end(args);

    if (size < 0) {
        return -1;
    }
    
    // Only allocate for large messages (> 99% of cases use stack buffer)
    if (size >= (int)sizeof(buffer) - 1) {
        va_start(args, message);
        int requiredSize = _vscprintf(message, args);
        va_end(args);
        
        if (requiredSize > 0) {
            char* formattedMessage = (char*)malloc((size_t)requiredSize + 1);
            if (formattedMessage != NULL) {
                va_start(args, message);
                vsnprintf_s(formattedMessage, (size_t)requiredSize + 1, _TRUNCATE, message, args);
                va_end(args);
                logDirectOutput(facility, level, formattedMessage);
                free(formattedMessage);
                return requiredSize;
            }
        }
        // Fallthrough: use truncated stack buffer if allocation fails
    }

    // Direct output with stack buffer (zero-copy path for normal cases)
    logDirectOutput(facility, level, buffer);

    return size;
}

void logInit(void)
{
    // Initialize Thread Local Storage for per-thread buffers
    logInitTls();
    // Open a sync log file (overwritten each run) for easier post-mortem
    // analysis. ANSI color escapes are stripped on write so it stays plain text.
    fopen_s(&g_logFile, "kitten.log", "w");
}

void logShutdown(void)
{
    // Cleanup Thread Local Storage
    if (g_logTlsIndex != TLS_OUT_OF_INDEXES) {
        TlsFree(g_logTlsIndex);
        g_logTlsIndex = TLS_OUT_OF_INDEXES;
    }
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = NULL;
    }
}


int logSanityChecks(int level, const char *message)
{
    if (level < LOG_TRACE || level > LOG_FATAL)
    {
        log_warn(plfDebugLog, "Invalid level in log."); 
        return -1;                         
    }

    if (level < g_logLevel)
    {
        return 0; 
    }

    if (!message)
    {
        log_warn(plfDebugLog, "Called log with a NULL message."); 
        return -1;                                  
    }

    return 1;
}

void logInitTimer()
{
    clock_gettime(CLOCK_MONOTONIC, &logStartTime);
}

void logGetElapsedTime(long *seconds, long *milliseconds)
{
    struct timespec now;
    if (logStartTime.tv_sec == 0)
    {
        logInitTimer();
    }
    clock_gettime(CLOCK_MONOTONIC, &now);

    *seconds = now.tv_sec - logStartTime.tv_sec;
    *milliseconds = (now.tv_nsec - logStartTime.tv_nsec) / 1e6;

    if (*milliseconds < 0)
    {
        *milliseconds += 1000;
        (*seconds)--;
    }
}

void logSetMinLevel(int level)
{
    g_logLevel = level;
}