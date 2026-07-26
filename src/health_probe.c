#include "internal.h"
#include "eventnet/mock_adapters.h"

#include <stdio.h>
#include <string.h>

static en_error_code_t validate_path(void *ctx, const en_path_t *path, en_path_health_t *health)
{
    en_health_probe_mock_t *mock = ctx;
    if (path == NULL || health == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    if (mock != NULL) {
        for (size_t i = 0; i < mock->override_count; i++) {
            if (strcmp(mock->overrides[i].path_id, path->path_id) == 0) {
                *health = mock->overrides[i];
                health->last_updated_ms = en_now_ms();
                return EN_ERR_NONE;
            }
        }
    }
    snprintf(health->path_id, sizeof(health->path_id), "%s", path->path_id);
    health->state = EN_HEALTH_HEALTHY;
    health->rtt_ms = 10.0 * (double)en_path_hop_count(path);
    health->packet_loss_percent = 0.0;
    health->jitter_ms = 1.0;
    health->consecutive_failures = 0;
    health->consecutive_successes = 3;
    health->last_updated_ms = en_now_ms();
    return EN_ERR_NONE;
}

en_health_probe_t en_health_probe_mock_adapter(en_health_probe_mock_t *mock)
{
    en_health_probe_t adapter = {
        .validate_path = validate_path,
        .ctx = mock,
    };
    return adapter;
}

void en_health_probe_mock_set(en_health_probe_mock_t *mock, en_path_health_t health)
{
    if (mock == NULL) {
        return;
    }
    for (size_t i = 0; i < mock->override_count; i++) {
        if (strcmp(mock->overrides[i].path_id, health.path_id) == 0) {
            mock->overrides[i] = health;
            return;
        }
    }
    if (mock->override_count < EN_MAX_PATHS) {
        mock->overrides[mock->override_count++] = health;
    }
}
