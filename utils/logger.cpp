
#include "logger.h"
#include <time.h>
#include <cstring>
#include <unistd.h>
#include <cstdlib>
#include <pthread.h>
#include <execinfo.h>

static LogLevel gLogMinLevel = LOG_DEFAULT_LEVEL;
static FILE* gLogFile = nullptr;
static int gFlushCacheSz = 1;
static int gFlushIntervalHours = 1;
static int gPid = 0;

static const char* gLogLevelNames[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static char* gFileBuf = nullptr;
static int gFilePos = 0;
static pthread_mutex_t gLogMutex = PTHREAD_MUTEX_INITIALIZER;

#if defined(_WIN32) || defined(_WIN64)
#define lock_file(fp)   _lock_file(fp)
#define unlock_file(fp) _unlock_file(fp)
#else
#define lock_file(fp)   flockfile(fp)
#define unlock_file(fp) funlockfile(fp)
#endif

FILE* fopen_log_file_tdy()
{
  time_t now = time(NULL);
  struct tm* utc_tm = gmtime(&now);

  char tbuf[32] = {};
  strftime(tbuf, sizeof(tbuf), "%Y%m%d_%A", utc_tm);

  char buf[128] = {};
  snprintf(buf, sizeof(buf), "log_%s.txt", tbuf);

  FILE* f = fopen(buf, "ab+");
  return f;
}

void log_set_file(FILE* log_file)
{ gLogFile = log_file; }

void log_init(FILE* log_file, LogLevel level, int flush_cache_sz, int flush_interval_hours)
{
  gLogMinLevel = level;
  gLogFile = log_file;
  gFlushCacheSz = flush_cache_sz;
  gFlushIntervalHours = flush_interval_hours;
  gPid = getpid();

  free(gFileBuf);
  gFileBuf = (char*)malloc(gFlushCacheSz);
  gFilePos = 0;
  atexit(log_shutdown);
  atexit(log_flush);
}

void log_shutdown()
{
  free(gFileBuf);

  gFileBuf = nullptr;
}

void log_childprocess_init()
{ gPid = getpid(); }

void log_flush()
{
  if (gFilePos > 0 && gLogFile)
  {
    lock_file(gLogFile);
    fwrite(gFileBuf, 1, gFilePos, gLogFile);
#if LOG_AUTO_FLUSH
    fflush(gLogFile);
#endif
    unlock_file(gLogFile);
    gFilePos = 0;
  }
}

void log_log(LogLevel level, const char* filelog, const char* func, int line, const char* fmt, ...)
{
  if (level < gLogMinLevel)
    return;

  FILE* file = gLogFile;

  time_t t = time(NULL);
  struct tm* lt = localtime(&t);
  char timebuf[10] = {};
  strftime(timebuf, sizeof(timebuf), "%H:%M:%S", lt);

  char msgbuf[4096] = {};
  va_list args;
  va_start(args, fmt);
  vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);
  va_end(args);

  const char* color = COLOR_RESET;
  switch (level)
  {
    case LOG_TRACE: color = COLOR_TRACE; break;
    case LOG_DEBUG: color = COLOR_DEBUG; break;
    case LOG_INFO: color = COLOR_INFO; break;
    case LOG_WARN: color = COLOR_WARN; break;
    case LOG_ERROR: color = COLOR_ERROR; break;
    case LOG_FATAL: color = COLOR_FATAL; break;
  }

  // cut filename to 15 characters
  int len = strlen(filelog);
  int max_char_count = 15;
  const char* filelog_shortened = len > max_char_count ? filelog + (len - max_char_count) : filelog;

  // print to console
#ifdef LOG_TO_CONSOLE
  fprintf(stdout, "%s[%s] [pid:%d] %s[%s:%d, %s()] %s[%-5s] %s%s\n", COLOR_TIME, timebuf, gPid,
          color, filelog_shortened, line, func, color, gLogLevelNames[level], msgbuf, COLOR_RESET);
#endif

  char file_line[8192] = {};
  int file_len =
    snprintf(file_line, sizeof(file_line), "[%s] [pid:%d] [%s:%d, %s()] [%-5s] %s\n", timebuf, gPid,
             filelog_shortened, line, func, gLogLevelNames[level], msgbuf);

  pthread_mutex_lock(&gLogMutex);

  if (file && file_len > 0)
  {
    int final_sz = gFilePos + file_len;
    if (final_sz >= gFlushCacheSz)
      log_flush();
    else
    {
      memcpy(gFileBuf + gFilePos, file_line, file_len);
      gFilePos += file_len;
    }
  }

#if LOG_AUTO_FLUSH
  log_flush();
#endif

  pthread_mutex_unlock(&gLogMutex);
}

void log_backtrace()
{
  void* stack[64] = {};
  char** symbols = nullptr;
  int size, i, j;

  size = backtrace(stack, 64);
  symbols = backtrace_symbols(stack, size);
  if (symbols == NULL)
    return;
  if (size == 1)
    return;

  LOG_WARN("==========backtrace=start==========");
  for (i = 1, j = 0; i < size; ++i, ++j)
  {
    LOG_WARN("%2d %s", j, symbols[i]);
  }
  LOG_WARN("===========backtrace=end===========");
  free(symbols);
}
