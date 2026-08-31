
#include "prereqs.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>
#include <cstring>

#include <uv.h>
#include <postgresql/libpq-fe.h>

#include "os_utils.h"
#include "logger.h"
#include "network.h"
#include "yyjson.h"
#include "server_stats.h"
#include "security.h"
#include "auth.h"
#include "db_pool.h"

const char* gProcessName = "auth_server";

int main(int argc, char* argv[])
{
  printf("process: auth_server, compile date: %s %s\n", __DATE__, __TIME__);
  /*if (argc < 2)
  {
    printf("usage: %s config.json\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  */
  if (process_exist(gProcessName) != 0)
  {
    printf("process: %s exists. pgrep <process name> for pid and use kill -9 <pid>\n",
           gProcessName);
    exit(EXIT_FAILURE);
    return -1;
  }
  process_title_init(argc, argv);

  if (set_process_limits() != 0)
  {
    printf("set_process_limits failed\n");
    exit(EXIT_FAILURE);
    return -1;
  }

  // do initializations here..
  log_init("logs");

  daemon(1, 1);  // detach from controlling terminal
  fork_process_and_keepalive();

  log_set_async();
#if 1
  {
    // 4 threads for event loops, each listening on the same port. SO_REUSEPORT tells the kernel to
    // handle loading balancing between sockets for you. from then on oyu
    setenv("UV_THREADPOOL_SIZE", "8", 1);

    uint num_threads = 1;
    HTTPServerSettings* settings = new HTTPServerSettings[num_threads];
    uv_thread_t* thread = new uv_thread_t[num_threads];

    for (uint i = 0; i < num_threads; ++i)
    {
      settings[i].mIPAddress = "127.0.0.1";
      settings[i].mPort = 8081;
      strcpy(settings[i].mJsonRpcPath, "/auth");
      settings[i].ComputeJsonRpcPathHash();

      settings[i].mInitCallback = [](HTTPServerSettings* s, uv_loop_t* uvloop)
      {
        PostgresConnectionPool* pool = new PostgresConnectionPool();
        pool->InitializeConnections(
          "host=localhost port=5432 dbname=tn_unyielding user=postgres password=tndb22", 8, uvloop);
        pool->mPingStr = "host=localhost port=5432";

        s->mDBConnection = pool;

        uv_timer_init(uvloop, &s->mDBCheckTimer);
        s->mDBCheckTimer.data = s->mDBConnection;
        uv_timer_start(&s->mDBCheckTimer, &PostgresConnectionPool::CheckConnections_TimerCb, 0,
                       3000);

        // periodic timer to step this server's coroutines
        uv_timer_init(uvloop, &s->mCoroutineTimer);
        s->mCoroutineTimer.data = s;
        uv_timer_start(&s->mCoroutineTimer, &HTTPServerSettings::CoroutineTimerCB, 0, 1);
      };
      settings[i].mShutdownCallback = [](HTTPServerSettings* s, uv_loop_t*)
      {
        uv_close((uv_handle_t*)&s->mCoroutineTimer, nullptr);
        uv_close((uv_handle_t*)&s->mDBCheckTimer, nullptr);
        s->mCoroutines.DestroyAllCoroutines();

        auto pool = (PostgresConnectionPool*)s->mDBConnection;
        if (pool)
        {
          pool->ShutdownConnections();
          delete pool;
        }
        s->mDBConnection = nullptr;
      };

      settings[i].mRpcCallbacks.emplace(Hash("login"), auth_login);
      settings[i].mRpcCallbacks.emplace(Hash("register"), auth_register);

      // restricted endpoint
      auto restricted_func = [](uv_stream_t* client, int msgid, yyjson_val* params,
                                int params_count, yyjson_mut_doc** doc,
                                yyjson_mut_val** result) -> JsonRpcResult
      {
        // clang-format off
        /*
        curl -X POST http://localhost:8081/auth -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "restricted", "params": ["<paste token here>"], "id": 2}'
        ab -n 10000 -c 100 -p test_verify.json -T 'application/json' http://localhost:46857/auth
        */
        // clang-format on
        (void)client;
        (void)msgid;
        if (params_count != 1)
          return kJsonRpcInvalidParams;
        if (yyjson_get_type(yyjson_arr_get(params, 0)) != YYJSON_TYPE_STR)
          return kJsonRpcInvalidParams;

        //@note: if this were a proper web browser request you should retrieve the access token in
        // the http request's Cookie header.

        TIMER_START();
        *doc = yyjson_mut_doc_new(NULL);
        *result = yyjson_mut_obj(*doc);

        //@note: you should be putting the here
        const char* token_str = yyjson_get_str(yyjson_arr_get(params, 0));
        int userid = -1;
        bool expired = false;
        bool verified = VerifySignedJWT(token_str, &userid, &expired);

        if (!verified)
        {
          yyjson_mut_obj_add_str(*doc, *result, "result", "Failed verification");
          return kJsonRpcSuccess;
        }

        if (expired)
        {
          yyjson_mut_obj_add_str(*doc, *result, "result", "Expired");
          return kJsonRpcSuccess;
        }

        // do work..

        // send reply
        yyjson_mut_obj_add_str(*doc, *result, "result", "Success");

        TIMER_END("restricted", false);
        return kJsonRpcSuccess;
      };
      settings[i].mRpcCallbacks.emplace(Hash("restricted"), restricted_func);

      auto stats_func = [](uv_stream_t* client) -> int
      {
        // clang-format off
        /*
        web browser http://localhost:8081/stats
        */
        // clang-format on

        char buffer[8192] = {};
        int len = GenerateStatsHTMLPage(buffer);

        char response[9216] = {};
        int response_len = snprintf(response, sizeof(response),
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Type: text/html\r\n"
                                    "Content-Length: %d\r\n"
                                    "Connection: close\r\n"
                                    "\r\n"
                                    "%s\r\n",
                                    len, buffer);

        // clamp to what was actually written into 'response' to avoid over-reading
        // the stack buffer when snprintf reported a would-be length >= sizeof(response)
        size_t sz = (size_t)response_len + 1;
        if (sz > sizeof(response))
          sz = sizeof(response);
        char* b = (char*)malloc(sz);
        memset(b, 0, sz);
        strncpy(b, response, sz);

        uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
        req->data = b;

        uv_buf_t sendbuf = uv_buf_init(b, sz);
        int result = uv_write(req, client, &sendbuf, 1, common_write_end_cb);
        if (result < 0)
        {
          LOG_WARN("stats: failed to initiate write: %s\n", uv_strerror(result));
          free(b);
          free(req);
        }
        return result;
      };
      settings[i].mPathCallbacks.emplace(Hash("/stats"), stats_func);

      // make thread
      uv_thread_create(&thread[i], http_server_thread_func, &settings[i]);
    }

    for (uint i = 0; i < num_threads; ++i)
      uv_thread_join(&thread[i]);
    delete[] settings;
    delete[] thread;
  }
#endif

  return 0;
}