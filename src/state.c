#include "internal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

long long en_now_ms(void)
{
    return (long long)time(NULL) * 1000LL;
}

void en_copy_id(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

bool en_streq(const char *left, const char *right)
{
    return left != NULL && right != NULL && strcmp(left, right) == 0;
}

size_t en_path_hop_count(const en_path_t *path)
{
    return path == NULL ? 0 : path->waypoint_count + 1;
}

en_path_t *en_find_path(en_controller_t *controller, const char *path_id)
{
    if (controller == NULL || path_id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < controller->state.path_count; i++) {
        if (en_streq(controller->state.paths[i].path_id, path_id)) {
            return &controller->state.paths[i];
        }
    }
    return NULL;
}

en_path_health_t *en_find_health(en_controller_t *controller, const char *path_id)
{
    if (controller == NULL || path_id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < controller->state.health_count; i++) {
        if (en_streq(controller->state.health[i].path_id, path_id)) {
            return &controller->state.health[i];
        }
    }
    return NULL;
}

en_tunnel_t *en_find_tunnel(en_controller_t *controller, const char *tunnel_id)
{
    if (controller == NULL || tunnel_id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < controller->state.observed_tunnel_count; i++) {
        if (en_streq(controller->state.observed_tunnels[i].tunnel_id, tunnel_id)) {
            return &controller->state.observed_tunnels[i];
        }
    }
    return NULL;
}

en_tunnel_t *en_find_desired_tunnel(en_controller_t *controller, const char *tunnel_id)
{
    if (controller == NULL || tunnel_id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < controller->state.desired_tunnel_count; i++) {
        if (en_streq(controller->state.desired_tunnels[i].tunnel_id, tunnel_id)) {
            return &controller->state.desired_tunnels[i];
        }
    }
    return NULL;
}

const char *en_get_applied_path(en_controller_t *controller, const char *traffic_key)
{
    if (controller == NULL || traffic_key == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < controller->state.applied_count; i++) {
        if (en_streq(controller->state.traffic_keys[i], traffic_key)) {
            return controller->state.applied_paths[i];
        }
    }
    return NULL;
}

void en_set_applied_path(en_controller_t *controller, const char *traffic_key, const char *path_id)
{
    if (controller == NULL || traffic_key == NULL || path_id == NULL) {
        return;
    }
    for (size_t i = 0; i < controller->state.applied_count; i++) {
        if (en_streq(controller->state.traffic_keys[i], traffic_key)) {
            en_copy_id(controller->state.applied_paths[i], sizeof(controller->state.applied_paths[i]), path_id);
            return;
        }
    }
    if (controller->state.applied_count < EN_MAX_CANDIDATES) {
        size_t idx = controller->state.applied_count++;
        en_copy_id(controller->state.traffic_keys[idx], sizeof(controller->state.traffic_keys[idx]), traffic_key);
        en_copy_id(controller->state.applied_paths[idx], sizeof(controller->state.applied_paths[idx]), path_id);
    }
}

void en_make_traffic_key(const en_traffic_selector_t *traffic, char *buf, size_t buf_len)
{
    if (buf_len == 0) {
        return;
    }
    if (traffic == NULL) {
        buf[0] = '\0';
        return;
    }
    snprintf(buf, buf_len, "%s->%s", traffic->source, traffic->destination);
}

const char *en_controller_applied_path(const en_controller_t *controller, const char *traffic_key)
{
    return en_get_applied_path((en_controller_t *)controller, traffic_key);
}

const char *en_health_state_name(en_health_state_t state)
{
    switch (state) {
    case EN_HEALTH_UNKNOWN: return "unknown";
    case EN_HEALTH_HEALTHY: return "healthy";
    case EN_HEALTH_DEGRADED: return "degraded";
    case EN_HEALTH_UNHEALTHY: return "unhealthy";
    case EN_HEALTH_FAILED: return "failed";
    }
    return "invalid";
}

const char *en_transition_state_name(en_transition_state_t state)
{
    switch (state) {
    case EN_TRANSITION_IDLE: return "idle";
    case EN_TRANSITION_CANDIDATE_SELECTED: return "candidate_selected";
    case EN_TRANSITION_PREPARING: return "preparing";
    case EN_TRANSITION_VALIDATING: return "validating";
    case EN_TRANSITION_READY: return "ready";
    case EN_TRANSITION_PAUSING: return "pausing";
    case EN_TRANSITION_DRAINING: return "draining";
    case EN_TRANSITION_SWITCHING: return "switching";
    case EN_TRANSITION_VERIFYING: return "verifying";
    case EN_TRANSITION_COMPLETED: return "completed";
    case EN_TRANSITION_ROLLING_BACK: return "rolling_back";
    case EN_TRANSITION_FAILED: return "failed";
    }
    return "invalid";
}

const char *en_error_code_name(en_error_code_t code)
{
    switch (code) {
    case EN_ERR_NONE: return "none";
    case EN_ERR_TUNNEL_ESTABLISH_TIMEOUT: return "tunnel_establish_timeout";
    case EN_ERR_PATH_VALIDATION_FAILED: return "path_validation_failed";
    case EN_ERR_FORWARDING_UPDATE_FAILED: return "forwarding_update_failed";
    case EN_ERR_STATE_CONFLICT: return "state_conflict";
    case EN_ERR_ROLLBACK_FAILED: return "rollback_failed";
    case EN_ERR_OBSERVATION_TIMEOUT: return "observation_timeout";
    case EN_ERR_INVALID_ARGUMENT: return "invalid_argument";
    case EN_ERR_NOT_FOUND: return "not_found";
    case EN_ERR_NO_CANDIDATE: return "no_candidate";
    }
    return "invalid";
}
