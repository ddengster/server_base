
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

#if 0
void tcp_on_read_job_handover(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf)
{
  if (nread > 0)
  {
    LOG("Received %d bytes: %.*s\n", (int)nread, nread, buf->base);
    // hand over to thread pool
    struct Job
    {
      uv_work_t work_req;     // Libuv work request wrapper (MUST be first)
      uv_stream_t* client;    // Pointer to the client socket
      char* data_buffer;      // Copied data payload for background processing
      ssize_t data_len;       // Length of the data payload
      int processing_result;  // Output status/result from background work
    };
    Job* job = new Job();
    job->data_buffer = buf->base;
    job->data_len = nread;
    job->work_req.data = job;

    client->data;

    // Offload the worker function to the background thread pool
    // the 'after_work_cb' is executed on the event loop
    uv_queue_work(client->loop, &job->work_req, heavy_processing_worker, after_heavy_processing);
  }

  if (nread < 0)
  {
    if (nread != UV_EOF)
      LOG_WARN("Read error %s\n", uv_strerror(nread));

    uv_close((uv_handle_t*)client, (uv_close_cb)free);
  }
  if (buf->base)
    free(buf->base);
}
#endif

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

static void send_jsonrpc_error(uv_stream_t* client, int code, const char* message)
{
  if (uv_is_closing((uv_handle_t*)client))
    return;
  yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);

  yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");
  yyjson_mut_val* err = yyjson_mut_obj_add_obj(doc, root, "error");
  yyjson_mut_obj_add_int(doc, err, "code", code);
  yyjson_mut_obj_add_str(doc, err, "message", message);
  yyjson_mut_obj_add_val(doc, root, "id", yyjson_mut_null(doc));

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
                     "%s",
                     json_len, json);
  // free(json);


  uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
  req->data = client;

  char* b = (char*)malloc(len + 1);
  strncpy(b, buf, len);

  uv_buf_t sendbuf = uv_buf_init(b, len + 1);
  if (uv_is_closing((uv_handle_t*)client))
    return;
  int result = uv_try_write(client, &sendbuf, 1);
  if (result < 0)
  {
    LOG_WARN("Failed to initiate write: %s\n", uv_strerror(result));
    free(req);
  }
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
      LOG("SUCCESS: method: %.*s, path: %.*s, ", method_len, method, path_len, path);
      auto settings = (HTTPServerSettings*)client->data;
      // if (settings->mDataRecvCallback)
      //   settings->mDataRecvCallback(client, buf);
    }
    else if (pret == -1)  // parse error
    {
      LOG_WARN("Error parsing http request: %d", pret);
      send_jsonrpc_error(client, 400, "Parse error");
      uv_close((uv_handle_t*)client, (uv_close_cb)free);
    }
    else if (pret <= -2)
    {
      LOG_WARN("Partial request, replying them to send below %d size", MAX_PACKET_SIZE);
      send_jsonrpc_error(client, 500, "Server Error Sz");
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
