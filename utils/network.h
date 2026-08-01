
#pragma once

#include <uv.h>
#include <string>
#include <unordered_map>

#define NETWORK_DBG 1
// #define RESTRICT_POST_ONLY 1
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


struct yyjson_val;
struct yyjson_mut_doc;
struct yyjson_mut_val;
typedef JsonRpcResult (*JsonRpcCallbackFunc)(uv_stream_t* client, int msgid, yyjson_val* params,
                                             int params_count, yyjson_mut_doc* doc,
                                             yyjson_mut_val* result);

inline uint32_t Hash(const char* str)
{
  uint32_t hash = 5381;
  int c;
  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;  // hash * 33 + c
  return hash;
}

void send_jsonrpc_error(uv_stream_t* client, int code, const char* message, int* id);
void send_jsonrpc_response(uv_stream_t* client, int* id, yyjson_mut_doc* doc,
                           yyjson_mut_val* result);

struct HTTPServerSettings
{
  const char* mIPAddress = nullptr;
  int mPort = 0;
  int mBacklogQueueSz = SOMAXCONN;
  char mPath[32] = "/";

  std::unordered_map<uint32_t, JsonRpcCallbackFunc> mRpcCallbacks;
};

// entry point for a server. userdata is expected to be type TCPServerSettings
void http_server_thread_func(void* userdata);