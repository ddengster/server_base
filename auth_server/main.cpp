
#include "prereqs.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>
#include <cstring>

#include <uv.h>

#include "os_utils.h"
#include "logger.h"
#include "network.h"
#include "yyjson.h"
#include "server_stats.h"

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
      strcpy(settings[i].mJsonRpcPath, "/login");
      settings[i].ComputeJsonRpcPathHash();

      // must put this behind a reverse proxy that handles SSL and rate limits for you
      auto login_func = [](uv_stream_t* client, int msgid, yyjson_val* params, int params_count,
                           yyjson_mut_doc** doc, yyjson_mut_val** result) -> JsonRpcResult
      {
        // clang-format off
        /*
        curl -X POST http://localhost:8081/login -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "login", "params": ["hashed_pwd"], "id": 1}'
        */
        // clang-format on
        (void)client;
        (void)msgid;
        if (params_count != 1)
          return kJsonRpcInvalidParams;
        if (yyjson_get_type(yyjson_arr_get(params, 0)) != YYJSON_TYPE_STR)
          return kJsonRpcInvalidParams;

        TIMER_START();

        // IMPLEMENT THIS: check with the database for hashed pwd
        bool hashed_pwd_verified = true;
        uint userid = 12345;  // retreived from db

        //  issue a JWT Token
        if (!hashed_pwd_verified)
          return kJsonRpcInternalError;

        *doc = yyjson_mut_doc_new(NULL);
        *result = yyjson_mut_obj(*doc);

        const char* access_token = nullptr;

        yyjson_mut_obj_add_str(*doc, *result, "access_token", access_token);
        yyjson_mut_obj_add_str(*doc, *result, "token_type", "Bearer");
        yyjson_mut_obj_add_double(*doc, *result, "expires_in", 60 * 60 * 24);  // 24h


        TIMER_END("login", false);

        return kJsonRpcSuccess;
      };
      settings[i].mRpcCallbacks.emplace(Hash("login"), login_func);

      // restricted endpoint
      auto restricted_func = [](uv_stream_t* client, int msgid, yyjson_val* params,
                                int params_count, yyjson_mut_doc** doc,
                                yyjson_mut_val** result) -> JsonRpcResult
      {
        // clang-format off
        /*
        curl -X POST http://localhost:8081/restricted -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "login", "params": [], "id": 2}'
        */
        // clang-format on
        (void)client;
        (void)msgid;
        if (params_count != 0)
          return kJsonRpcInvalidParams;


        return kJsonRpcSuccess;
      };
      settings[i].mRpcCallbacks.emplace(Hash("restricted"), restricted_func);

      // make thread
      uv_thread_create(&thread[i], http_server_thread_func, &settings[i]);
    }

    for (int i = 0; i < 4; ++i)
      uv_thread_join(&thread[i]);
    delete[] settings;
    delete[] thread;
  }
#endif
  return 0;
}