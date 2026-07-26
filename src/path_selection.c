#include "internal.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

static bool waypoint_contains(char waypoints[EN_MAX_WAYPOINTS][EN_MAX_ID_LEN], size_t count, const char *needle);
static bool exclusion_reason(const en_path_t *path, const en_path_health_t *health, const en_path_constraints_t *constraints, char *reason, size_t reason_len);
static int compare_paths(const en_path_t *left, const en_path_t *right, const en_path_health_t *left_health, const en_path_health_t *right_health, const en_path_selection_t *selection);
static double metric_value(const en_path_t *path, const en_path_health_t *health, en_comparison_key_t key);

en_error_code_t en_select_path(en_controller_t *controller, const en_intent_t *intent, en_selection_result_t *result)
{
    if (controller == NULL || intent == NULL || result == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));

    const en_path_selection_t *selection = &intent->path_selection;
    if (selection->mode == EN_SELECT_EXPLICIT) {
        en_path_t *path = en_find_path(controller, selection->path_id);
        if (path == NULL) {
            return EN_ERR_NOT_FOUND;
        }
        char reason[128] = {0};
        if (exclusion_reason(path, en_find_health(controller, path->path_id), &selection->constraints, reason, sizeof(reason))) {
            en_copy_id(result->excluded_path_ids[0], sizeof(result->excluded_path_ids[0]), path->path_id);
            snprintf(result->excluded_reasons[0], sizeof(result->excluded_reasons[0]), "%s", reason);
            result->excluded_count = 1;
            return EN_ERR_NO_CANDIDATE;
        }
        en_copy_id(result->selected_path, sizeof(result->selected_path), path->path_id);
        en_copy_id(result->candidates[0], sizeof(result->candidates[0]), path->path_id);
        result->candidate_count = 1;
        snprintf(result->reason, sizeof(result->reason), "explicit path requested");
        return EN_ERR_NONE;
    }

    size_t candidate_count = selection->candidate_count;
    for (size_t i = 0; i < candidate_count; i++) {
        en_copy_id(result->candidates[i], sizeof(result->candidates[i]), selection->candidates[i]);
    }
    result->candidate_count = candidate_count;

    if (selection->mode == EN_SELECT_PRIORITY) {
        for (size_t i = 0; i < candidate_count; i++) {
            en_path_t *path = en_find_path(controller, selection->candidates[i]);
            if (path == NULL) {
                return EN_ERR_NOT_FOUND;
            }
            char reason[128] = {0};
            if (!exclusion_reason(path, en_find_health(controller, path->path_id), &selection->constraints, reason, sizeof(reason))) {
                en_copy_id(result->selected_path, sizeof(result->selected_path), path->path_id);
                snprintf(result->reason, sizeof(result->reason), "first usable priority candidate");
                return EN_ERR_NONE;
            }
            size_t idx = result->excluded_count++;
            en_copy_id(result->excluded_path_ids[idx], sizeof(result->excluded_path_ids[idx]), path->path_id);
            snprintf(result->excluded_reasons[idx], sizeof(result->excluded_reasons[idx]), "%s", reason);
        }
        return EN_ERR_NO_CANDIDATE;
    }

    if (selection->mode == EN_SELECT_EVALUATED) {
        en_path_t *best = NULL;
        en_path_health_t *best_health = NULL;
        for (size_t i = 0; i < candidate_count; i++) {
            en_path_t *path = en_find_path(controller, selection->candidates[i]);
            if (path == NULL) {
                return EN_ERR_NOT_FOUND;
            }
            en_path_health_t *health = en_find_health(controller, path->path_id);
            char reason[128] = {0};
            if (exclusion_reason(path, health, &selection->constraints, reason, sizeof(reason))) {
                size_t idx = result->excluded_count++;
                en_copy_id(result->excluded_path_ids[idx], sizeof(result->excluded_path_ids[idx]), path->path_id);
                snprintf(result->excluded_reasons[idx], sizeof(result->excluded_reasons[idx]), "%s", reason);
                continue;
            }
            if (best == NULL || compare_paths(path, best, health, best_health, selection) < 0) {
                best = path;
                best_health = health;
            }
        }
        if (best == NULL) {
            return EN_ERR_NO_CANDIDATE;
        }
        en_copy_id(result->selected_path, sizeof(result->selected_path), best->path_id);
        snprintf(result->reason, sizeof(result->reason), "best evaluated candidate by comparison_order");
        return EN_ERR_NONE;
    }

    return EN_ERR_INVALID_ARGUMENT;
}

static bool exclusion_reason(const en_path_t *path, const en_path_health_t *health, const en_path_constraints_t *constraints, char *reason, size_t reason_len)
{
    if (path->administrative_state != EN_ADMIN_ENABLED) {
        snprintf(reason, reason_len, "administratively disabled");
        return true;
    }
    if (health != NULL && (health->state == EN_HEALTH_UNHEALTHY || health->state == EN_HEALTH_FAILED)) {
        snprintf(reason, reason_len, "health is %s", en_health_state_name(health->state));
        return true;
    }
    if (health != NULL && constraints->has_max_rtt_ms && health->rtt_ms > constraints->max_rtt_ms) {
        snprintf(reason, reason_len, "rtt constraint violation");
        return true;
    }
    if (health != NULL && constraints->has_max_packet_loss_percent && health->packet_loss_percent > constraints->max_packet_loss_percent) {
        snprintf(reason, reason_len, "packet loss constraint violation");
        return true;
    }
    for (size_t i = 0; i < constraints->forbidden_waypoint_count; i++) {
        if (waypoint_contains((char (*)[EN_MAX_ID_LEN])path->waypoints, path->waypoint_count, constraints->forbidden_waypoints[i])) {
            snprintf(reason, reason_len, "forbidden waypoint constraint violation");
            return true;
        }
    }
    for (size_t i = 0; i < constraints->required_waypoint_count; i++) {
        if (!waypoint_contains((char (*)[EN_MAX_ID_LEN])path->waypoints, path->waypoint_count, constraints->required_waypoints[i])) {
            snprintf(reason, reason_len, "required waypoint constraint violation");
            return true;
        }
    }
    return false;
}

static bool waypoint_contains(char waypoints[EN_MAX_WAYPOINTS][EN_MAX_ID_LEN], size_t count, const char *needle)
{
    for (size_t i = 0; i < count; i++) {
        if (en_streq(waypoints[i], needle)) {
            return true;
        }
    }
    return false;
}

static int compare_paths(const en_path_t *left, const en_path_t *right, const en_path_health_t *left_health, const en_path_health_t *right_health, const en_path_selection_t *selection)
{
    size_t count = selection->comparison_count == 0 ? 1 : selection->comparison_count;
    for (size_t i = 0; i < count; i++) {
        en_comparison_key_t key = selection->comparison_count == 0 ? EN_COMPARE_PATH_ID : selection->comparison_order[i];
        if (key == EN_COMPARE_PATH_ID) {
            int cmp = strcmp(left->path_id, right->path_id);
            if (cmp != 0) {
                return cmp;
            }
            continue;
        }
        double left_value = metric_value(left, left_health, key);
        double right_value = metric_value(right, right_health, key);
        if (left_value < right_value) {
            return -1;
        }
        if (left_value > right_value) {
            return 1;
        }
    }
    return strcmp(left->path_id, right->path_id);
}

static double metric_value(const en_path_t *path, const en_path_health_t *health, en_comparison_key_t key)
{
    switch (key) {
    case EN_COMPARE_PACKET_LOSS:
        return health == NULL ? DBL_MAX : health->packet_loss_percent;
    case EN_COMPARE_LATENCY:
        return health == NULL ? DBL_MAX : health->rtt_ms;
    case EN_COMPARE_HOP_COUNT:
        return (double)en_path_hop_count(path);
    case EN_COMPARE_ADMIN_PRIORITY:
        return (double)path->priority;
    case EN_COMPARE_PATH_ID:
        return 0.0;
    }
    return DBL_MAX;
}
