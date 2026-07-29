
#include "prereqs.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>

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

  uv_loop_t* loop = uv_default_loop();
  set_loop(loop);
  tcp_server_setup("127.0.0.1", 8080);

  return uv_run(loop, UV_RUN_DEFAULT);


#else
  daemon(1, 1);  // detach from controlling terminal
  fork_process_and_keepalive();


  LOG_INFO("end\n");
  return 0;
#endif
}