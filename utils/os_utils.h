
#pragma once

#include <cstddef>

int set_process_limits(size_t coredump_filesz_limit = 30 * 1024,  // 30mb
                       size_t fd_limit = 250000);

int process_exist(const char* fmt, ...);

int fork_process_and_keepalive();