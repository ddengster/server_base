
#include "prereqs.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>
#include <cstring>

#include <uv.h>

#include "os_utils.h"
#include "logger.h"
#include "network.h"

const char* gProcessName = "sample_http";

int main(int argc, char* argv[])
{
  printf("process: sample_http, compile date: %s %s\n", __DATE__, __TIME__);
  /*if (argc < 2)
  {
    printf("usage: %s config.json\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  */
  if (process_exist(gProcessName) != 0)
  {
    printf("process: %s exists. pgrep <process name> for pid and use kill -9 <pid>\n",
           gProcessName);
    exit(EXIT_FAILURE);
    return -1;
  }
  process_title_init(argc, argv);

  if (set_process_limits() != 0)
  {
    printf("set_process_limits failed\n");
    exit(EXIT_FAILURE);
    return -1;
  }

  // do initializations here..
  log_init("logs");

  daemon(1, 1);  // detach from controlling terminal
  fork_process_and_keepalive();

#if 1
  {
    // 4 threads for event loops, each listening on the same port. SO_REUSEPORT tells the kernel to
    // handle loading balancing between sockets for you. from then on oyu
    // setenv("UV_THREADPOOL_SIZE", "8", 1);

    auto server_tcp_callback = [](uv_stream_t* client, const uv_buf_t* buf)
    {
      const char* message = "ok\n";
      if (buf->len > 0 && (buf->base[0] >= '1' && buf->base[0] <= '9'))
        message = "number ok\n";

      uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
      req->data = client;

      uv_buf_t sendbuf = uv_buf_init((char*)message, strlen(message));
      int result = uv_write(req, client, &sendbuf, 1, common_write_end_cb);
      if (result < 0)
      {
        LOG_WARN("Failed to initiate write: %s\n", uv_strerror(result));
        free(req);
      }
    };

    uint num_threads = 1;
    HTTPServerSettings* settings = new HTTPServerSettings[num_threads];
    uv_thread_t* thread = new uv_thread_t[num_threads];

    for (uint i = 0; i < num_threads; ++i)
    {
      settings[i].mIPAddress = "127.0.0.1";
      settings[i].mPort = 8081;
      // settings[i].mDataRecvCallback = server_tcp_callback;
      uv_thread_create(&thread[i], http_server_thread_func, &settings[i]);
    }

    for (int i = 0; i < 4; ++i)
      uv_thread_join(&thread[i]);
    delete[] settings;
    delete[] thread;
  }
#endif
  return 0;
}