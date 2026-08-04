
#pragma once

// prereqs file, include in all headers

#include <stdlib.h>


#define DEVELOPER_BUILD 1


inline uint Hash(const char* str)
{
  uint hash = 5381;
  int c;
  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;  // hash * 33 + c
  return hash;
}
