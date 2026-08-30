#include <assert.h>
#include <string.h>

#include "official_auth_store.h"

static netease_token_set_t tokens_with_times(int64_t now_ms,
                                             int64_t access_delta_ms,
                                             int64_t refresh_delta_ms)
{
    netease_token_set_t tokens = {0};
    strcpy(tokens.access_token, "access-token");
    strcpy(tokens.refresh_token, "refresh-token");
    tokens.access_expires_at_ms = now_ms + access_delta_ms;
    tokens.refresh_expires_at_ms = now_ms + refresh_delta_ms;
    return tokens;
}

int main(void)
{
    const int64_t now_ms = 1724918400000LL;

    assert(official_auth_decide(NULL, now_ms, false) == OFFICIAL_AUTH_LOGIN_QR);

    netease_token_set_t tokens = tokens_with_times(now_ms, 10 * 60 * 1000, 30 * 24 * 60 * 60 * 1000LL);
    assert(official_auth_decide(&tokens, now_ms, false) == OFFICIAL_AUTH_USE_ACCESS);

    tokens = tokens_with_times(now_ms, 5 * 60 * 1000, 30 * 24 * 60 * 60 * 1000LL);
    assert(official_auth_decide(&tokens, now_ms, false) == OFFICIAL_AUTH_REFRESH);

    tokens = tokens_with_times(now_ms, 10 * 60 * 1000, -1);
    assert(official_auth_decide(&tokens, now_ms, false) == OFFICIAL_AUTH_LOGIN_QR);

    tokens = tokens_with_times(now_ms, -1, 30 * 24 * 60 * 60 * 1000LL);
    assert(official_auth_decide(&tokens, now_ms, true) == OFFICIAL_AUTH_CLEAR_AND_LOGIN_QR);

    tokens = tokens_with_times(now_ms, 10 * 60 * 1000, 30 * 24 * 60 * 60 * 1000LL);
    tokens.access_token[0] = '\0';
    assert(official_auth_decide(&tokens, now_ms, false) == OFFICIAL_AUTH_LOGIN_QR);

    return 0;
}
