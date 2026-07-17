#include <stdio.h>

int main(int argc, char *argv[])
{
  printf("process: sample_server, compile date: %s %s\n", __DATE__, __TIME__);
  /*if (argc < 2) 
  {
    printf("usage: %s config.json\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  if (process_exist(__process__) != 0) 
  {
    printf("process: %s exist\n", __process__);
    exit(EXIT_FAILURE);
  }*/


  return 0;
}