
#pragma once

#include "prereqs.h"

//@future: SECRET_KEY management, openssl setup
// check https://github.com/ddengster/learning_webdev/tree/master/vuejs_gobackend_login
// for setups:
// var privKeyPath = "./keys/app.rsa"  //openssl genrsa -out app.rsa keysize
// var pubKeyPath = "./keys/app.rsa.pub"   //openssl rsa -in app.rsa -pubout > app.rsa.pub
#define SECRET_KEY "asdasdgerr"

// Generates a JWT. Free the returned string after usage
const char* GenerateSignedJWT(uint userid, long expire_duration = 24 * 60 * 60);

// verifies a signed JWT
bool VerifySignedJWT(const char* token_str, int* userid = nullptr, bool* expired = nullptr);
