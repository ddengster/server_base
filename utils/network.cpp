
#include "network.h"
#include "logger.h"
#include <stdlib.h>
#include <uv.h>

// prompt 'How glibc malloc Routes malloc', 1032bytes or 80-160byte
#define MAX_PACKET_SIZE 1 * 1024

void on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  // uv_handle_get_loop(handle);
  //  ignore packet sizes, give 1k
  buf->base = new char[MAX_PACKET_SIZE];
  buf->len = MAX_PACKET_SIZE;
}

void tcp_on_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf)
{
  if (nread > 0)
  {
#ifdef NETWORK_DBG_LOG
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

  uv_tcp_bind(server, (const struct sockaddr*)&addr, SO_REUSEPORT);

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
