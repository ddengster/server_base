
#include "db_pool.h"
#include "logger.h"
#include "coroutine_mgt.h"
#include "server_stats.h"
#include "network.h"

void PostgresConnectionPool::InitializeConnections(const char* connection_str, int num_connections,
                                                   uv_loop_t* uvloop)
{
  mConnectionStr = connection_str;
  mNumConnections = num_connections;
  mConnections = new DBConnectionCtx[num_connections];

  uint retries = 0;
  uint max_retries = 5;
  for (int i = 0; i < mNumConnections;)
  {
    // synchronous connection, akways returns non-null
    // @ref: https://postgrespro.com/docs/enterprise/current/libpq-connect
    PGconn* conn = PQconnectdb(connection_str);
    mConnections[i].mConn = conn;

    if (PQstatus(conn) != CONNECTION_OK)
    {
      LOG_WARN("Connection to database failed: %s", PQerrorMessage(conn));
      PQfinish(conn);
      ++retries;
      if (retries >= max_retries)
        exit(EXIT_FAILURE);
    }
    else
    {
      PQsetnonblocking(conn, 1);  // swap to async
      // register interest in the loop, do not start it yet
      uv_poll_init(uvloop, &mConnections[i].mPollData, PQsocket(conn));
      ++i;
    }
  }
}

void PostgresConnectionPool::ShutdownConnections()
{
  for (int i = 0; i < mNumConnections; ++i)
  {
    uv_poll_stop(&mConnections[i].mPollData);
    PQfinish(mConnections[i].mConn);
  }

  delete[] mConnections;
}

DBConnectionCtx* PostgresConnectionPool::AcquireDBConn(uv_stream_t* stream)
{
  for (int i = 0; i < mNumConnections; ++i)
  {
    PGconn* conn = mConnections[i].mConn;
    int status = PQstatus(conn);
    if (status == CONNECTION_OK && !mConnections[i].mInUse && !mConnections[i].mTerminate &&
        !mConnections[i].mReconnectPhase)
    {
      mConnections[i].mInUse = true;
      mConnections[i].ResetFlags();
      mConnections[i].mStream = stream;
      return &mConnections[i];
    }
  }
  return nullptr;
}

void PostgresConnectionPool::ReleaseDBConn(DBConnectionCtx* ctx)
{
  ctx->mInUse = false;
  ctx->mStream = nullptr;
}

void PostgresConnectionPool::Poll_Callback(uv_poll_t* handle, int status, int events)
{
  DBConnectionCtx* ctx = (DBConnectionCtx*)handle->data;

  if (status < 0)
  {
    //@reference: https://docs.libuv.org/en/v1.x/poll.html
    // one of: https://docs.libuv.org/en/v1.x/errors.html#errors
    LOG_WARN("Poll callback status < 0 with %d, msg: %s", status, PQerrorMessage(ctx->mConn));
    if (status == UV_EBADF)
    {
      uv_poll_stop(handle);
      ctx->mTerminate = true;
      SAFE_UV_CLOSE(ctx->mStream, free);
    }
    return;
  }

  if (!PQconsumeInput(ctx->mConn))
  {
    LOG_WARN("PQconsumeInput failed: handle: status: %d, msg: %s", status,
             PQerrorMessage(ctx->mConn));
    uv_poll_stop(handle);
    ctx->mTerminate = true;
    SAFE_UV_CLOSE(ctx->mStream, free);
    return;
  }

  // Check if the query is still processing internally
  if (PQisBusy(ctx->mConn))
    return;  // wait

  // data is ready
  ctx->mDataReady = true;

  uv_poll_stop(handle);
}

void PostgresConnectionPool::CheckConnections_TimerCb(uv_timer_t* timer)
{
  auto db_pool = (PostgresConnectionPool*)timer->data;
  if (!db_pool->mPingStr)
    return;

  //@note: roughly 4-5ms ping to localhost, more for other setups
  // TIMER_START();
  PGPing p = PQping(db_pool->mPingStr);
  // TIMER_END("ping", true);

  bool one_db_connection_bad = false;
  if (p == PQPING_OK)
  {
    for (int i = 0; i < db_pool->mNumConnections; ++i)
    {
      DBConnectionCtx* ctx = &db_pool->mConnections[i];
      if (ctx->mConn == nullptr || PQstatus(ctx->mConn) != CONNECTION_OK)
      {
        one_db_connection_bad = true;
        break;
      }
    }
  }

  if (p != PQPING_OK || one_db_connection_bad)
  {
    LOG_WARN("DB connection failed, PGPing: %d", p);
    LOG_WARN("Restarting all db connections");

    for (int i = 0; i < db_pool->mNumConnections; ++i)
    {
      DBConnectionCtx* ctx = &db_pool->mConnections[i];

      if (ctx->mConn)
      {
        uv_poll_stop(&ctx->mPollData);
        PQfinish(ctx->mConn);
        ctx->mConn = nullptr;
      }
      ctx->mReconnectPhase = true;

      // connect synchronously
      ctx->mConn = PQconnectdb(db_pool->mConnectionStr);
      if (PQstatus(ctx->mConn) != CONNECTION_OK)
      {
        LOG_WARN("Connection %d to database failed: %s", i, PQerrorMessage(ctx->mConn));
        PQfinish(ctx->mConn);
        ctx->mConn = nullptr;
        break;  // wait for next iteration of callback
      }
      else
      {
        PQsetnonblocking(ctx->mConn, 1);  // swap to async
        // register interest in the loop, do not start it yet
        uv_poll_init(timer->loop, &ctx->mPollData, PQsocket(ctx->mConn));

        ctx->ResetFlags();
        ctx->mReconnectPhase = false;
      }
    }
  }
}

DBConnectionCtx* yield_until_db_ctx_or_time_limit(void* pool, uv_stream_t* stream, int millsecs)
{
  auto db_pool = (PostgresConnectionPool*)pool;

  volatile int64_t time_to_stopyield = get_time_ms() + (int64_t)(millsecs);

  DBConnectionCtx* db_ctx = nullptr;
  while (db_ctx == nullptr && get_time_ms() < time_to_stopyield)
  {
    if (uv_is_closing((uv_handle_t*)stream))
      return nullptr;

    db_ctx = db_pool->AcquireDBConn(stream);
    if (db_ctx == nullptr)
      yield();
  }
  return db_ctx;
}

void start_poll_and_yield_until_polldata(DBConnectionCtx* db_ctx)
{
  // activate polling for this connection during the Poll for I/O phase
  db_ctx->mPollData.data = (void*)db_ctx;
  uv_poll_start(&db_ctx->mPollData, UV_READABLE, &PostgresConnectionPool::Poll_Callback);

  yield_until_true([](void* db_ctx) { return ((DBConnectionCtx*)db_ctx)->DonePollingData(); },
                   db_ctx);
}

int PQsendQueryParamsIgnoreResults(DBConnectionCtx* db_ctx, PGconn* conn, const char* command,
                                   int nParams, const Oid* paramTypes,
                                   const char* const* paramValues, const int* paramLengths,
                                   const int* paramFormats, int resultFormat)
{
  int update_res = PQsendQueryParams(conn, command, 1, NULL, paramValues, NULL, NULL, 0);
  if (update_res != 0)
  {
    start_poll_and_yield_until_polldata(db_ctx);

    if (!db_ctx->mTerminate)
    {
      PGresult* update_pgres = nullptr;
      while ((update_pgres = PQgetResult(conn)) != nullptr)
        PQclear(update_pgres);
    }
  }
  return update_res;
}