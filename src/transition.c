#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L
#endif

#include "internal.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include <stdio.h>

static void sleep_ms(int milliseconds);
static en_error_code_t transition_prepare(en_controller_t *controller, en_path_t *path);
static en_error_code_t transition_ready(en_controller_t *controller, en_path_t *path);
static en_error_code_t transition_commit(en_controller_t *controller, const en_intent_t *intent, en_path_t *target_path, const char *traffic_key);
static en_error_code_t transition_confirm(en_controller_t *controller, en_path_t *target_path, const char *traffic_key);
static en_error_code_t rollback(en_controller_t *controller, const char *traffic_key, const char *rollback_path_id);

en_error_code_t en_transition_path(en_controller_t *controller, const en_intent_t *intent, en_path_t *target_path)
{
    if (controller == NULL || intent == NULL || target_path == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }

    char traffic_key[EN_MAX_ID_LEN * 2] = {0};
    en_make_traffic_key(&intent->traffic, traffic_key, sizeof(traffic_key));
    const char *current_path = en_get_applied_path(controller, traffic_key);
    char rollback_path_id[EN_MAX_ID_LEN] = {0};
    en_copy_id(rollback_path_id, sizeof(rollback_path_id), current_path);

    controller->state.transition_state = EN_TRANSITION_PREPARING;
    en_audit_append(controller, "TRANSITION_STARTED", "preparing target path", target_path->path_id);

    en_error_code_t err = transition_prepare(controller, target_path);
    if (err != EN_ERR_NONE) {
        en_error_append(controller, err, "target path preparation failed");
        rollback(controller, traffic_key, rollback_path_id);
        return err;
    }

    err = transition_ready(controller, target_path);
    if (err != EN_ERR_NONE) {
        en_error_append(controller, err, "target path validation failed");
        rollback(controller, traffic_key, rollback_path_id);
        return err;
    }

    err = transition_commit(controller, intent, target_path, traffic_key);
    if (err != EN_ERR_NONE) {
        en_error_append(controller, EN_ERR_FORWARDING_UPDATE_FAILED, "forwarding update failed");
        rollback(controller, traffic_key, rollback_path_id);
        return EN_ERR_FORWARDING_UPDATE_FAILED;
    }

    err = transition_confirm(controller, target_path, traffic_key);
    if (err != EN_ERR_NONE) {
        rollback(controller, traffic_key, rollback_path_id);
        return err;
    }

    controller->state.transition_state = EN_TRANSITION_COMPLETED;
    en_audit_append(controller, "TRANSITION_COMPLETED", "target path is active", target_path->path_id);
    controller->state.transition_state = EN_TRANSITION_IDLE;
    return EN_ERR_NONE;
}

static en_error_code_t transition_prepare(en_controller_t *controller, en_path_t *path)
{
    path->operational_state = EN_PATH_PREPARING;
    en_audit_append(controller, "PREPARE", "prepare target tunnels", path->path_id);
    for (size_t i = 0; i < path->segment_count; i++) {
        en_segment_t *segment = &path->segments[i];
        en_tunnel_t desired = {0};
        en_tunnel_t *configured = en_find_desired_tunnel(controller, segment->tunnel_id);
        if (configured != NULL) {
            desired = *configured;
        } else {
            en_copy_id(desired.tunnel_id, sizeof(desired.tunnel_id), segment->tunnel_id);
            en_copy_id(desired.local_node, sizeof(desired.local_node), segment->from_node);
            en_copy_id(desired.remote_node, sizeof(desired.remote_node), segment->to_node);
            en_copy_id(desired.protocol, sizeof(desired.protocol), "ipsec");
            desired.state = EN_TUNNEL_CONFIGURED;
            desired.health = EN_HEALTH_UNKNOWN;
        }

        en_tunnel_t observed = {0};
        en_error_code_t err = controller->strongswan.ensure_tunnel(controller->strongswan.ctx, &desired, &observed);
        if (err != EN_ERR_NONE) {
            return err;
        }

        en_tunnel_t *existing = en_find_tunnel(controller, observed.tunnel_id);
        if (existing != NULL) {
            *existing = observed;
        } else if (controller->state.observed_tunnel_count < EN_MAX_TUNNELS) {
            controller->state.observed_tunnels[controller->state.observed_tunnel_count++] = observed;
        } else {
            return EN_ERR_INVALID_ARGUMENT;
        }
    }
    path->operational_state = EN_PATH_READY;
    return EN_ERR_NONE;
}

static en_error_code_t transition_ready(en_controller_t *controller, en_path_t *path)
{
    controller->state.transition_state = EN_TRANSITION_VALIDATING;
    en_audit_append(controller, "READY", "validate target path", path->path_id);
    if (controller->health_probe.validate_path == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    en_path_health_t health = {0};
    en_error_code_t err = controller->health_probe.validate_path(controller->health_probe.ctx, path, &health);
    if (err != EN_ERR_NONE) {
        return err;
    }
    en_path_health_t *existing = en_find_health(controller, health.path_id);
    if (existing != NULL) {
        *existing = health;
    } else if (controller->state.health_count < EN_MAX_PATHS) {
        controller->state.health[controller->state.health_count++] = health;
    }
    if (health.state != EN_HEALTH_HEALTHY && health.state != EN_HEALTH_DEGRADED) {
        return EN_ERR_PATH_VALIDATION_FAILED;
    }
    controller->state.transition_state = EN_TRANSITION_READY;
    return EN_ERR_NONE;
}

static en_error_code_t transition_commit(en_controller_t *controller, const en_intent_t *intent, en_path_t *target_path, const char *traffic_key)
{
    en_audit_append(controller, "COMMIT", "commit forwarding switch", target_path->path_id);
    if (intent->transition.strategy == EN_TRANSITION_GRACEFUL) {
        controller->state.transition_state = EN_TRANSITION_PAUSING;
        sleep_ms(intent->transition.max_pause_ms);
        controller->state.transition_state = EN_TRANSITION_DRAINING;
        target_path->operational_state = EN_PATH_DRAINING;
        sleep_ms(intent->transition.drain_timeout_ms);
    } else if (intent->transition.strategy == EN_TRANSITION_FLOW_PRESERVE) {
        en_error_append(controller, EN_ERR_STATE_CONFLICT, "flow-preserve is reserved for stage 2");
        return EN_ERR_STATE_CONFLICT;
    }

    controller->state.transition_state = EN_TRANSITION_SWITCHING;
    if (controller->vpp.install_path == NULL) {
        en_error_append(controller, EN_ERR_INVALID_ARGUMENT, "vpp adapter is missing install_path");
        return EN_ERR_INVALID_ARGUMENT;
    }
    en_error_code_t err = controller->vpp.install_path(controller->vpp.ctx, traffic_key, target_path);
    if (err != EN_ERR_NONE) {
        en_error_append(controller, EN_ERR_FORWARDING_UPDATE_FAILED, "forwarding update failed");
        return EN_ERR_FORWARDING_UPDATE_FAILED;
    }
    en_set_applied_path(controller, traffic_key, target_path->path_id);
    target_path->operational_state = EN_PATH_ACTIVE;
    return EN_ERR_NONE;
}

static en_error_code_t transition_confirm(en_controller_t *controller, en_path_t *target_path, const char *traffic_key)
{
    controller->state.transition_state = EN_TRANSITION_VERIFYING;
    en_audit_append(controller, "CONFIRM", "confirm forwarding state", target_path->path_id);
    if (controller->vpp.active_path == NULL) {
        return EN_ERR_NONE;
    }
    const char *active = controller->vpp.active_path(controller->vpp.ctx, traffic_key);
    if (!en_streq(active, target_path->path_id)) {
        en_error_append(controller, EN_ERR_FORWARDING_UPDATE_FAILED, "forwarding verification failed");
        return EN_ERR_FORWARDING_UPDATE_FAILED;
    }
    return EN_ERR_NONE;
}

static en_error_code_t rollback(en_controller_t *controller, const char *traffic_key, const char *rollback_path_id)
{
    controller->state.transition_state = EN_TRANSITION_ROLLING_BACK;
    if (rollback_path_id == NULL || rollback_path_id[0] == '\0') {
        en_audit_append(controller, "ROLLBACK_SKIPPED", "no previous path exists", "");
        controller->state.transition_state = EN_TRANSITION_FAILED;
        return EN_ERR_ROLLBACK_FAILED;
    }
    en_set_applied_path(controller, traffic_key, rollback_path_id);
    en_audit_append(controller, "ROLLBACK_COMPLETED", "previous path restored", rollback_path_id);
    controller->state.transition_state = EN_TRANSITION_IDLE;
    return EN_ERR_NONE;
}

static void sleep_ms(int milliseconds)
{
    if (milliseconds <= 0) {
        return;
    }
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}
