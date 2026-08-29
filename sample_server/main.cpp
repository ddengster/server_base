
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

const char* gProcessName = "sample_server";

int main(int argc, char* argv[])
{
  printf("process: sample_server, compile date: %s %s\n", __DATE__, __TIME__);
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

  //@note: good read on multiprocess vs multithread.
  // Tldr: multiprocess for untrusted 3rd party 'this-can-crash' code, otherwise multithread
  // https://www.reddit.com/r/ExperiencedDevs/comments/1pqoo4g/multi_process_or_multi_thread_architectures_on/

#if 0    // process that spawns multiple worker processes
  //@note: gdb only follows first forked child, for dev debug one first

  int worker_count = 4;
  for (int i = 0; i < worker_count; ++i)
  {
    int pid = fork();
    if (pid < 0)
      error(EXIT_FAILURE, errno, "fork error");
    else if (pid == 0)
    {
      // child
      process_title_set("%s_worker_%d", gProcessName, i);
      daemon(1, 1);
      fork_process_and_keepalive();
      log_childprocess_init();

      LOG_INFO("worker %d\n", i);
      /*
      ret = init_server();
      if (ret < 0)
      {
        error(EXIT_FAILURE, errno, "init server fail: %d", ret);
      }*/

      goto run;
    }
  }

  // main process becomes listener
  process_title_set("%s_listener", __process__);
  daemon(1, 1);
  fork_process_and_keepalive();

run:
  LOG_INFO("end\n");

  while (true) {}
  return 0;
#elif 1  // libuv multithread implementation

  daemon(1, 1);  // detach from controlling terminal
  fork_process_and_keepalive();

  log_set_async();

#if 0
  {
    // a single server listening on a port intended to serve other internal backend servers
    uv_loop_t* loop = uv_default_loop();
    TCPServerSettings settings;
    settings.mIPAddress = "127.0.0.1";
    settings.mPort = 8081;
    settings.mDataRecvCallback = [](uv_stream_t* client, const uv_buf_t* buf)
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

    tcp_server_setup(loop, &settings);
    return uv_run(loop, UV_RUN_DEFAULT);
  }
#endif

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

    uint num_threads = 4;
    TCPServerSettings* settings = new TCPServerSettings[num_threads];
    uv_thread_t* thread = new uv_thread_t[num_threads];

    for (uint i = 0; i < num_threads; ++i)
    {
      settings[i].mIPAddress = "127.0.0.1";
      settings[i].mPort = 8081;
      settings[i].mDataRecvCallback = server_tcp_callback;
      uv_thread_create(&thread[i], tcp_server_thread_func, &settings[i]);
    }

    for (uint i = 0; i < num_threads; ++i)
      uv_thread_join(&thread[i]);
    delete[] settings;
    delete[] thread;
  }
#endif
  return 0;


#else
  daemon(1, 1);  // detach from controlling terminal
  fork_process_and_keepalive();


  LOG_INFO("end\n");
  return 0;
#endif
}