
#include "prereqs.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

  if (set_process_limits() != 0)
  {
    printf("set_process_limits failed\n");
    exit(EXIT_FAILURE);
    return -1;
  }

  // do initializations here..
  log_init("logs");
  /*
    uint worker_count = 4;
    for () {}*/

  daemon(1, 1);  // detach from controlling terminal
  fork_process_and_keepalive();


  LOG_INFO("end\n");

  // int* a = nullptr;
  // *a = 12121;
  return 0;
}