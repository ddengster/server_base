/*
 * C/C++ Logger for server multiprocesses
 *
 * Modifications for server logging:
 * - log name mgt & deletion
 * - periodic batch flushing, and on close/crash/timeinterval&swap
 * - routine flush function
 * - @future: remote logging?
 */
#pragma once

#include "prereqs.h"
#include <stdarg.h>
#include <stdio.h>

// macros, enable for development needs
#ifdef DEVELOPER_BUILD
#define LOG_TO_CONSOLE 1  // logging to console
#define LOG_AUTO_FLUSH 1  // auto flush after every log, disable for perf
#endif

typedef enum
{
  LOG_TRACE,
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR,
  LOG_FATAL
} LogLevel;

#ifndef LOG_DEFAULT_LEVEL
#define LOG_DEFAULT_LEVEL LOG_TRACE
#endif

/* Log Management/Core API */
FILE* fopen_log_file_tdy();
void log_init(FILE* log_file, LogLevel level = LOG_DEFAULT_LEVEL, int flush_cache_sz = 16 * 1024,
              int flush_interval_hours = 1);
void log_childprocess_init();
void log_flush();
void log_backtrace();
void log_shutdown();

void log_log(LogLevel level, const char* filelog, const char* func, int line, const char* fmt, ...);

/* Convenience macros */
#define LOG_TRACE(...) log_log(LOG_TRACE, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) log_log(LOG_DEBUG, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  log_log(LOG_INFO, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_log(LOG_WARN, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_log(LOG_ERROR, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) log_log(LOG_FATAL, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG(...)       LOG_INFO(__VA_ARGS__)

/* ANSI color codes */
#define COLOR_RESET "\x1b[0m"
#define COLOR_TIME  "\x1b[90m"
#define COLOR_TRACE "\x1b[90m"     // gray
#define COLOR_DEBUG "\x1b[36m"     // cyan
#define COLOR_INFO  "\x1b[32m"     // green
#define COLOR_WARN  "\x1b[33m"     // yellow
#define COLOR_ERROR "\x1b[31m"     // red
#define COLOR_FATAL "\x1b[37;41m"  // bold red
