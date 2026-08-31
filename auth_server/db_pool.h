
#pragma once

#include <postgresql/libpq-fe.h>
#include <vector>
#include <uv.h>

/****
 * @note
 * Assuming each thread runs a uv_loop and hosts a tcp/http server listening on a port,
 * we setup X database connections at initialization, deploy them as new db queries are made
                     uv_loop_t
                         │
     ┌───────────────────┼───────────────────┐
     │                   │                   │
uv_poll(conn A)   uv_poll(conn B)   uv_poll(conn C)
     │                   │                   │
     ▼                   ▼                   ▼
 PostgreSQL         PostgreSQL         PostgreSQL
     │                   │                   │
     └──────────────┬────┴──────────────┬────┘
                    ▼                   ▼
               http_call_resume()    http_call_resume()
 *****/
// @note: For postgres db side, do \show max_connections, by default it's 100

struct DBConnectionCtx
{
  PGconn* mConn = nullptr;
  uv_stream_t* mStream = nullptr;
  // heap-allocated so the handle can be closed & replaced on reconnect
  // (uv_poll_t must not be re-initialized in place with a new fd)
  uv_poll_t* mPollData = nullptr;
  bool mInUse = false;
  bool mDataReady = false;
  bool mTerminate = false;  // for db polling errors, stop coroutines immediately
  bool mReconnectPhase = false;

  bool DonePollingData() { return mDataReady || mTerminate; }
  void ResetFlags() { mDataReady = mTerminate = false; }
};

// Connection pool to postgres
struct PostgresConnectionPool
{
public:
  const char* mConnectionStr = nullptr;
  const char* mPingStr = nullptr;  // host & port only

  DBConnectionCtx* mConnections = nullptr;
  int mNumConnections = 0;

public:
  void InitializeConnections(const char* connection_str, int num_connections, uv_loop_t* uvloop);
  void ShutdownConnections();

  DBConnectionCtx* AcquireDBConn(uv_stream_t* stream);
  void ReleaseDBConn(DBConnectionCtx* ctx);

  static void Poll_Callback(uv_poll_t* handle, int status, int events);
  static void CheckConnections_TimerCb(uv_timer_t* timer);
};

/** Async yield functions **/
DBConnectionCtx* yield_until_db_ctx_or_time_limit(void* db_pool, uv_stream_t* stream,
                                                  int millsecs = 500);

// yield until DonePollingData() returns true. Handle the mTerminate conditional if true
void start_poll_and_yield_until_polldata(DBConnectionCtx*);

/** Helper functions **/

int PQsendQueryParamsIgnoreResults(DBConnectionCtx* db_ctx, PGconn* conn, const char* command,
                                   int nParams, const Oid* paramTypes,
                                   const char* const* paramValues, const int* paramLengths,
                                   const int* paramFormats, int resultFormat);