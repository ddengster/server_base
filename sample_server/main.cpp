#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "os_utils.h"

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
    printf("process: %s exist\n", gProcessName);
    exit(EXIT_FAILURE);
  }

  if (set_process_limits() != 0)
  {
    printf("set_process_limits failed\n");
    exit(EXIT_FAILURE);
  }

  // do initializations here..
  {
  }
  /*
    uint worker_count = 4;
    for () {}*/

  daemon(1, 1);  // detach from controlling terminal
  fork_process_and_keepalive();

  printf("end\n");
  return 0;
}