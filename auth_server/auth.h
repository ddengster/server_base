
#pragma once

#include "prereqs.h"
#include "logger.h"
#include "network.h"

#define DB_TIMEOUT 2000

// clang-format off
/*
curl -X POST http://localhost:8081/auth -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "login", "params": ["direct", "direct_test", "direct-pass-123"], "id": 1}'
curl -X POST http://localhost:8081/auth -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "login", "params": ["steam", "76561198000000001"], "id": 5}'
ab -n 10000 -c 100 -p test_json/login.json -T 'application/json' http://localhost:8081/auth
*/
// clang-format on
JsonRpcResult auth_login(uv_stream_t* client, int msgid, yyjson_val* params, int params_count,
                         yyjson_mut_doc** doc, yyjson_mut_val** result);

// clang-format off
/*
curl -X POST http://localhost:8081/auth -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "register", "params": ["direct", "direct_test2", "direct@test.com", "direct-pass-1234"], "id": 6}'
curl -X POST http://localhost:8081/auth -H "Content-Type: application/json" -d '{"jsonrpc": "2.0", "method": "register", "params": ["steam", "76561198000000011", "steamer"], "id": 7}'
*/
// clang-format on
JsonRpcResult auth_register(uv_stream_t* client, int msgid, yyjson_val* params, int params_count,
                            yyjson_mut_doc** doc, yyjson_mut_val** result);