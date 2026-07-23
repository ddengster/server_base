
#include "prereqs.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>

#include "os_utils.h"
#include "logger.h"

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

#if 1
  // process that spawns multiple workers
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
  // process_title_set("%s_listener", __process__);
  daemon(1, 1);
  fork_process_and_keepalive();

run:
  LOG_INFO("end\n");

  while (true) {}

#else
  daemon(1, 1);  // detach from controlling terminal
  fork_process_and_keepalive();


  LOG_INFO("end\n");
#endif
  return 0;
}