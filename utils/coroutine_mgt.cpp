
#include "prereqs.h"
#include "coroutine_mgt.h"
#include "slotmap/SlotMap.h"
#include <time.h>

#define MINICORO_IMPL
#include "minicoro.h"

#include "network.h"
#include <assert.h>

CoroutineManager::~CoroutineManager()
{ DestroyAllCoroutines(); }

void CoroutineManager::CreateUntrackedCoroutine(void (*co_func)(mco_coro*),
                                                uv_stream_t* clientstream, void* user_data,
                                                void (*user_data_free_cb)(void*))
{
  mco_desc desc = mco_desc_init(co_func, 0);
  desc.user_data = user_data;

  mco_coro* coro = nullptr;
  mco_result res = mco_create(&coro, &desc);
  assert(res == MCO_SUCCESS && "Failed to make coroutine");

  Coro2 c;
  c.mCoro = coro;
  c.mStream = clientstream;
  c.mUserDataFreeCb = user_data_free_cb;
  mUntrackedCoroutines.push_back(c);
}

ObjectHandle CoroutineManager::CreateManagedCoroutine(void (*co_func)(mco_coro*), void* user_data,
                                                      bool autodelete)
{
  Coro* coro = mCoroutineData.Add();
  coro->mAutoDelete = autodelete;

  mco_desc desc = mco_desc_init(co_func, 0);
  desc.user_data = user_data;

  mco_result res = mco_create(&coro->mCoro, &desc);
  assert(res == MCO_SUCCESS && "Failed to make coroutine");

  return coro->mHandle;
}

mco_coro* CoroutineManager::GetCoroutine(ObjectHandle handle)
{
  Coro* coro = mCoroutineData.Get(handle);
  if (coro)
    return coro->mCoro;
  return nullptr;
}

void CoroutineManager::DestroyCoroutine(ObjectHandle handle)
{
  Coro* coro = mCoroutineData.Get(handle);
  if (coro && coro->mCoro)
  {
    mco_destroy(coro->mCoro);
    coro->mCoro = nullptr;
  }
  mCoroutineData.Remove(handle);
}

void CoroutineManager::StepCoroutines()
{
  for (uint i = 0; i < mCoroutineData.size(); ++i)
  {
    Coro* coro = mCoroutineData[i];
    if (coro && coro->mCoro && mco_status(coro->mCoro) == MCO_SUSPENDED)
      mco_resume(coro->mCoro);
  }

  for (uint i = 0; i < mUntrackedCoroutines.size(); ++i)
  {
    Coro2& coro = mUntrackedCoroutines[i];
    if (coro.mCoro && mco_status(coro.mCoro) == MCO_SUSPENDED)
      mco_resume(coro.mCoro);
  }
}

void CoroutineManager::CleanupDeadCoroutines()
{
  for (uint i = 0; i < mCoroutineData.size(); ++i)
  {
    Coro* coro = mCoroutineData[i];
    if (coro && coro->mAutoDelete && coro->mCoro && mco_status(coro->mCoro) == MCO_DEAD)
      DestroyCoroutine(coro->mHandle);
  }

  for (uint i = 0; i < mUntrackedCoroutines.size();)
  {
    Coro2& coro = mUntrackedCoroutines[i];
    if (coro.mCoro && mco_status(coro.mCoro) == MCO_DEAD)
    {
      if (coro.mUserDataFreeCb && coro.mCoro->user_data)
        coro.mUserDataFreeCb(coro.mCoro->user_data);
      mco_destroy(coro.mCoro);
      SAFE_UV_CLOSE(coro.mStream, free);
      //@todo: swap&erase?
      mUntrackedCoroutines.erase(mUntrackedCoroutines.begin() + i);
    }
    else
      ++i;
  }
}

void CoroutineManager::DestroyAllCoroutines()
{
  for (uint i = 0; i < mCoroutineData.size(); ++i)
  {
    Coro* coro = mCoroutineData[i];
    if (coro && coro->mCoro)
    {
      mco_destroy(coro->mCoro);
      coro->mCoro = nullptr;
    }
  }
  mCoroutineData.Clear();

  for (uint i = 0; i < mUntrackedCoroutines.size(); ++i)
  {
    Coro2& coro = mUntrackedCoroutines[i];
    if (coro.mUserDataFreeCb && coro.mCoro && coro.mCoro->user_data)
      coro.mUserDataFreeCb(coro.mCoro->user_data);
    mco_destroy(coro.mCoro);
    SAFE_UV_CLOSE(coro.mStream, free);
  }
  mUntrackedCoroutines.clear();
}

int CoroutineManager::GetCoroutineCount()
{ return (int)mCoroutineData.count() + (int)mUntrackedCoroutines.size(); }

void yield()
{ mco_yield(mco_running()); }

void yield_until_true(CoConditionalNoArg conditionalfunc)
{
  while (!conditionalfunc())
    mco_yield(mco_running());
}

void yield_until_true(CoConditional conditionalfunc, void* userdata)
{
  while (!conditionalfunc(userdata))
    mco_yield(mco_running());
}

int64_t get_time_ms()
{
  struct timespec ts;
  // Get current calendar time with nanosecond resolution
  if (timespec_get(&ts, TIME_UTC) == 0)
    return -1;  // Failure

  // Convert seconds and nanoseconds to milliseconds
  return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

void yield_for(float seconds)
{
  volatile int64_t time_to_stopyield = get_time_ms() + (int64_t)(seconds * 1000.0f);

  while (get_time_ms() < time_to_stopyield)
  {
    mco_yield(mco_running());
  }
}
