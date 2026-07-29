
#include "network.h"
#include "logger.h"
#include <stdlib.h>
#include <uv.h>

uv_loop_t* gGlobalLoop = nullptr;

void set_loop(uv_loop_t* loop)
{ gGlobalLoop = loop; }

// prompt 'How glibc malloc Routes malloc', 1032bytes or 80-160byte
#define MAX_PACKET_SIZE 1024

void on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  if (suggested_size > MAX_PACKET_SIZE)
  {
    LOG_WARN("Suggested packet size (%d) exceeds limit!", suggested_size);
    buf->base = nullptr;
    buf->len = 0;
    return;
  }

  buf->base = (char*)malloc(suggested_size);
  buf->len = suggested_size;
}

void tcp_on_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf)
{
  if (nread > 0)
  {
    LOG_WARN("Received %d bytes\n", (int)nread);
    if (buf->base)
      free(buf->base);
    return;
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


void tcp_on_new_connection(uv_stream_t* server, int status)
{
  if (status < 0)
  {
    LOG_WARN("New connection error %s\n", uv_strerror(status));
    return;
  }

  uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
  uv_tcp_init(gGlobalLoop, client);

  if (uv_accept(server, (uv_stream_t*)client) == 0)
    uv_read_start((uv_stream_t*)client, on_alloc, tcp_on_read);
  else
    uv_close((uv_handle_t*)client, (uv_close_cb)free);
}

void tcp_server_setup(const char* ip_address, int port, int backlog_queue_sz)
{
  uv_tcp_t server;
  uv_tcp_init(gGlobalLoop, &server);
  uv_tcp_simultaneous_accepts(&server, 1);
  uv_tcp_nodelay(&server, 1);

  struct sockaddr_in addr;
  uv_ip4_addr("localhost", port, &addr);

  uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);

  int r = uv_listen((uv_stream_t*)&server, backlog_queue_sz, tcp_on_new_connection);
  if (r)
  {
    LOG_WARN("Listen error %s\n", uv_strerror(r));
    return;
  }
}
