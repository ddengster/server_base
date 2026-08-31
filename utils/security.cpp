
#include "security.h"
#include <jwt.h>
#include <cstring>
#include "logger.h"

const char* GenerateSignedJWT(uint userid, long expire_duration)
{
  char* token_str = nullptr;
  jwt_t* jwt = nullptr;

  int ret = jwt_new(&jwt);

  // JWT_ALG_HS256 uses a symmetric key
  ret = jwt_set_alg(jwt, JWT_ALG_HS256, (unsigned char*)SECRET_KEY, strlen(SECRET_KEY));
  if (ret != 0)
  {
    LOG_WARN("Failed to set algorithm.\n");
    jwt_free(jwt);
    return nullptr;
  }
  // prompt "JWT grants" for a list of standard jwt grants
  jwt_add_grant_int(jwt, "userid", userid);
  jwt_add_grant_int(jwt, "iat", (long)time(NULL));
  jwt_add_grant_int(jwt, "exp", expire_duration);  // 24h

  token_str = jwt_encode_str(jwt);

  // Free the working object memory
  jwt_free(jwt);

  return token_str;
}

bool VerifySignedJWT(const char* token_str, int* userid, bool* expired)
{
  jwt_t* jwt = nullptr;
  int ret = jwt_decode(&jwt, token_str, (unsigned char*)SECRET_KEY, strlen(SECRET_KEY));
  if (ret != 0 || jwt == NULL)
  {
    printf("Token verification FAILED! Signature invalid or token tampered with.\n");
    return false;
  }
  if (userid)
    *userid = jwt_get_grant_int(jwt, "userid");

  if (expired)
  {
    long iat = jwt_get_grant_int(jwt, "iat");
    long exp = jwt_get_grant_int(jwt, "exp");
    long expire_time = iat + exp;
    *expired = ((long)time(NULL) >= expire_time) ? true : false;
  }

  // Free the working object memory
  jwt_free(jwt);
  return true;
}