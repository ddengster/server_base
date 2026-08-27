
#include "logger.h"
#include <time.h>
#include <cstring>
#include <unistd.h>
#include <cstdlib>
#include <pthread.h>
#include <execinfo.h>
#include <filesystem>

#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

static LogLevel gLogMinLevel = LOG_DEFAULT_LEVEL;
static FILE* gLogFile = nullptr;
static int gFlushCacheSz = 8 * 1024;
static int gPid = 0;
static char gLastLogTiming[32] = {};
static int gLogCountLimit = 30;
static const char* gLogDir = "logs";

static const char* gLogLevelNames[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static bool gLogAsync = false;
static std::vector<char*> gAsyncLogBuffer;
static std::mutex gAsyncMutex;
static std::condition_variable gAsyncCond;
static std::atomic<bool> gAsyncExit{false};
static constexpr size_t gAsyncBufferLimit = 120;
static std::thread gAsyncThread;

static char* gFileBuf = nullptr;
static int gFilePos = 0;
static pthread_mutex_t gLogMutex = PTHREAD_MUTEX_INITIALIZER;

void log_flush();
void check_new_log();

#if defined(_WIN32) || defined(_WIN64)
#define lock_file(fp)   _lock_file(fp)
#define unlock_file(fp) _unlock_file(fp)
#else
#define lock_file(fp)   flockfile(fp)
#define unlock_file(fp) funlockfile(fp)
#endif

void delete_logs_beyond(int days_ago);

FILE* fopen_log_file_tdy()
{
  delete_logs_beyond(gLogCountLimit);

  time_t now = time(NULL);
  struct tm utc_tm;
  gmtime_r(&now, &utc_tm);

  strftime(gLastLogTiming, sizeof(gLastLogTiming), "%Y%m%d_%A", &utc_tm);

  char filenamebuf[128] = {};
  snprintf(filenamebuf, sizeof(filenamebuf), "%s/log_%s.txt", gLogDir, gLastLogTiming);

  FILE* f = fopen(filenamebuf, "ab+");
  return f;
}

void log_set_file(FILE* log_file)
{ gLogFile = log_file; }

void log_init(const char* log_dir, LogLevel level, int flush_cache_sz, int log_limit)
{
  gLogMinLevel = level;
  gFlushCacheSz = flush_cache_sz;
  gPid = getpid();
  gLogCountLimit = log_limit;

  gLogDir = log_dir;
  if (!std::filesystem::exists(gLogDir))
    std::filesystem::create_directory(gLogDir);
  gLogFile = fopen_log_file_tdy();

  free(gFileBuf);
  gFileBuf = (char*)malloc(gFlushCacheSz);
  gFilePos = 0;
  atexit(log_shutdown);
  atexit(log_flush);
}

void log_shutdown()
{
  if (gLogAsync)
  {
    if (!gAsyncExit.exchange(true))
      gAsyncCond.notify_all();
    if (gAsyncThread.joinable())
      gAsyncThread.join();
    if (gFilePos > 0 && gLogFile)
      log_flush();
  }
  free(gFileBuf);
  gFileBuf = nullptr;
  if (gLogFile)
    fclose(gLogFile);
}

void log_childprocess_init()
{ gPid = getpid(); }

static void log_async_thread(void* userdata)
{
  auto last = std::chrono::steady_clock::now();
  while (true)
  {
    std::vector<char*> local;
    {
      std::unique_lock<std::mutex> lk(gAsyncMutex);
      gAsyncCond.wait_for(
        lk, std::chrono::seconds(3),
        [] { return gAsyncExit.load() || gAsyncLogBuffer.size() >= gAsyncBufferLimit; });
      if (gAsyncExit.load() && gAsyncLogBuffer.empty())
        break;
      auto now = std::chrono::steady_clock::now();
      bool timeout = now - last >= std::chrono::seconds(3);
      if (gAsyncLogBuffer.empty() && !timeout && !gAsyncExit.load())
        continue;
      local.swap(gAsyncLogBuffer);
    }
    if (local.empty())
    {
      if (gAsyncExit.load())
        break;
      continue;
    }
#ifdef LOG_TO_CONSOLE
    for (char* s : local)
      fputs(s, stdout);
#endif
    check_new_log();
    for (char* s : local)
    {
      size_t len = strlen(s);
      if (!gLogFile || len == 0)
        continue;
      if (gFilePos + (int)len >= gFlushCacheSz)
      {
        if (gFilePos > 0)
          log_flush();
        if ((int)len >= gFlushCacheSz)
        {
          lock_file(gLogFile);
          fwrite(s, 1, len, gLogFile);
          fflush(gLogFile);
          unlock_file(gLogFile);
        }
        else
        {
          memcpy(gFileBuf + gFilePos, s, len);
          gFilePos += (int)len;
        }
      }
      else
      {
        memcpy(gFileBuf + gFilePos, s, len);
        gFilePos += (int)len;
      }
    }
    log_flush();
    for (char* s : local)
      delete[] s;
    last = std::chrono::steady_clock::now();
  }
}

void log_set_async()
{
  if (gLogAsync)
    return;
  gLogAsync = true;
  gAsyncExit.store(false);
  gAsyncThread = std::thread(log_async_thread, nullptr);
}

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

void check_new_log()
{
  time_t now = time(NULL);
  struct tm utc_tm;
  gmtime_r(&now, &utc_tm);

  char tbuf[32] = {};
  strftime(tbuf, sizeof(tbuf), "%Y%m%d_%A", &utc_tm);

  // new day/crossover log, make new one and delete the others
  if (strcmp(gLastLogTiming, tbuf) != 0)
  {
    log_flush();
    if (gLogFile)
      fclose(gLogFile);

    snprintf(gLastLogTiming, sizeof(gLastLogTiming), "%s", tbuf);

    char filenamebuf[128] = {};
    snprintf(filenamebuf, sizeof(filenamebuf), "%s/log_%s.txt", gLogDir, gLastLogTiming);

    gLogFile = fopen(filenamebuf, "ab+");

    delete_logs_beyond(gLogCountLimit);
  }
}

void log_log(LogLevel level, const char* filelog, const char* func, int line, const char* fmt, ...)
{
  if (level < gLogMinLevel)
    return;

  if (!gLogAsync)
    check_new_log();

  time_t t = time(NULL);
  struct tm utc_tm;
  gmtime_r(&t, &utc_tm);
  char timebuf[10] = {};
  strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &utc_tm);

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

  char file_line[8192] = {};
  int file_len =
    snprintf(file_line, sizeof(file_line), "[%s] [pid:%d] [%s:%d, %s()] [%-5s] %s\n", timebuf, gPid,
             filelog_shortened, line, func, gLogLevelNames[level], msgbuf);

  if (gLogAsync)
  {
    int sz = file_len + 1;
    char* buf = new char[sz];
    memcpy(buf, file_line, sz);
    {
      std::lock_guard<std::mutex> lk(gAsyncMutex);
      gAsyncLogBuffer.push_back(buf);
    }
    gAsyncCond.notify_one();
    return;
  }

  // print to console
#ifdef LOG_TO_CONSOLE
  FILE* stream = stdout;
  if (level == LOG_WARN || level == LOG_ERROR || level == LOG_FATAL)
    stream = stderr;
  fprintf(stream, "%s%s%s\n", COLOR_TIME, file_line, COLOR_RESET);
#endif

  pthread_mutex_lock(&gLogMutex);

  FILE* file = gLogFile;
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
  {
    if (symbols)
      free(symbols);
    return;
  }

  LOG_WARN("==========backtrace=start==========");
  for (i = 1, j = 0; i < size; ++i, ++j)
  {
    LOG_WARN("%2d %s", j, symbols[i]);
  }
  LOG_WARN("===========backtrace=end===========");
  free(symbols);
}

void delete_logs_beyond(int days_ago)
{
  // delete logs older than 30 days to 30*2 days, perf limit
  int iterations = days_ago * 2;
  time_t t = time(NULL) - (86400 * days_ago);
  struct tm del_tm;
  char del_name[128] = {};
  for (int i = 0; i < iterations; i++, t -= 86400)
  {
    gmtime_r(&t, &del_tm);
    strftime(del_name, sizeof(del_name), "log_%Y%m%d_%A.txt", &del_tm);

    char ffilename[256] = {};
    snprintf(ffilename, sizeof(ffilename), "%s/%s", gLogDir, del_name);
    remove(ffilename);
  }
}
