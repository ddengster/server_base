
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

const char* gProcessName = "sample_http";

int main(int argc, char* argv[])
{
  printf("process: sample_http, compile date: %s %s\n", __DATE__, __TIME__);
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
      strcpy(settings[i].mPath, "/api/v0");

      auto subtract_func = [](uv_stream_t* client, yyjson_val* params, int params_count,
                              yyjson_mut_doc* doc, yyjson_mut_val* result) -> JsonRpcResult
      {
        // clang-format off
        /*
        curl -X POST http://localhost:8081/api/v0 -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "subtract", "params": [42, 23], "id": 1}'
        */
        // clang-format on
        (void)client;
        if (params_count != 2)
          return kJsonRpcInvalidParams;
        if (yyjson_get_type(yyjson_arr_get(params, 0)) != YYJSON_TYPE_NUM ||
            yyjson_get_type(yyjson_arr_get(params, 1)) != YYJSON_TYPE_NUM)
          return kJsonRpcInvalidParams;

        double param1 = yyjson_get_num(yyjson_arr_get(params, 0));
        double param2 = yyjson_get_num(yyjson_arr_get(params, 1));
        LOG_INFO("%.2f, %.2f", param1, param2);

        yyjson_mut_obj_add_double(doc, result, "difference", param1 - param2);

        return kJsonRpcSuccess;
      };
      settings[i].mRpcCallbacks.emplace(Hash("subtract"), subtract_func);

      auto add_func = [](uv_stream_t* client, yyjson_val* params, int params_count,
                         yyjson_mut_doc* doc, yyjson_mut_val* result) -> JsonRpcResult
      {
        (void)client;
        if (params_count != 2)
          return kJsonRpcInvalidParams;
        if (yyjson_get_type(yyjson_arr_get(params, 0)) != YYJSON_TYPE_NUM ||
            yyjson_get_type(yyjson_arr_get(params, 1)) != YYJSON_TYPE_NUM)
          return kJsonRpcInvalidParams;

        double param1 = yyjson_get_num(yyjson_arr_get(params, 0));
        double param2 = yyjson_get_num(yyjson_arr_get(params, 1));
        LOG_INFO("%.2f, %.2f", param1, param2);

        yyjson_mut_obj_add_double(doc, result, "difference", param1 - param2);
        yyjson_mut_obj_add_double(doc, result, "sum", param1 + param2);

        return kJsonRpcSuccess;
      };
      settings[i].mRpcCallbacks.emplace(Hash("add"), add_func);


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