
#pragma once

#include <uv.h>

#define NETWORK_DBG 1
/**
 * Networking setups
 */
typedef void (*CallbackFunc)(uv_stream_t* client, const uv_buf_t* buf);

struct TCPServerSettings
{
  const char* mIPAddress = nullptr;
  int mPort = 0;
  int mBacklogQueueSz = SOMAXCONN;

  CallbackFunc mDataRecvCallback = nullptr;
};

void tcp_server_setup(uv_loop_t* loop, TCPServerSettings* settings);

// entry point for a server. userdata is expected to be type TCPServerSettings
void server_thread_func(void* userdata);

// common 'end' callbacks
void common_write_end_cb(uv_write_t* req, int status);

struct HTTPServerSettings
{
  const char* mIPAddress = nullptr;
  int mPort = 0;
  int mBacklogQueueSz = SOMAXCONN;
};

// entry point for a server. userdata is expected to be type TCPServerSettings
void http_server_thread_func(void* userdata);