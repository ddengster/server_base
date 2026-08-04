
#include "server_stats.h"
#include <uv.h>
#include <cstring>
#include <cstdio>
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
  uint ret_idx = (uint)gPerfRecords.size() - 1;

  pthread_mutex_unlock(&gPerfMutex);

  return ret_idx;
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

  for (int i = 0; i < kNumTimingBuckets; ++i)
    mHistogram[i] = 0;
  mSum = 0.0;
  mAverage = 0.0;
  mMax = 0.0;
  mCalls = 0;

  pthread_mutex_unlock(&mMutex);
}

int GenerateStatsHTMLPage(char (&buffer)[8192])
{
  static const char* bucketnames[kNumTimingBuckets] = {"<1ms",   "1-2ms",   "2-5ms",
                                                       "5-10ms", "10-50ms", ">50ms"};

  static const char* header = "<!DOCTYPE html>"
                              "<html>"
                              "<head>"
                              "<title>Server Performance Statistics</title>"
                              "<style>"
                              "body { font-family: Arial, sans-serif; margin: 40px; }"
                              "table { border-collapse: collapse; width: 100%%; }"
                              "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }"
                              "th { background-color: #f2f2f2; }"
                              "tr:nth-child(even) { background-color: #f9f9f9; }"
                              "</style>"
                              "</head>"
                              "<body>"
                              "<h1>Server Performance Statistics</h1>";

  int n = 0;
  n += snprintf(buffer + n, sizeof(buffer) - (size_t)n, "%s", header);

  pthread_mutex_lock(&gPerfMutex);

  for (size_t i = 0; i < gPerfRecords.size(); ++i)
  {
    PerfRecord& rec = gPerfRecords[i];

    n += snprintf(buffer + n, sizeof(buffer) - (size_t)n,
                  "<h2>%s</h2>"
                  "<table>"
                  "<tr><th>Metric</th><th>Value</th></tr>"
                  "<tr><td>Calls</td><td>%d</td></tr>"
                  "<tr><td>Avg time</td><td>%.3f ms</td></tr>"
                  "<tr><td>Max time</td><td>%.3f ms</td></tr>"
                  "<tr><td>Total time</td><td>%.3f ms</td></tr>",
                  rec.mName, rec.mCalls, rec.mAverage, rec.mMax, rec.mSum);

    for (int b = 0; b < kNumTimingBuckets; ++b)
    {
      n += snprintf(buffer + n, sizeof(buffer) - (size_t)n, "<tr><td>%s</td><td>%d</td></tr>",
                    bucketnames[b], rec.mHistogram[b]);
    }

    n += snprintf(buffer + n, sizeof(buffer) - (size_t)n, "</table>");
  }

  pthread_mutex_unlock(&gPerfMutex);

  n += snprintf(buffer + n, sizeof(buffer) - (size_t)n,
                "</body>"
                "</html>\r\n");
  return n;
}