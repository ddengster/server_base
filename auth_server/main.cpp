
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
#include "security.h"

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
      strcpy(settings[i].mJsonRpcPath, "/auth");
      settings[i].ComputeJsonRpcPathHash();

      // must put this behind a reverse proxy that handles SSL and rate limits for you
      auto login_func = [](uv_stream_t* client, int msgid, yyjson_val* params, int params_count,
                           yyjson_mut_doc** doc, yyjson_mut_val** result) -> JsonRpcResult
      {
        // clang-format off
        /*
        curl -X POST http://localhost:8081/auth -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "login", "params": ["hashed_pwd"], "id": 1}'
        */
        // clang-format on
        (void)client;
        (void)msgid;
        if (params_count != 1)
          return kJsonRpcInvalidParams;
        if (yyjson_get_type(yyjson_arr_get(params, 0)) != YYJSON_TYPE_STR)
          return kJsonRpcInvalidParams;

        TIMER_START();

        // IMPLEMENT THIS: check with the database for user + hashed pwd
        bool user_access_granted = true;
        uint userid = 12345;  // retreived from db

        //  issue a JWT Token
        if (!user_access_granted)
          return kJsonRpcInternalError;

        const char* access_token = GenerateSignedJWT(userid);
        if (!access_token)
          return kJsonRpcInternalError;
#ifdef NETWORK_DBG
        LOG_INFO(access_token);
#endif

        *doc = yyjson_mut_doc_new(NULL);
        *result = yyjson_mut_obj(*doc);

        yyjson_mut_obj_add_str(*doc, *result, "access_token", access_token);
        yyjson_mut_obj_add_str(*doc, *result, "token_type", "Bearer");
        //@note: if this were a proper web browser request you put the access token in the
        // http response Set-Cookie header.

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
        curl -X POST http://localhost:8081/auth -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "restricted", "params": ["<paste token here>"], "id": 2}'
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

        TIMER_END("restricted", true);
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

        uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
        req->data = client;

        size_t sz = (size_t)response_len + 1;
        char* b = (char*)malloc(sz);
        memset(b, 0, sz);
        strncpy(b, response, sz);

        uv_buf_t sendbuf = uv_buf_init(b, sz);
        int result = uv_write(req, client, &sendbuf, 1, common_write_end_cb);
        if (result < 0)
        {
          LOG_WARN("stats: failed to initiate write: %s\n", uv_strerror(result));
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