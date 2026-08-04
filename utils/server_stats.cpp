
#include "server_stats.h"
#include <uv.h>
#include <cstring>
#include <vector>

std::vector<PerfRecord> gPerfRecords;
pthread_mutex_t gPerfMutex = PTHREAD_MUTEX_INITIALIZER;

PerfRecord* GetPerfRecord(uint idx)
{
  if (idx >= gPerfRecords.size())
    return nullptr;
  return &gPerfRecords[idx];
}

uint AddPerfRecord(const char* name)
{
  pthread_mutex_lock(&gPerfMutex);

  PerfRecord rec;
  strcpy(rec.mName, name);
  rec.mNameHash = Hash(name);
  rec.mMutex = PTHREAD_MUTEX_INITIALIZER;

  gPerfRecords.push_back(rec);

  pthread_mutex_unlock(&gPerfMutex);
  
  return (uint)gPerfRecords.size() - 1;
}


void PerfRecord::Update(double ms)
{
  pthread_mutex_lock(&mMutex);

  TimingBuckets bucket = kPerfSub1ms;
  if (ms <= 1.0)
    bucket = kPerfSub1ms;
  else if (ms <= 2.0)
    bucket = kPerf1to2ms;
  else if (ms <= 5.0)
    bucket = kPerf2to5ms;
  else if (ms <= 10.0)
    bucket = kPerf5to10ms;
  else if (ms <= 50.0)
    bucket = kPerf10to50ms;
  else
    bucket = kPerfOver50ms;

  ++mHistogram[bucket];

  mSum += ms;
  ++mCalls;
  mAverage = mSum / (double)mCalls;

  if (mMax < ms)
    mMax = ms;

  pthread_mutex_unlock(&mMutex);
}

void PerfRecord::Reset()
{
  pthread_mutex_lock(&mMutex);

  for (int i=0; i<kNumTimingBuckets; ++i)
    mHistogram[i] = 0;
  mSum = 0.0;
  mAverage = 0.0;
  mMax = 0.0;
  mCalls = 0;

  pthread_mutex_unlock(&mMutex);
}
