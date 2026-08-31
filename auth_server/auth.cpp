#include "auth.h"
#include "security.h"
#include "yyjson.h"
#include <postgresql/libpq-fe.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "db_pool.h"
#include "coroutine_mgt.h"

enum DbQueryResult
{
  kDbQueryError = -1,
  kDbNoMatch = -2,
};

JsonRpcResult auth_login(uv_stream_t* client, int msgid, yyjson_val* params, int params_count,
                         yyjson_mut_doc** doc, yyjson_mut_val** result)
{
  (void)msgid;
  if (params_count < 2)
    return kJsonRpcInvalidParams;

  const char* type = yyjson_get_str(yyjson_arr_get(params, 0));
  if (!type)
    return kJsonRpcInvalidParams;

  // clang-format off
  /*
  curl -X POST http://localhost:8081/auth -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "login", "params": ["direct", "direct_test", "direct-pass-123"], "id": 1}'
  curl -X POST http://localhost:8081/auth -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "login", "params": ["steam", "76561198000000001"], "id": 5}'
  ab -n 10000 -c 100 -p test_json/login.json -T 'application/json' http://localhost:8081/auth
  */
  // clang-format on

  struct AuthLoginData
  {
    uv_stream_t* mClient = nullptr;
    String mUsername;
    String mPassword;
    String mSourceId;
    int mId = -1;
    bool mDirect = false;
  };
  auto auth_login_coro = [](mco_coro* coro)
  {
    TIMER_START();
    auto data = (AuthLoginData*)coro->user_data;
    if (uv_is_closing((const uv_handle_t*)data->mClient))
    {
      return;
    }

    auto server_settings = (HTTPServerSettings*)data->mClient->data;
    auto dbpool = (PostgresConnectionPool*)server_settings->mDBConnection;

    DBConnectionCtx* db_ctx = yield_until_db_ctx_or_time_limit(dbpool, data->mClient, DB_TIMEOUT);
    if (!db_ctx)
    {
      LOG_WARN("db_ctx null because stream cancelled or timelimit expired");
      send_jsonrpc_error(data->mClient, kJsonRpcServerError, "Aborted", &data->mId);
      return;
    }

    PGconn* conn = db_ctx->mConn;
    const char* query = "SELECT user_id FROM users "
                        "WHERE source = 'direct' "
                        "  AND username = $1 "
                        "  AND password_hash = crypt($2, password_hash)";
    const char* values[2] = {data->mUsername.c_str(), data->mPassword.c_str()};
    uint paramcount = 2;

    if (!data->mDirect)
    {
      /*
      // indirect, for something like steam/oauth, etc.
      query = "SELECT user_id FROM users "
              "WHERE source = 'steam' AND source_user_id = $1";

      values[0] = {data->mSourceId.c_str()};
      paramcount = 1;
      */
    }

    int res = PQsendQueryParams(conn, query, paramcount, NULL, values, NULL, NULL, 0);
    if (res == 0)
    {
      LOG_WARN("auth_login PQsendQueryParams failed: %s", PQerrorMessage(conn));
      dbpool->ReleaseDBConn(db_ctx);
      send_jsonrpc_error(data->mClient, kJsonRpcServerError, "Query failed #1", &data->mId);
      return;
    }

    start_poll_and_yield_until_polldata(db_ctx);
    if (db_ctx->mTerminate)
    {
      LOG_WARN("auth_login DB polling failed");
      dbpool->ReleaseDBConn(db_ctx);
      send_jsonrpc_error(data->mClient, kJsonRpcServerError, "Query failed #2", &data->mId);
      return;
    }

    uint user_id = UINT_MAX;
    PGresult* pgres = nullptr;
    while ((pgres = PQgetResult(conn)) != nullptr)
    {
      if (PQresultStatus(pgres) == PGRES_TUPLES_OK)
      {
        int rows = PQntuples(pgres);
        if (PQntuples(pgres) == 1)
          user_id = (uint)strtoll(PQgetvalue(pgres, 0, 0), nullptr, 10);
      }
      else if (PQresultStatus(pgres) == PGRES_FATAL_ERROR)
      {
        LOG_WARN("auth_login PQresultStatus failed: %s", PQerrorMessage(conn));
        dbpool->ReleaseDBConn(db_ctx);
        send_jsonrpc_error(data->mClient, kJsonRpcServerError, "DB Async query failed", &data->mId);

        PQclear(pgres);
        return;
      }
      PQclear(pgres);
    }

    // issue a JWT Token
    if (uv_is_writable(data->mClient))
    {
      if (user_id < UINT_MAX)
      {
        const char* access_token = GenerateSignedJWT((uint)user_id);
        if (access_token)
        {
          yyjson_mut_doc* result_doc = yyjson_mut_doc_new(NULL);
          yyjson_mut_val* result = yyjson_mut_obj(result_doc);

          yyjson_mut_obj_add_str(result_doc, result, "access_token", access_token);
          yyjson_mut_obj_add_str(result_doc, result, "token_type", "Bearer");

          //@note: if this were a proper web browser request you put the access token in the
          // http response Set-Cookie header.
          send_jsonrpc_response(data->mClient, &data->mId, result_doc, result);

          yyjson_mut_doc_free(result_doc);
        }
        // free the string returned by GenerateSignedJWT (allocated via jwt_encode_str)
        free((void*)access_token);
      }
      else
      {
        // failed login
        send_jsonrpc_error(data->mClient, kJsonRpcInvalidParams, "Login failed", &data->mId);
      }

      // async update_last_login
      if (user_id < UINT_MAX)
      {
        char user_id_str[32] = {};
        snprintf(user_id_str, sizeof(user_id_str), "%u", user_id);

        const char* update_query = "UPDATE users SET last_login = now() WHERE user_id = $1::bigint";
        const char* update_values[1] = {user_id_str};

        PQsendQueryParamsIgnoreResults(db_ctx, conn, update_query, 1, NULL, update_values, NULL,
                                       NULL, 0);
        if (db_ctx->mTerminate)
        {
          LOG_WARN("auth_login DB update failed");
          dbpool->ReleaseDBConn(db_ctx);
          send_jsonrpc_error(data->mClient, kJsonRpcServerError, "Query failed #3", &data->mId);
          return;
        }
      }

      dbpool->ReleaseDBConn(db_ctx);
    }
    else
    {
      dbpool->ReleaseDBConn(db_ctx);
    }

    TIMER_END("login", true);
  };

  AuthLoginData* data = new AuthLoginData();
  data->mClient = client;
  data->mId = msgid;

  if (strequal(type, "direct"))
  {
    //["direct", "username", "password"]
    data->mUsername = yyjson_get_str(yyjson_arr_get(params, 1));
    data->mPassword = yyjson_get_str(yyjson_arr_get(params, 2));
    data->mDirect = true;
  }
  // else if (strequal(type, "steam")) // other source
  else
  {
    delete data;
    return kJsonRpcInvalidParams;
  }

  auto server_settings = (HTTPServerSettings*)client->data;
  server_settings->mCoroutines.CreateUntrackedCoroutine(auth_login_coro, client, data,
                                                        [](void* p) { delete (AuthLoginData*)p; });

  return kJsonRpcInternalPending;
}

JsonRpcResult auth_register(uv_stream_t* client, int msgid, yyjson_val* params, int params_count,
                            yyjson_mut_doc** doc, yyjson_mut_val** result)
{
  (void)doc;
  (void)result;
  if (params_count < 2)
    return kJsonRpcInvalidParams;

  const char* type = yyjson_get_str(yyjson_arr_get(params, 0));
  if (!type)
    return kJsonRpcInvalidParams;

  // clang-format off
  /*
  curl -X POST http://localhost:8081/auth -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "register", "params": ["direct", "direct_test", "direct@test.com", "direct-pass-123"], "id": 6}'
  curl -X POST http://localhost:8081/auth -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "register", "params": ["steam", "76561198000000001", "steamer"], "id": 7}'
  */
  // clang-format on

  TIMER_START();

  struct AuthRegisterData
  {
    uv_stream_t* mClient = nullptr;
    String mUsername;
    String mEmail;
    String mPassword;
    String mSourceId;
    int mId = -1;
    bool mDirect = false;
  };
  auto register_coro = [](mco_coro* coro)
  {
    auto data = (AuthRegisterData*)coro->user_data;
    if (uv_is_closing((const uv_handle_t*)data->mClient))
    {
      return;
    }
    auto server_settings = (HTTPServerSettings*)data->mClient->data;
    auto dbpool = (PostgresConnectionPool*)server_settings->mDBConnection;

    DBConnectionCtx* db_ctx = yield_until_db_ctx_or_time_limit(dbpool, data->mClient, 500);
    if (!db_ctx)
    {
      LOG_WARN("db_ctx null because stream cancelled or timelimit expired");
      send_jsonrpc_error(data->mClient, kJsonRpcServerError, "Aborted", &data->mId);
      return;
    }

    PGconn* conn = db_ctx->mConn;

    // password stored as a pgcrypto bcrypt hash
    const char* query =
      "INSERT INTO users (source, source_user_id, username, email, password_hash, roles) "
      "VALUES ('direct', NULL, $1, $2, crypt($3, gen_salt('bf')), ARRAY['player']) "
      "ON CONFLICT (username) WHERE source = 'direct' DO NOTHING "
      "RETURNING user_id";
    const char* values[3] = {data->mUsername.c_str(),
                             data->mEmail.length() ? data->mEmail.c_str() : nullptr,
                             data->mPassword.c_str()};
    uint paramcount = 3;

    if (!data->mDirect)
    {
      /*
      query = "INSERT INTO users (source, source_user_id, username, roles) "
              "VALUES ('source', $1, NULL, ARRAY['player']) "
              "ON CONFLICT (source, source_user_id) DO NOTHING "
              "RETURNING user_id";
      values[0] = {data->mSourceId.c_str()};
      paramcount = 1;*/
    }

    int sent = PQsendQueryParams(conn, query, paramcount, NULL, values, NULL, NULL, 0);
    if (sent == 0)
    {
      LOG_WARN("auth_register PQsendQueryParams failed: %s", PQerrorMessage(conn));
      dbpool->ReleaseDBConn(db_ctx);
      send_jsonrpc_error(data->mClient, kJsonRpcServerError, "Query failed #1", &data->mId);
      return;
    }

    start_poll_and_yield_until_polldata(db_ctx);
    if (db_ctx->mTerminate)
    {
      LOG_WARN("auth_login DB polling failed");
      dbpool->ReleaseDBConn(db_ctx);
      send_jsonrpc_error(data->mClient, kJsonRpcServerError, "Query failed #2", &data->mId);
      return;
    }

    long long new_user_id = kDbNoMatch;
    PGresult* pgres = nullptr;
    while ((pgres = PQgetResult(conn)) != nullptr)
    {
      if (PQresultStatus(pgres) == PGRES_TUPLES_OK)
      {
        if (PQntuples(pgres) == 1)
          new_user_id = strtoll(PQgetvalue(pgres, 0, 0), NULL, 10);
      }
      else if (PQresultStatus(pgres) == PGRES_FATAL_ERROR)
      {
        LOG_WARN("auth_register PQresultStatus failed: %s", PQerrorMessage(conn));
        new_user_id = kDbQueryError;
      }
      PQclear(pgres);
    }

    dbpool->ReleaseDBConn(db_ctx);

    if (uv_is_writable(data->mClient))
    {
      yyjson_mut_doc* result_doc = yyjson_mut_doc_new(NULL);
      yyjson_mut_val* result = yyjson_mut_obj(result_doc);

      // database error
      if (new_user_id == kDbQueryError)
      {
        send_jsonrpc_error(data->mClient, kJsonRpcServerError, "DB Async query failed", &data->mId);
      }
      else
      {
        // already registered, insert returned no row
        if (new_user_id == kDbNoMatch)
          yyjson_mut_obj_add_str(result_doc, result, "result", "User already exists");
        else
          yyjson_mut_obj_add_uint(result_doc, result, "user_id", (uint64_t)new_user_id);

        send_jsonrpc_response(data->mClient, &data->mId, result_doc, result);
      }

      yyjson_mut_doc_free(result_doc);
    }
  };

  AuthRegisterData* data = new AuthRegisterData();
  data->mClient = client;
  data->mId = msgid;

  if (strequal(type, "direct"))
  {
    //["direct", "username", "email", "passwordhash"]
    if (params_count != 4)
    {
      delete data;
      return kJsonRpcInvalidParams;
    }

    const char* username = yyjson_get_str(yyjson_arr_get(params, 1));
    const char* email = yyjson_get_str(yyjson_arr_get(params, 2));
    const char* password = yyjson_get_str(yyjson_arr_get(params, 3));
    if (!username || username[0] == '\0' || !password || password[0] == '\0')
    {
      delete data;
      return kJsonRpcInvalidParams;
    }

    data->mUsername = username;
    data->mEmail = email ? email : "";
    data->mPassword = password;
    data->mDirect = true;
  }
  else if (strequal(type, "source"))
  {
    //["source", "<sourceid>", "username"]
    if (params_count != 3)
    {
      delete data;
      return kJsonRpcInvalidParams;
    }

    const char* sourceid = yyjson_get_str(yyjson_arr_get(params, 1));
    const char* username = yyjson_get_str(yyjson_arr_get(params, 2));

    // do extra checks, double confirm if sourceid is valid via sourceid's servers,
    // usually through some https api check
    // they'll usually have a way to do this confirmation
    data->mSourceId = sourceid;
    data->mUsername = username;
    data->mDirect = false;
  }
  else
  {
    delete data;
    return kJsonRpcInvalidParams;
  }

  auto server_settings = (HTTPServerSettings*)client->data;
  server_settings->mCoroutines.CreateUntrackedCoroutine(register_coro, client, data,
                                                        [](void* p) { delete (AuthRegisterData*)p; });

  TIMER_END("register", false);
  return kJsonRpcInternalPending;
}