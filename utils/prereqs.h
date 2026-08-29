
#pragma once

// prereqs file, include in all headers

#include <cstring>
#include <string>
#include <stdlib.h>

// #define DEVELOPER_BUILD 1

inline uint Hash(const char* str)
{
  uint hash = 5381;
  int c;
  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;  // hash * 33 + c
  return hash;
}

// 32-bit Weak Reference Handle containing 15 bits each for ID and Recycle Version,
// plus 1 bit for Deleted flag (supports iteration).
// ObjectHandles must be initialized to NULL_OBJECTHANDLE.
typedef int ObjectHandle;
#define NULL_OBJECTHANDLE -1

typedef std::string String;
typedef std::wstring WString;

inline bool strequal(const char* a, const char* b)
{ return strcmp(a, b) == 0; }