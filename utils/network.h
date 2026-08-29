
#pragma once

#include "prereqs.h"
#include <uv.h>
#include <string>
#include <unordered_map>
#include "server_stats.h"
#include "coroutine_mgt.h"

#ifdef DEVELOPER_BUILD
#define NETWORK_DBG 1
#endif
// #define RESTRICT_POST_ONLY 1

#define SAFE_UV_CLOSE(stream, free_func)    \
  if (!uv_is_closing((uv_handle_t*)stream)) \
    uv_close((uv_handle_t*)stream, (uv_close_cb)free_func);

/**
 * TCP Server setups
 */

typedef void (*CallbackFunc)(uv_stream_t* client, const uv_buf_t* buf);
struct TCPServerSettings
{
  const char* mIPAddress = nullptr;
  int mPort = 0;
  int mBacklogQueueSz = SOMAXCONN;

  CallbackFunc mDataRecvCallback = nullptr;
};

// entry point for a server. userdata is expected to be type TCPServerSettings
void tcp_server_thread_func(void* userdata);
void tcp_server_setup(uv_loop_t* loop, TCPServerSettings* settings);

/**
 * Common functions
 */
// common 'end' callbacks
void common_write_end_cb(uv_write_t* req, int status);

enum JsonRpcResult
{
  kJsonRpcSuccess = 0,
  // JSON-RPC 2.0 error codes
  kJsonRpcParseError = -32700,
  kJsonRpcInvalidRequest = -32600,
  kJsonRpcMethodNotFound = -32601,
  kJsonRpcInvalidParams = -32602,
  kJsonRpcInternalError = -32603,
  kJsonRpcServerError = -32000,

  // specialized return for server to not do anything
  kJsonRpcInternalPending = -32100  // pending
};

typedef int (*PathCallbackFunc)(uv_stream_t* client);
struct HTTPServerSettings;
typedef void (*ThreadCallbackFunc)(HTTPServerSettings* server_settings, uv_loop_t* uvloop);

struct yyjson_val;
struct yyjson_mut_doc;
struct yyjson_mut_val;
typedef JsonRpcResult (*JsonRpcCallbackFunc)(uv_stream_t* client, int msgid, yyjson_val* params,
                                             int params_count, yyjson_mut_doc** doc,
                                             yyjson_mut_val** result);

/**** Standard responses you can use to send replies *****/
void send_jsonrpc_error(uv_stream_t* client, int code, const char* message, int* id);
void send_jsonrpc_response(uv_stream_t* client, int* id, yyjson_mut_doc* doc,
                           yyjson_mut_val* result);

/**
 * Http Server setups
 */
struct HTTPServerSettings
{
  const char* mIPAddress = nullptr;
  int mPort = 0;
  int mBacklogQueueSz = SOMAXCONN;
  char mJsonRpcPath[32] = "/";
  uint mJsonRpcPathHash = 0;

  void* mDBConnection = nullptr;
  uv_timer_t mDBCheckTimer;
  ThreadCallbackFunc mInitCallback = nullptr;
  ThreadCallbackFunc mShutdownCallback = nullptr;

  CoroutineManager mCoroutines;
  uv_timer_t mCoroutineTimer;
  static void CoroutineTimerCB(uv_timer_t* timer);

  std::unordered_map<uint, PathCallbackFunc> mPathCallbacks;
  std::unordered_map<uint, JsonRpcCallbackFunc> mRpcCallbacks;

  void ComputeJsonRpcPathHash() { mJsonRpcPathHash = Hash(mJsonRpcPath); }
};

// entry point for a server. userdata is expected to be type TCPServerSettings
void http_server_thread_func(void* userdata);