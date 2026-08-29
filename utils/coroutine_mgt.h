
#pragma once

#include "prereqs.h"
#include "minicoro.h"
#include "slotmap/SlotMap.h"
#include <uv.h>

/**
Small utility layer on top of minicoro to manage coroutines.

How this works:

- Add one CoroutineManager instance per server/event loop that needs coroutines.
- call StepCoroutines() in your main update loop (eg. from a libuv timer), it will resume all
  active coroutines.
- call CleanupDeadCoroutines() periodically to reap finished autodelete coroutines.
- the destructor runs DestroyAllCoroutines() for cleanup

In your Immediate mode routines:
- CreateManagedCoroutine() and grab the handle to the coroutine, keep it around.
- When you want to check on the coroutine, call GetCoroutine(handle) to get the pointer to the
coroutine object, then check its status or user data or whatever you need.
- when you check the coroutine and find it's done (ie. mco_status(co) == MCO_DEAD), call
DestroyCoroutine(handle) to clean it up. don't forget to set your handle to -1 or something so you
don't accidentally check on it again next frame.

*/

struct DBConnectionCtx;

class CoroutineManager
{
public:
  CoroutineManager() {}
  ~CoroutineManager();

  // non-interactive coroutine. clientstream is closed upon coroutine death
  void CreateUntrackedCoroutine(void (*co_func)(mco_coro*), uv_stream_t* clientstream,
                                void* user_data = nullptr);

  // coroutine that you can lookup from external
  ObjectHandle CreateManagedCoroutine(void (*co_func)(mco_coro*), void* user_data = nullptr,
                                      bool autodelete = true);

  mco_coro* GetCoroutine(ObjectHandle handle);

  void DestroyCoroutine(ObjectHandle handle);

  // call this in an update loop to resume all active coroutines
  void StepCoroutines();

  void CleanupDeadCoroutines();

  void DestroyAllCoroutines();

  int GetCoroutineCount();

private:
  struct Coro
  {
    ObjectHandle mHandle = NULL_OBJECTHANDLE;
    mco_coro* mCoro = nullptr;
    bool mAutoDelete = false;
  };

  SSlotMap<Coro> mCoroutineData;

  struct Coro2
  {
    mco_coro* mCoro = nullptr;
    uv_stream_t* mStream = nullptr;
  };

  std::vector<Coro2> mUntrackedCoroutines;
};

/***  coroutine control functions ****/

void yield();

typedef bool (*CoConditionalNoArg)();
typedef bool (*CoConditional)(void* userdata);
void yield_until_true(CoConditionalNoArg conditionalfunc);
void yield_until_true(CoConditional conditionalfunc, void* userdata);

int64_t get_time_ms();

void yield_for(float seconds);  // ms resolution
