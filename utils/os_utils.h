
#pragma once

#include "prereqs.h"
#include <cstddef>

int set_process_limits(size_t coredump_filesz_limit = 30 * 1024,  // 30mb
                       size_t fd_limit = 250000);

int process_exist(const char* fmt, ...);

int fork_process_and_keepalive();

/// Process title manipulation, makes target is better
/// allows you to do `ps aux` (which otherwise shows up as `?`) or
/// `killall -9 <processname>_worker_..
void process_title_init(int argc, char* argv[]);
void process_title_set(const char* fmt, ...);
