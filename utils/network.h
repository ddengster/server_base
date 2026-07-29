
#pragma once

#include <uv.h>


void set_loop(uv_loop_t* loop);

/**
 * Networking setups
 */
void tcp_server_setup(const char* ip_address, int port, int backlog_queue_sz = 128);
