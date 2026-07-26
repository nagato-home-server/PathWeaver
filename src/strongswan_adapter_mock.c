#include "eventnet/mock_adapters.h"

static en_error_code_t ensure_tunnel(void *ctx, const en_tunnel_t *desired, en_tunnel_t *observed)
{
    (void)ctx;
    if (desired == NULL || observed == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    *observed = *desired;
    observed->state = EN_TUNNEL_ESTABLISHED;
    observed->health = EN_HEALTH_HEALTHY;
    return EN_ERR_NONE;
}

static en_error_code_t remove_tunnel(void *ctx, const en_tunnel_t *desired, en_tunnel_t *observed)
{
    (void)ctx;
    if (desired == NULL || observed == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    *observed = *desired;
    observed->state = EN_TUNNEL_ABSENT;
    observed->health = EN_HEALTH_UNKNOWN;
    return EN_ERR_NONE;
}

en_strongswan_adapter_t en_strongswan_mock_adapter(void)
{
    en_strongswan_adapter_t adapter = {
        .ensure_tunnel = ensure_tunnel,
        .remove_tunnel = remove_tunnel,
        .ctx = NULL,
    };
    return adapter;
}
