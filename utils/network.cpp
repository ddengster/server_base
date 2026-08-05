
#include "network.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#include "picohttpparser.h"
#include "yyjson.h"

// prompt 'How glibc malloc Routes malloc', 1032bytes or 80-160byte
#define MAX_PACKET_SIZE 1 * 1024

void on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  (void)handle;
  (void)suggested_size;
  // uv_handle_get_loop(handle);
  //  ignore packet sizes, give 1k
  buf->base = (char*)malloc(MAX_PACKET_SIZE);
  buf->len = MAX_PACKET_SIZE;
}

void tcp_on_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf)
{
  if (nread > 0)
  {
#ifdef NETWORK_DBG
    uv_thread_t th = uv_thread_self();
    LOG("threadid: %ld, Received %d bytes: %.*s\n", th, (int)nread, nread, buf->base);
#endif
    auto settings = (TCPServerSettings*)client->data;
    if (settings->mDataRecvCallback)
      settings->mDataRecvCallback(client, buf);
  }
  else if (nread < 0)
  {
    if (nread != UV_EOF)
      LOG_WARN("Read error %s\n", uv_strerror(nread));

    uv_close((uv_handle_t*)client, (uv_close_cb)free);
  }
  if (buf->base)
    free(buf->base);
}

void tcp_on_new_connection(uv_stream_t* server, int status)
{
  if (status < 0)
  {
    LOG_WARN("New connection error %s\n", uv_strerror(status));
    return;
  }

  uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
  uv_tcp_init(server->loop, client);
  client->data = server->data;

  if (uv_accept(server, (uv_stream_t*)client) == 0)
  {
    // non blocking
    uv_read_start((uv_stream_t*)client, on_alloc, tcp_on_read);
  }
  else
    uv_close((uv_handle_t*)client, (uv_close_cb)free);
}

void tcp_server_setup(uv_loop_t* loop, TCPServerSettings* settings)
{
  uv_tcp_t* server = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
  uv_tcp_init(loop, server);
  uv_tcp_simultaneous_accepts(server, 1);
  uv_tcp_nodelay(server, 1);
  server->data = settings;

  struct sockaddr_in addr;
  uv_ip4_addr(settings->mIPAddress, settings->mPort, &addr);

  uv_tcp_bind(server, (const struct sockaddr*)&addr, SO_REUSEPORT);

  int r = uv_listen((uv_stream_t*)server, settings->mBacklogQueueSz, tcp_on_new_connection);
  if (r)
  {
    LOG_WARN("Listen error %s\n", uv_strerror(r));
    return;
  }
}

void server_thread_func(void* userdata)
{
  auto server_settings = (TCPServerSettings*)userdata;

  uv_loop_t loop;
  uv_loop_init(&loop);

  uv_tcp_t* server = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
  uv_tcp_init(&loop, server);
  uv_tcp_simultaneous_accepts(server, 1);
  uv_tcp_nodelay(server, 1);
  server->data = server_settings;

  struct sockaddr_in addr;
  uv_ip4_addr(server_settings->mIPAddress, server_settings->mPort, &addr);

#ifdef NETWORK_DBG
  uv_tcp_bind(server, (const struct sockaddr*)&addr, 0);
#else
  uv_tcp_bind(server, (const struct sockaddr*)&addr, SO_REUSEPORT);
#endif

  int r = uv_listen((uv_stream_t*)server, server_settings->mBacklogQueueSz, tcp_on_new_connection);
  if (r)
  {
    LOG_WARN("Listen error %s\n", uv_strerror(r));
    return;
  }

  uv_run(&loop, UV_RUN_DEFAULT);

  uv_loop_close(&loop);
}

void common_write_end_cb(uv_write_t* req, int status)
{
  if (status < 0)
    LOG_WARN("Write error: %s\n", uv_strerror(status));

  // Free the request memory allocated in the write call
  free(req);
}

void send_jsonrpc_error(uv_stream_t* client, int code, const char* message, int* id)
{
  //@note: prompt "json rpc errors" for error codes
  yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);

  yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");
  yyjson_mut_val* err = yyjson_mut_obj_add_obj(doc, root, "error");
  yyjson_mut_obj_add_int(doc, err, "code", code);
  yyjson_mut_obj_add_str(doc, err, "message", message);
  if (id == nullptr)
    yyjson_mut_obj_add_val(doc, root, "id", yyjson_mut_null(doc));
  else
    yyjson_mut_obj_add_int(doc, root, "id", *id);

  size_t json_len = 0;
  char* json = yyjson_mut_write(doc, 0, &json_len);
  yyjson_mut_doc_free(doc);

  char buf[512] = {};
  int len = snprintf(buf, sizeof(buf),
                     "HTTP/1.1 400 Bad Request\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s\r\n",
                     json_len, json);
  free(json);

  uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
  req->data = client;

  char* b = (char*)malloc(len + 1);
  strncpy(b, buf, (size_t)len + 1);

  uv_buf_t sendbuf = uv_buf_init(b, len + 1);
  int result = uv_write(req, client, &sendbuf, 1, common_write_end_cb);
  if (result < 0)
  {
    LOG_WARN("Failed to initiate write: %s\n", uv_strerror(result));
    free(req);
  }
}

void send_jsonrpc_response(uv_stream_t* client, int* id, yyjson_mut_doc* doc,
                           yyjson_mut_val* result)
{
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);

  yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");
  yyjson_mut_obj_add_val(doc, root, "result", result);
  if (id == nullptr)
    yyjson_mut_obj_add_val(doc, root, "id", yyjson_mut_null(doc));
  else
    yyjson_mut_obj_add_int(doc, root, "id", *id);

  size_t json_len = 0;
  char* json = yyjson_mut_write(doc, 0, &json_len);

  char buf[1024] = {};
  int len = snprintf(buf, sizeof(buf),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s\r\n",
                     json_len, json);
  free(json);

  uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
  req->data = client;

  char* b = (char*)malloc((size_t)len + 1);
  strncpy(b, buf, (size_t)len + 1);

  uv_buf_t sendbuf = uv_buf_init(b, (size_t)len + 1);
  int result_code = uv_write(req, client, &sendbuf, 1, common_write_end_cb);
  if (result_code < 0)
  {
    LOG_WARN("Failed to initiate write: %s\n", uv_strerror(result_code));
    free(req);
  }
}

static void handle_jsonrpc_request(uv_stream_t* client, const char* body, size_t body_len,
                                   HTTPServerSettings* settings)
{
  yyjson_doc* doc = yyjson_read(body, body_len, 0);
  if (doc == NULL)
  {
    LOG_WARN("JSON-RPC: parse error\n");
    send_jsonrpc_error(client, kJsonRpcParseError, "Parse error", nullptr);
    uv_close((uv_handle_t*)client, (uv_close_cb)free);
    return;
  }

  yyjson_val* root = yyjson_doc_get_root(doc);
  yyjson_val* jsonrpc = yyjson_obj_get(root, "jsonrpc");
  yyjson_val* method = yyjson_obj_get(root, "method");
  yyjson_val* id = yyjson_obj_get(root, "id");
  int id_num = yyjson_get_num(id);  // 0 if null
  int* msg_id = yyjson_is_null(id) ? nullptr : &id_num;

  bool valid = yyjson_is_obj(root) && yyjson_is_str(jsonrpc) &&
               strcmp(yyjson_get_str(jsonrpc), "2.0") == 0 && yyjson_is_str(method) &&
               yyjson_get_len(method) > 0 && (id == NULL || !yyjson_is_null(id));

  if (!valid)
  {
    LOG_WARN("JSON-RPC: invalid request\n");
    send_jsonrpc_error(client, kJsonRpcParseError, "Parse Error", msg_id);
    uv_close((uv_handle_t*)client, (uv_close_cb)free);
    yyjson_doc_free(doc);
    return;
  }

  yyjson_val* params = yyjson_obj_get(root, "params");
  uint params_count = yyjson_arr_size(params);
  size_t params_len = 0;
  char* params_str = yyjson_val_write(params, 0, &params_len);

#ifdef NETWORK_DBG
  LOG("JSON-RPC: method: %s, params: %s\n", yyjson_get_str(method),
      params_str ? params_str : "null");
#endif

  auto itr = settings->mRpcCallbacks.find(Hash(yyjson_get_str(method)));
  if (itr != settings->mRpcCallbacks.end())
  {
    JsonRpcCallbackFunc cb = (JsonRpcCallbackFunc)itr->second;
    if (cb)
    {
      yyjson_mut_doc* result_doc = nullptr;
      yyjson_mut_val* result = nullptr;

      JsonRpcResult ret = cb(client, id_num, params, params_count, &result_doc, &result);
      if (ret == kJsonRpcSuccess)
      {
        send_jsonrpc_response(client, msg_id, result_doc, result);
        yyjson_mut_doc_free(result_doc);
      }
      else if (ret == kJsonRpcInvalidParams)
      {
        char errmsg[64] = {};
        snprintf(errmsg, sizeof(errmsg), "Invalid params");
        send_jsonrpc_error(client, kJsonRpcInvalidParams, errmsg, msg_id);
      }
      else if (ret == kJsonRpcInternalError)
        send_jsonrpc_error(client, kJsonRpcServerError, "Internal error", msg_id);
      else if (ret == kJsonRpcInternalPending)
      {
#ifdef NETWORK_DBG
        LOG("Async job, letting it handle sends..");
#endif
      }
      else
        send_jsonrpc_error(client, ret, "Internal error", msg_id);
    }
  }
  else
  {
    send_jsonrpc_error(client, kJsonRpcMethodNotFound, "Method not found", msg_id);
  }

  free(params_str);
  yyjson_doc_free(doc);
}

void http_on_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf)
{
  if (nread > 0)
  {
#ifdef NETWORK_DBG
    uv_thread_t th = uv_thread_self();
    LOG("threadid: %ld, Received %d bytes: %.*s\n", th, (int)nread, nread, buf->base);
#endif
    // parse http request
    const char* method = nullptr;
    const char* path = nullptr;
    size_t method_len = 0, path_len = 0;
    int minor_version = 0;

    static const size_t max_headers = 64;
    phr_header headers[max_headers] = {};
    size_t num_headers = max_headers;
    size_t prev_buf_len = 0;

    int pret = phr_parse_request(buf->base, buf->len, &method, &method_len, &path, &path_len,
                                 &minor_version, headers, &num_headers, prev_buf_len);
    if (pret > 0)  // success
    {
      LOG("Parsed: method: %.*s, path: %.*s, ", method_len, method, path_len, path);

      auto settings = (HTTPServerSettings*)client->data;

      char pathbuf[64] = {};
      strncpy(pathbuf, path, path_len);
      uint req_path_hash = Hash(pathbuf);

      auto path_itr = settings->mPathCallbacks.find(req_path_hash);
      if (path_itr != settings->mPathCallbacks.end())
      {
        PathCallbackFunc cb = path_itr->second;
        if (cb)
        {
          cb(client);
          uv_close((uv_handle_t*)client, (uv_close_cb)free);
        }
      }
      else
      {
#ifdef RESTRICT_POST_ONLY
        if (strncmp(method, "POST", method_len) != 0)
        {
          send_jsonrpc_error(client, kJsonRpcMethodNotFound, "Incorrect HTTP method (use POST)",
                             nullptr);
          uv_close((uv_handle_t*)client, (uv_close_cb)free);
        }
        else
#endif
          if (settings->mJsonRpcPathHash == req_path_hash)
        {
          const char* body = buf->base + pret;
          size_t body_len = (size_t)(nread - pret);
          handle_jsonrpc_request(client, body, body_len, settings);
        }
        else
        {
          LOG_WARN("Wrong path");
          send_jsonrpc_error(client, kJsonRpcMethodNotFound, "Wrong Path", nullptr);
          uv_close((uv_handle_t*)client, (uv_close_cb)free);
        }
      }
    }
    else if (pret == -1)  // parse error
    {
      LOG_WARN("Error parsing http request: %d", pret);
      send_jsonrpc_error(client, kJsonRpcParseError, "Parse error", nullptr);
      uv_close((uv_handle_t*)client, (uv_close_cb)free);
    }
    else if (pret <= -2)
    {
      LOG_WARN("Partial request, replying them to send below %d size", MAX_PACKET_SIZE);
      send_jsonrpc_error(client, kJsonRpcServerError,
                         "Server Error (Sz), try downsizing your packages", nullptr);
      uv_close((uv_handle_t*)client, (uv_close_cb)free);
    }
  }
  else if (nread < 0)
  {
    if (nread != UV_EOF)
      LOG_WARN("Read error %s\n", uv_strerror(nread));

    uv_close((uv_handle_t*)client, (uv_close_cb)free);
  }
  if (buf->base)
    free(buf->base);
}

void http_on_new_connection(uv_stream_t* server, int status)
{
  if (status < 0)
  {
    LOG_WARN("New connection error %s\n", uv_strerror(status));
    return;
  }

  uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
  uv_tcp_init(server->loop, client);
  client->data = server->data;

  if (uv_accept(server, (uv_stream_t*)client) == 0)
  {
    // non blocking
    uv_read_start((uv_stream_t*)client, on_alloc, http_on_read);
  }
  else
    uv_close((uv_handle_t*)client, (uv_close_cb)free);
}


void http_server_thread_func(void* userdata)
{
  auto server_settings = (HTTPServerSettings*)userdata;

  uv_loop_t loop;
  uv_loop_init(&loop);

  uv_tcp_t* server = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
  uv_tcp_init(&loop, server);
  uv_tcp_simultaneous_accepts(server, 1);
  uv_tcp_nodelay(server, 1);
  server->data = server_settings;

  struct sockaddr_in addr;
  uv_ip4_addr(server_settings->mIPAddress, server_settings->mPort, &addr);

#ifdef NETWORK_DBG
  uv_tcp_bind(server, (const struct sockaddr*)&addr, 0);
#else
  uv_tcp_bind(server, (const struct sockaddr*)&addr, SO_REUSEPORT);
#endif

  int r = uv_listen((uv_stream_t*)server, server_settings->mBacklogQueueSz, http_on_new_connection);
  if (r)
  {
    LOG_WARN("Listen error %s\n", uv_strerror(r));
    return;
  }

  uv_run(&loop, UV_RUN_DEFAULT);

  uv_loop_close(&loop);
}
