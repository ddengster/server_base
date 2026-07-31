#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#include "logger.h"

// Callback to allocate memory when receiving data
void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  (void)handle;
  buf->base = (char*)malloc(suggested_size);
  buf->len = suggested_size;
}

// Callback invoked after closing a handle
void on_close(uv_handle_t* handle)
{
  LOG("Connection closed.\n");
  free(handle);
}

// Callback invoked when reading incoming data from the server
void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
  if (nread > 0)
  {
    // Data received successfully
    LOG("Received from server: %.*s", (int)nread, buf->base);
  }
  else if (nread < 0)
  {
    // Error or EOF (Server disconnected)
    if (nread != UV_EOF)
    {
      LOG_WARN("Read error: %s", uv_err_name(nread));
    }
    else
    {
      LOG("Server disconnected.");
    }
    uv_close((uv_handle_t*)stream, on_close);
  }

  // Always free the allocated buffer base
  if (buf->base)
  {
    free(buf->base);
  }
}

// Callback invoked after data is written to the stream
void on_write(uv_write_t* req, int status)
{
  if (status < 0)
  {
    LOG_WARN("Write error: %s", uv_err_name(status));
  }
  else
  {
    LOG("Message sent to server successfully.");
  }
  // Free the write request container
  free(req);
}

// Callback invoked once the connection to the server is established
void on_connect(uv_connect_t* req, int status)
{
  if (status < 0)
  {
    LOG_WARN("Connection failed: %s", uv_err_name(status));
    uv_close((uv_handle_t*)req->handle, on_close);
    free(req);
    return;
  }

  LOG("Connected to server successfully!");
  uv_stream_t* stream = req->handle;
  free(req);  // The connection request is no longer needed

  // 1. Start reading data from the server
  uv_read_start(stream, alloc_buffer, on_read);

  // 2. Prepare and send an initial greeting message
  const char* message = "Hello from libuv client!";
  char* b = (char*)malloc(strlen(message) + 1);
  strncpy(b, message, strlen(message) + 1);
  uv_buf_t buffer = uv_buf_init(b, strlen(message) + 1);

  uv_write_t* write_req = (uv_write_t*)malloc(sizeof(uv_write_t));
  int result = uv_write(write_req, stream, &buffer, 1, on_write);
  if (result < 0)
  {
    LOG_WARN("Failed to initiate write: %s", uv_err_name(result));
  }
}

int main()
{
  // Obtain the default event loop loop execution context
  uv_loop_t* loop = uv_default_loop();

  // Allocate and initialize the TCP structure
  uv_tcp_t* socket = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
  uv_tcp_init(loop, socket);

  // Set up the destination endpoint (IPv4)
  struct sockaddr_in dest;
  uv_ip4_addr("127.0.0.1", 45261, &dest);

  // Allocate connection request state and trigger the connection
  uv_connect_t* connect_req = (uv_connect_t*)malloc(sizeof(uv_connect_t));
  int result = uv_tcp_connect(connect_req, socket, (const struct sockaddr*)&dest, on_connect);

  if (result < 0)
  {
    LOG_WARN("Failed to initiate connection: %s\n", uv_err_name(result));
    return 1;
  }

  // Block and run the event loop engine until all tasks are finished
  return uv_run(loop, UV_RUN_DEFAULT);
}
