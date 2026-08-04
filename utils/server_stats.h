
#pragma once

#include "prereqs.h"
#include "logger.h"

#define HIGHRES_TIMER 1

enum TimingBuckets
{
  kPerfSub1ms = 0,
  kPerf1to2ms,
  kPerf2to5ms,
  kPerf5to10ms,
  kPerf10to50ms,
  kPerfOver50ms,
  kNumTimingBuckets
};

struct PerfRecord
{
  char mName[64] = {};
  uint mNameHash = 0;

  int mHistogram[kNumTimingBuckets] = {};

  double mSum = 0.0;
  double mAverage = 0.0;
  double mMax = 0.0;
  int mCalls = 0;

  pthread_mutex_t mMutex;
  void Update(double ms);
  void Reset();
};

uint AddPerfRecord(const char* name);  // returns index
PerfRecord* GetPerfRecord(uint idx);   // do not store return

int GenerateStatsHTMLPage(char (&buffer)[8192]);

#ifdef HIGHRES_TIMER

#define TIMER_START() uint64_t ts_start = uv_hrtime();
#define TIMER_END(markername, log_timetaken)  \
  uint64_t ts_end = uv_hrtime();              \
  double ms = (ts_end - ts_start) / 1e6;      \
  static int idx = AddPerfRecord(markername); \
  auto perf_record = GetPerfRecord(idx);      \
  perf_record->Update(ms);                    \
  if (log_timetaken)                          \
    LOG_INFO(#markername " time taken: %fms", ms);


#else

#define TIMER_START() uint64_t ts_start = uv_now();
#define TIMER_END(markername, log_timetaken)  \
  uint64_t ts_end = uv_now();                 \
  double ms = (ts_end - ts_start) / 1e6;      \
  static int idx = AddPerfRecord(markername); \
  auto perf_record = GetPerfRecord(idx);      \
  perf_record->Update(ms);                    \
  if (log_timetaken)                          \
    LOG_INFO(#markername " time taken: %fms", ms);


#endif
