#include "eventnet/yaml_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    YAML_SECTION_NONE = 0,
    YAML_SECTION_TUNNELS,
    YAML_SECTION_PATHS,
    YAML_SECTION_INTENTS,
    YAML_SECTION_VPP_EDGES
} yaml_top_section_t;

typedef enum {
    YAML_CONTEXT_NONE = 0,
    YAML_CONTEXT_PATH_WAYPOINTS,
    YAML_CONTEXT_PATH_SEGMENTS,
    YAML_CONTEXT_INTENT_TRAFFIC,
    YAML_CONTEXT_INTENT_SELECTION,
    YAML_CONTEXT_INTENT_CANDIDATES,
    YAML_CONTEXT_INTENT_CONSTRAINTS,
    YAML_CONTEXT_INTENT_FORBIDDEN_WAYPOINTS,
    YAML_CONTEXT_INTENT_REQUIRED_WAYPOINTS,
    YAML_CONTEXT_INTENT_COMPARISON_ORDER,
    YAML_CONTEXT_INTENT_TRANSITION,
    YAML_CONTEXT_INTENT_FALLBACK
} yaml_context_t;

typedef struct {
    yaml_top_section_t top;
    yaml_context_t context;
    en_path_t *path;
    en_segment_t *segment;
    en_tunnel_t *tunnel;
    en_intent_t *intent;
    en_vpp_edge_t *vpp_edge;
} yaml_parse_state_t;

static void set_error(char *error, size_t error_len, size_t line_no, const char *message);
static char *trim(char *text);
static void strip_comment(char *text);
static int indent_of(const char *line);
static bool split_key_value(char *text, char **key, char **value);
static void clean_scalar(char *text);
static en_error_code_t parse_line(en_yaml_config_t *config, yaml_parse_state_t *state, char *line, size_t line_no, char *error, size_t error_len);
static en_error_code_t parse_tunnel_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no);
static en_error_code_t parse_path_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no);
static en_error_code_t parse_segment_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no);
static en_error_code_t parse_intent_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no);
static en_error_code_t parse_vpp_edge_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no);
static en_error_code_t parse_selection_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no);
static en_error_code_t parse_constraints_kv(yaml_parse_state_t *state, const char *key, const char *value);
static en_error_code_t parse_transition_kv(yaml_parse_state_t *state, const char *key, const char *value);
static en_error_code_t parse_fallback_kv(yaml_parse_state_t *state, const char *key, const char *value);
static en_path_selection_mode_t parse_selection_mode(const char *value, bool *ok);
static en_transition_strategy_t parse_transition_strategy(const char *value, bool *ok);
static en_comparison_key_t parse_comparison_key(const char *value, bool *ok);
static bool parse_bool(const char *value);
static void copy_id(char *dst, size_t dst_len, const char *src);
static void copy_address_without_cidr(char *dst, size_t dst_len, const char *src);
static void yaml_normalize(en_yaml_config_t *config);
static bool yaml_has_path(const en_yaml_config_t *config, const char *path_id);
static bool yaml_has_tunnel(const en_yaml_config_t *config, const char *tunnel_id);
static const en_tunnel_t *yaml_find_tunnel(const en_yaml_config_t *config, const char *tunnel_id);

en_error_code_t en_yaml_config_load_file(const char *filename, en_yaml_config_t *config, char *error, size_t error_len)
{
    if (filename == NULL || config == NULL) {
        set_error(error, error_len, 0, "invalid argument");
        return EN_ERR_INVALID_ARGUMENT;
    }

    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        set_error(error, error_len, 0, "failed to open yaml file");
        return EN_ERR_NOT_FOUND;
    }

    memset(config, 0, sizeof(*config));
    yaml_parse_state_t state = {0};
    char line[512];
    size_t line_no = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        line_no++;
        en_error_code_t err = parse_line(config, &state, line, line_no, error, error_len);
        if (err != EN_ERR_NONE) {
            fclose(file);
            return err;
        }
    }

    fclose(file);
    yaml_normalize(config);
    return en_yaml_config_validate(config, error, error_len);
}

en_error_code_t en_yaml_config_validate(const en_yaml_config_t *config, char *error, size_t error_len)
{
    if (config == NULL) {
        set_error(error, error_len, 0, "config is null");
        return EN_ERR_INVALID_ARGUMENT;
    }
    if (config->path_count == 0) {
        set_error(error, error_len, 0, "at least one path is required");
        return EN_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < config->tunnel_count; i++) {
        const en_tunnel_t *tunnel = &config->tunnels[i];
        if (tunnel->tunnel_id[0] == '\0' || tunnel->local_node[0] == '\0' || tunnel->remote_node[0] == '\0') {
            set_error(error, error_len, 0, "tunnel requires id, local_node, and remote_node");
            return EN_ERR_INVALID_ARGUMENT;
        }
        for (size_t j = i + 1; j < config->tunnel_count; j++) {
            if (strcmp(tunnel->tunnel_id, config->tunnels[j].tunnel_id) == 0) {
                set_error(error, error_len, 0, "duplicate tunnel id");
                return EN_ERR_INVALID_ARGUMENT;
            }
        }
    }
    for (size_t i = 0; i < config->path_count; i++) {
        const en_path_t *path = &config->paths[i];
        if (path->path_id[0] == '\0' || path->source[0] == '\0' || path->destination[0] == '\0') {
            set_error(error, error_len, 0, "path requires id, source, and destination");
            return EN_ERR_INVALID_ARGUMENT;
        }
        for (size_t j = i + 1; j < config->path_count; j++) {
            if (strcmp(path->path_id, config->paths[j].path_id) == 0) {
                set_error(error, error_len, 0, "duplicate path id");
                return EN_ERR_INVALID_ARGUMENT;
            }
        }
        if (path->segment_count == 0) {
            set_error(error, error_len, 0, "path requires at least one segment");
            return EN_ERR_INVALID_ARGUMENT;
        }
        for (size_t j = 0; j < path->segment_count; j++) {
            if (!yaml_has_tunnel(config, path->segments[j].tunnel_id)) {
                set_error(error, error_len, 0, "path segment references unknown tunnel_id");
                return EN_ERR_NOT_FOUND;
            }
        }
        if (path->egress_tunnel_id[0] != '\0' && !yaml_has_tunnel(config, path->egress_tunnel_id)) {
            set_error(error, error_len, 0, "path egress_tunnel_id references unknown tunnel");
            return EN_ERR_NOT_FOUND;
        }
    }
    for (size_t i = 0; i < config->intent_count; i++) {
        const en_intent_t *intent = &config->intents[i];
        if (intent->intent_id[0] == '\0') {
            set_error(error, error_len, 0, "intent requires id");
            return EN_ERR_INVALID_ARGUMENT;
        }
        for (size_t j = i + 1; j < config->intent_count; j++) {
            if (strcmp(intent->intent_id, config->intents[j].intent_id) == 0) {
                set_error(error, error_len, 0, "duplicate intent id");
                return EN_ERR_INVALID_ARGUMENT;
            }
        }
        if (intent->path_selection.mode == EN_SELECT_EXPLICIT && !yaml_has_path(config, intent->path_selection.path_id)) {
            set_error(error, error_len, 0, "explicit intent references unknown path_id");
            return EN_ERR_NOT_FOUND;
        }
        for (size_t j = 0; j < intent->path_selection.candidate_count; j++) {
            if (!yaml_has_path(config, intent->path_selection.candidates[j])) {
                set_error(error, error_len, 0, "intent candidate references unknown path");
                return EN_ERR_NOT_FOUND;
            }
        }
    }
    for (size_t i = 0; i < config->vpp_edge_count; i++) {
        const en_vpp_edge_t *edge = &config->vpp_edges[i];
        if (edge->node_id[0] == '\0' || edge->vpp_interface[0] == '\0' || edge->next_hop[0] == '\0') {
            set_error(error, error_len, 0, "vpp edge requires node_id, vpp_interface, and next_hop");
            return EN_ERR_INVALID_ARGUMENT;
        }
        for (size_t j = i + 1; j < config->vpp_edge_count; j++) {
            if (strcmp(edge->node_id, config->vpp_edges[j].node_id) == 0) {
                set_error(error, error_len, 0, "duplicate vpp edge node_id");
                return EN_ERR_INVALID_ARGUMENT;
            }
        }
    }
    return EN_ERR_NONE;
}

static en_error_code_t parse_line(en_yaml_config_t *config, yaml_parse_state_t *state, char *line, size_t line_no, char *error, size_t error_len)
{
    strip_comment(line);
    int indent = indent_of(line);
    char *text = trim(line);
    if (text[0] == '\0') {
        return EN_ERR_NONE;
    }

    if (indent == 0 && strcmp(text, "tunnels:") == 0) {
        state->top = YAML_SECTION_TUNNELS;
        state->context = YAML_CONTEXT_NONE;
        state->tunnel = NULL;
        return EN_ERR_NONE;
    }
    if (indent == 0 && strcmp(text, "paths:") == 0) {
        state->top = YAML_SECTION_PATHS;
        state->context = YAML_CONTEXT_NONE;
        state->path = NULL;
        return EN_ERR_NONE;
    }
    if (indent == 0 && strcmp(text, "intents:") == 0) {
        state->top = YAML_SECTION_INTENTS;
        state->context = YAML_CONTEXT_NONE;
        state->intent = NULL;
        return EN_ERR_NONE;
    }
    if (indent == 0 && strcmp(text, "vpp_edges:") == 0) {
        state->top = YAML_SECTION_VPP_EDGES;
        state->context = YAML_CONTEXT_NONE;
        state->vpp_edge = NULL;
        return EN_ERR_NONE;
    }

    if (state->top == YAML_SECTION_NONE) {
        set_error(error, error_len, line_no, "expected top-level tunnels:, paths:, intents:, or vpp_edges:");
        return EN_ERR_INVALID_ARGUMENT;
    }

    if (strncmp(text, "- ", 2) == 0) {
        char *item = trim(text + 2);
        char *key = NULL;
        char *value = NULL;
        bool has_key = split_key_value(item, &key, &value);

        if (state->top == YAML_SECTION_TUNNELS && indent == 2) {
            if (config->tunnel_count >= EN_MAX_TUNNELS) {
                set_error(error, error_len, line_no, "too many tunnels");
                return EN_ERR_INVALID_ARGUMENT;
            }
            state->tunnel = &config->tunnels[config->tunnel_count++];
            memset(state->tunnel, 0, sizeof(*state->tunnel));
            copy_id(state->tunnel->protocol, sizeof(state->tunnel->protocol), "ipsec");
            state->tunnel->state = EN_TUNNEL_CONFIGURED;
            state->tunnel->health = EN_HEALTH_UNKNOWN;
            state->context = YAML_CONTEXT_NONE;
            if (has_key) {
                return parse_tunnel_kv(state, key, value, error, error_len, line_no);
            }
            return EN_ERR_NONE;
        }

        if (state->top == YAML_SECTION_VPP_EDGES && indent == 2) {
            if (config->vpp_edge_count >= EN_MAX_VPP_EDGES) {
                set_error(error, error_len, line_no, "too many vpp edges");
                return EN_ERR_INVALID_ARGUMENT;
            }
            state->vpp_edge = &config->vpp_edges[config->vpp_edge_count++];
            memset(state->vpp_edge, 0, sizeof(*state->vpp_edge));
            state->context = YAML_CONTEXT_NONE;
            if (has_key) {
                return parse_vpp_edge_kv(state, key, value, error, error_len, line_no);
            }
            return EN_ERR_NONE;
        }

        if (state->top == YAML_SECTION_PATHS && indent == 2) {
            if (config->path_count >= EN_MAX_PATHS) {
                set_error(error, error_len, line_no, "too many paths");
                return EN_ERR_INVALID_ARGUMENT;
            }
            state->path = &config->paths[config->path_count++];
            memset(state->path, 0, sizeof(*state->path));
            state->path->administrative_state = EN_ADMIN_ENABLED;
            state->path->operational_state = EN_PATH_UNKNOWN;
            state->context = YAML_CONTEXT_NONE;
            if (has_key) {
                return parse_path_kv(state, key, value, error, error_len, line_no);
            }
            return EN_ERR_NONE;
        }

        if (state->top == YAML_SECTION_PATHS && state->context == YAML_CONTEXT_PATH_WAYPOINTS) {
            if (state->path == NULL || state->path->waypoint_count >= EN_MAX_WAYPOINTS) {
                set_error(error, error_len, line_no, "too many path waypoints");
                return EN_ERR_INVALID_ARGUMENT;
            }
            clean_scalar(item);
            copy_id(state->path->waypoints[state->path->waypoint_count++], EN_MAX_ID_LEN, item);
            return EN_ERR_NONE;
        }

        if (state->top == YAML_SECTION_PATHS && state->context == YAML_CONTEXT_PATH_SEGMENTS) {
            if (state->path == NULL || state->path->segment_count >= EN_MAX_SEGMENTS) {
                set_error(error, error_len, line_no, "too many path segments");
                return EN_ERR_INVALID_ARGUMENT;
            }
            state->segment = &state->path->segments[state->path->segment_count++];
            memset(state->segment, 0, sizeof(*state->segment));
            if (has_key) {
                return parse_segment_kv(state, key, value, error, error_len, line_no);
            }
            return EN_ERR_NONE;
        }

        if (state->top == YAML_SECTION_INTENTS && indent == 2) {
            if (config->intent_count >= EN_MAX_CANDIDATES) {
                set_error(error, error_len, line_no, "too many intents");
                return EN_ERR_INVALID_ARGUMENT;
            }
            state->intent = &config->intents[config->intent_count++];
            memset(state->intent, 0, sizeof(*state->intent));
            state->intent->transition.strategy = EN_TRANSITION_IMMEDIATE;
            state->intent->transition.max_pause_ms = 50;
            state->intent->transition.drain_timeout_ms = 40;
            state->intent->transition.timeout_ms = 5000;
            state->intent->fallback.enabled = true;
            state->context = YAML_CONTEXT_NONE;
            if (has_key) {
                return parse_intent_kv(state, key, value, error, error_len, line_no);
            }
            return EN_ERR_NONE;
        }

        if (state->top == YAML_SECTION_INTENTS && state->context == YAML_CONTEXT_INTENT_CANDIDATES) {
            if (state->intent == NULL || state->intent->path_selection.candidate_count >= EN_MAX_CANDIDATES) {
                set_error(error, error_len, line_no, "too many path candidates");
                return EN_ERR_INVALID_ARGUMENT;
            }
            clean_scalar(item);
            copy_id(state->intent->path_selection.candidates[state->intent->path_selection.candidate_count++], EN_MAX_ID_LEN, item);
            return EN_ERR_NONE;
        }

        if (state->top == YAML_SECTION_INTENTS && state->context == YAML_CONTEXT_INTENT_FORBIDDEN_WAYPOINTS) {
            en_path_constraints_t *constraints = &state->intent->path_selection.constraints;
            if (constraints->forbidden_waypoint_count >= EN_MAX_WAYPOINTS) {
                set_error(error, error_len, line_no, "too many forbidden waypoints");
                return EN_ERR_INVALID_ARGUMENT;
            }
            clean_scalar(item);
            copy_id(constraints->forbidden_waypoints[constraints->forbidden_waypoint_count++], EN_MAX_ID_LEN, item);
            return EN_ERR_NONE;
        }

        if (state->top == YAML_SECTION_INTENTS && state->context == YAML_CONTEXT_INTENT_REQUIRED_WAYPOINTS) {
            en_path_constraints_t *constraints = &state->intent->path_selection.constraints;
            if (constraints->required_waypoint_count >= EN_MAX_WAYPOINTS) {
                set_error(error, error_len, line_no, "too many required waypoints");
                return EN_ERR_INVALID_ARGUMENT;
            }
            clean_scalar(item);
            copy_id(constraints->required_waypoints[constraints->required_waypoint_count++], EN_MAX_ID_LEN, item);
            return EN_ERR_NONE;
        }

        if (state->top == YAML_SECTION_INTENTS && state->context == YAML_CONTEXT_INTENT_COMPARISON_ORDER) {
            if (state->intent->path_selection.comparison_count >= EN_MAX_COMPARISONS) {
                set_error(error, error_len, line_no, "too many comparison keys");
                return EN_ERR_INVALID_ARGUMENT;
            }
            clean_scalar(item);
            bool ok = false;
            en_comparison_key_t key_value = parse_comparison_key(item, &ok);
            if (!ok) {
                set_error(error, error_len, line_no, "unknown comparison key");
                return EN_ERR_INVALID_ARGUMENT;
            }
            state->intent->path_selection.comparison_order[state->intent->path_selection.comparison_count++] = key_value;
            return EN_ERR_NONE;
        }

        set_error(error, error_len, line_no, "unexpected list item");
        return EN_ERR_INVALID_ARGUMENT;
    }

    char *key = NULL;
    char *value = NULL;
    if (!split_key_value(text, &key, &value)) {
        set_error(error, error_len, line_no, "expected key: value");
        return EN_ERR_INVALID_ARGUMENT;
    }

    if (state->top == YAML_SECTION_TUNNELS) {
        return parse_tunnel_kv(state, key, value, error, error_len, line_no);
    }

    if (state->top == YAML_SECTION_PATHS) {
        if (state->context == YAML_CONTEXT_PATH_SEGMENTS && state->segment != NULL && indent >= 6) {
            return parse_segment_kv(state, key, value, error, error_len, line_no);
        }
        return parse_path_kv(state, key, value, error, error_len, line_no);
    }

    if (state->top == YAML_SECTION_VPP_EDGES) {
        return parse_vpp_edge_kv(state, key, value, error, error_len, line_no);
    }

    if (state->top == YAML_SECTION_INTENTS) {
        if (strcmp(key, "traffic") == 0) {
            state->context = YAML_CONTEXT_INTENT_TRAFFIC;
            return EN_ERR_NONE;
        }
        if (strcmp(key, "path_selection") == 0) {
            state->context = YAML_CONTEXT_INTENT_SELECTION;
            return EN_ERR_NONE;
        }
        if (strcmp(key, "candidates") == 0) {
            state->context = YAML_CONTEXT_INTENT_CANDIDATES;
            return EN_ERR_NONE;
        }
        if (strcmp(key, "constraints") == 0) {
            state->context = YAML_CONTEXT_INTENT_CONSTRAINTS;
            return EN_ERR_NONE;
        }
        if (strcmp(key, "comparison_order") == 0) {
            state->context = YAML_CONTEXT_INTENT_COMPARISON_ORDER;
            return EN_ERR_NONE;
        }
        if (strcmp(key, "transition") == 0) {
            state->context = YAML_CONTEXT_INTENT_TRANSITION;
            return EN_ERR_NONE;
        }
        if (strcmp(key, "fallback") == 0) {
            state->context = YAML_CONTEXT_INTENT_FALLBACK;
            return EN_ERR_NONE;
        }
        if (state->context == YAML_CONTEXT_INTENT_TRAFFIC) {
            return parse_intent_kv(state, key, value, error, error_len, line_no);
        }
        if (state->context == YAML_CONTEXT_INTENT_SELECTION) {
            return parse_selection_kv(state, key, value, error, error_len, line_no);
        }
        if (state->context == YAML_CONTEXT_INTENT_CONSTRAINTS) {
            if (strcmp(key, "forbidden_waypoints") == 0) {
                state->context = YAML_CONTEXT_INTENT_FORBIDDEN_WAYPOINTS;
                return EN_ERR_NONE;
            }
            if (strcmp(key, "required_waypoints") == 0) {
                state->context = YAML_CONTEXT_INTENT_REQUIRED_WAYPOINTS;
                return EN_ERR_NONE;
            }
            return parse_constraints_kv(state, key, value);
        }
        if (state->context == YAML_CONTEXT_INTENT_TRANSITION) {
            return parse_transition_kv(state, key, value);
        }
        if (state->context == YAML_CONTEXT_INTENT_FALLBACK) {
            return parse_fallback_kv(state, key, value);
        }
        return parse_intent_kv(state, key, value, error, error_len, line_no);
    }

    return EN_ERR_NONE;
}

static en_error_code_t parse_vpp_edge_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no)
{
    if (state->vpp_edge == NULL) {
        set_error(error, error_len, line_no, "vpp edge field without vpp edge item");
        return EN_ERR_INVALID_ARGUMENT;
    }
    if (strcmp(key, "id") == 0 || strcmp(key, "node") == 0 || strcmp(key, "node_id") == 0) {
        copy_id(state->vpp_edge->node_id, sizeof(state->vpp_edge->node_id), value);
    } else if (strcmp(key, "host_interface") == 0 || strcmp(key, "host_if") == 0) {
        copy_id(state->vpp_edge->host_interface, sizeof(state->vpp_edge->host_interface), value);
    } else if (strcmp(key, "vpp_interface") == 0 || strcmp(key, "vpp_if") == 0) {
        copy_id(state->vpp_edge->vpp_interface, sizeof(state->vpp_edge->vpp_interface), value);
    } else if (
        strcmp(key, "namespace_interface") == 0 ||
        strcmp(key, "ns_interface") == 0 ||
        strcmp(key, "ns_if") == 0
    ) {
        copy_id(state->vpp_edge->namespace_interface, sizeof(state->vpp_edge->namespace_interface), value);
    } else if (strcmp(key, "namespace_address") == 0 || strcmp(key, "ns_address") == 0 || strcmp(key, "ns_addr") == 0) {
        copy_id(state->vpp_edge->namespace_address, sizeof(state->vpp_edge->namespace_address), value);
    } else if (strcmp(key, "vpp_address") == 0 || strcmp(key, "vpp_addr") == 0) {
        copy_id(state->vpp_edge->vpp_address, sizeof(state->vpp_edge->vpp_address), value);
    } else if (strcmp(key, "next_hop") == 0) {
        copy_id(state->vpp_edge->next_hop, sizeof(state->vpp_edge->next_hop), value);
    }
    return EN_ERR_NONE;
}

static en_error_code_t parse_tunnel_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no)
{
    if (state->tunnel == NULL) {
        set_error(error, error_len, line_no, "tunnel field without tunnel item");
        return EN_ERR_INVALID_ARGUMENT;
    }
    if (strcmp(key, "id") == 0 || strcmp(key, "tunnel_id") == 0) {
        copy_id(state->tunnel->tunnel_id, sizeof(state->tunnel->tunnel_id), value);
    } else if (strcmp(key, "local_node") == 0) {
        copy_id(state->tunnel->local_node, sizeof(state->tunnel->local_node), value);
    } else if (strcmp(key, "remote_node") == 0) {
        copy_id(state->tunnel->remote_node, sizeof(state->tunnel->remote_node), value);
    } else if (strcmp(key, "local_endpoint") == 0) {
        copy_id(state->tunnel->local_endpoint, sizeof(state->tunnel->local_endpoint), value);
    } else if (strcmp(key, "remote_endpoint") == 0) {
        copy_id(state->tunnel->remote_endpoint, sizeof(state->tunnel->remote_endpoint), value);
    } else if (strcmp(key, "local_id") == 0) {
        copy_id(state->tunnel->local_id, sizeof(state->tunnel->local_id), value);
    } else if (strcmp(key, "remote_id") == 0) {
        copy_id(state->tunnel->remote_id, sizeof(state->tunnel->remote_id), value);
    } else if (strcmp(key, "psk") == 0) {
        copy_id(state->tunnel->psk, sizeof(state->tunnel->psk), value);
    } else if (strcmp(key, "local_ts") == 0 || strcmp(key, "local_traffic_selector") == 0) {
        copy_id(state->tunnel->local_traffic_selector, sizeof(state->tunnel->local_traffic_selector), value);
    } else if (strcmp(key, "remote_ts") == 0 || strcmp(key, "remote_traffic_selector") == 0) {
        copy_id(state->tunnel->remote_traffic_selector, sizeof(state->tunnel->remote_traffic_selector), value);
    } else if (strcmp(key, "protocol") == 0) {
        copy_id(state->tunnel->protocol, sizeof(state->tunnel->protocol), value);
    }
    return EN_ERR_NONE;
}

static en_error_code_t parse_path_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no)
{
    if (state->path == NULL) {
        set_error(error, error_len, line_no, "path field without path item");
        return EN_ERR_INVALID_ARGUMENT;
    }
    if (strcmp(key, "id") == 0 || strcmp(key, "path_id") == 0) {
        copy_id(state->path->path_id, sizeof(state->path->path_id), value);
    } else if (strcmp(key, "source") == 0) {
        copy_id(state->path->source, sizeof(state->path->source), value);
    } else if (strcmp(key, "destination") == 0) {
        copy_id(state->path->destination, sizeof(state->path->destination), value);
    } else if (strcmp(key, "route_destination_prefix") == 0 || strcmp(key, "dst_prefix") == 0) {
        copy_id(state->path->route_destination_prefix, sizeof(state->path->route_destination_prefix), value);
    } else if (strcmp(key, "route_next_hop") == 0 || strcmp(key, "next_hop") == 0) {
        copy_id(state->path->route_next_hop, sizeof(state->path->route_next_hop), value);
    } else if (strcmp(key, "egress_tunnel_id") == 0) {
        copy_id(state->path->egress_tunnel_id, sizeof(state->path->egress_tunnel_id), value);
    } else if (strcmp(key, "priority") == 0) {
        state->path->priority = atoi(value);
    } else if (strcmp(key, "waypoints") == 0) {
        state->context = YAML_CONTEXT_PATH_WAYPOINTS;
    } else if (strcmp(key, "segments") == 0) {
        state->context = YAML_CONTEXT_PATH_SEGMENTS;
    }
    return EN_ERR_NONE;
}

static en_error_code_t parse_segment_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no)
{
    if (state->segment == NULL) {
        set_error(error, error_len, line_no, "segment field without segment item");
        return EN_ERR_INVALID_ARGUMENT;
    }
    if (strcmp(key, "id") == 0 || strcmp(key, "segment_id") == 0) {
        copy_id(state->segment->segment_id, sizeof(state->segment->segment_id), value);
    } else if (strcmp(key, "from") == 0 || strcmp(key, "from_node") == 0) {
        copy_id(state->segment->from_node, sizeof(state->segment->from_node), value);
    } else if (strcmp(key, "to") == 0 || strcmp(key, "to_node") == 0) {
        copy_id(state->segment->to_node, sizeof(state->segment->to_node), value);
    } else if (strcmp(key, "tunnel_id") == 0) {
        copy_id(state->segment->tunnel_id, sizeof(state->segment->tunnel_id), value);
    }
    return EN_ERR_NONE;
}

static en_error_code_t parse_intent_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no)
{
    if (state->intent == NULL) {
        set_error(error, error_len, line_no, "intent field without intent item");
        return EN_ERR_INVALID_ARGUMENT;
    }
    if (strcmp(key, "id") == 0 || strcmp(key, "intent_id") == 0) {
        copy_id(state->intent->intent_id, sizeof(state->intent->intent_id), value);
    } else if (strcmp(key, "source") == 0) {
        copy_id(state->intent->traffic.source, sizeof(state->intent->traffic.source), value);
    } else if (strcmp(key, "destination") == 0) {
        copy_id(state->intent->traffic.destination, sizeof(state->intent->traffic.destination), value);
    }
    return EN_ERR_NONE;
}

static en_error_code_t parse_selection_kv(yaml_parse_state_t *state, const char *key, const char *value, char *error, size_t error_len, size_t line_no)
{
    if (strcmp(key, "mode") == 0) {
        bool ok = false;
        state->intent->path_selection.mode = parse_selection_mode(value, &ok);
        if (!ok) {
            set_error(error, error_len, line_no, "unknown path selection mode");
            return EN_ERR_INVALID_ARGUMENT;
        }
    } else if (strcmp(key, "path_id") == 0) {
        copy_id(state->intent->path_selection.path_id, sizeof(state->intent->path_selection.path_id), value);
    } else if (strcmp(key, "candidates") == 0) {
        state->context = YAML_CONTEXT_INTENT_CANDIDATES;
    } else if (strcmp(key, "comparison_order") == 0) {
        state->context = YAML_CONTEXT_INTENT_COMPARISON_ORDER;
    } else if (strcmp(key, "constraints") == 0) {
        state->context = YAML_CONTEXT_INTENT_CONSTRAINTS;
    }
    return EN_ERR_NONE;
}

static en_error_code_t parse_constraints_kv(yaml_parse_state_t *state, const char *key, const char *value)
{
    en_path_constraints_t *constraints = &state->intent->path_selection.constraints;
    if (strcmp(key, "max_rtt_ms") == 0) {
        constraints->has_max_rtt_ms = true;
        constraints->max_rtt_ms = atof(value);
    } else if (strcmp(key, "max_packet_loss_percent") == 0) {
        constraints->has_max_packet_loss_percent = true;
        constraints->max_packet_loss_percent = atof(value);
    }
    return EN_ERR_NONE;
}

static en_error_code_t parse_transition_kv(yaml_parse_state_t *state, const char *key, const char *value)
{
    if (strcmp(key, "strategy") == 0) {
        bool ok = false;
        state->intent->transition.strategy = parse_transition_strategy(value, &ok);
        return ok ? EN_ERR_NONE : EN_ERR_INVALID_ARGUMENT;
    } else if (strcmp(key, "max_pause_ms") == 0) {
        state->intent->transition.max_pause_ms = atoi(value);
    } else if (strcmp(key, "drain_timeout_ms") == 0) {
        state->intent->transition.drain_timeout_ms = atoi(value);
    } else if (strcmp(key, "timeout_ms") == 0) {
        state->intent->transition.timeout_ms = atoi(value);
    }
    return EN_ERR_NONE;
}

static en_error_code_t parse_fallback_kv(yaml_parse_state_t *state, const char *key, const char *value)
{
    if (strcmp(key, "enabled") == 0) {
        state->intent->fallback.enabled = parse_bool(value);
    } else if (strcmp(key, "path_id") == 0) {
        copy_id(state->intent->fallback.path_id, sizeof(state->intent->fallback.path_id), value);
    }
    return EN_ERR_NONE;
}

static en_path_selection_mode_t parse_selection_mode(const char *value, bool *ok)
{
    *ok = true;
    if (strcmp(value, "explicit") == 0) return EN_SELECT_EXPLICIT;
    if (strcmp(value, "priority") == 0) return EN_SELECT_PRIORITY;
    if (strcmp(value, "evaluated") == 0) return EN_SELECT_EVALUATED;
    *ok = false;
    return EN_SELECT_EXPLICIT;
}

static en_transition_strategy_t parse_transition_strategy(const char *value, bool *ok)
{
    *ok = true;
    if (strcmp(value, "immediate") == 0) return EN_TRANSITION_IMMEDIATE;
    if (strcmp(value, "graceful") == 0) return EN_TRANSITION_GRACEFUL;
    if (strcmp(value, "flow-preserve") == 0) return EN_TRANSITION_FLOW_PRESERVE;
    *ok = false;
    return EN_TRANSITION_IMMEDIATE;
}

static en_comparison_key_t parse_comparison_key(const char *value, bool *ok)
{
    *ok = true;
    if (strcmp(value, "packet_loss") == 0 || strcmp(value, "packet_loss_percent") == 0) return EN_COMPARE_PACKET_LOSS;
    if (strcmp(value, "latency") == 0 || strcmp(value, "rtt") == 0 || strcmp(value, "rtt_ms") == 0) return EN_COMPARE_LATENCY;
    if (strcmp(value, "hop_count") == 0) return EN_COMPARE_HOP_COUNT;
    if (strcmp(value, "administrative_priority") == 0 || strcmp(value, "priority") == 0) return EN_COMPARE_ADMIN_PRIORITY;
    if (strcmp(value, "path_id") == 0) return EN_COMPARE_PATH_ID;
    *ok = false;
    return EN_COMPARE_PATH_ID;
}

static bool parse_bool(const char *value)
{
    return strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 || strcmp(value, "1") == 0;
}

static void set_error(char *error, size_t error_len, size_t line_no, const char *message)
{
    if (error == NULL || error_len == 0) {
        return;
    }
    if (line_no == 0) {
        snprintf(error, error_len, "%s", message);
    } else {
        snprintf(error, error_len, "line %zu: %s", line_no, message);
    }
}

static int indent_of(const char *line)
{
    int indent = 0;
    while (*line == ' ') {
        indent++;
        line++;
    }
    return indent;
}

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) {
        text++;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';
    return text;
}

static void strip_comment(char *text)
{
    bool quoted = false;
    char quote = '\0';
    for (char *p = text; *p != '\0'; p++) {
        if ((*p == '"' || *p == '\'') && (!quoted || *p == quote)) {
            quoted = !quoted;
            quote = quoted ? *p : '\0';
        }
        if (!quoted && *p == '#') {
            *p = '\0';
            return;
        }
    }
}

static bool split_key_value(char *text, char **key, char **value)
{
    char *colon = strchr(text, ':');
    if (colon == NULL) {
        return false;
    }
    *colon = '\0';
    *key = trim(text);
    *value = trim(colon + 1);
    clean_scalar(*value);
    return true;
}

static void clean_scalar(char *text)
{
    char *value = trim(text);
    if (value != text) {
        memmove(text, value, strlen(value) + 1);
    }
    size_t len = strlen(text);
    if (len >= 2 && ((text[0] == '"' && text[len - 1] == '"') || (text[0] == '\'' && text[len - 1] == '\''))) {
        memmove(text, text + 1, len - 2);
        text[len - 2] = '\0';
    }
}

static void copy_id(char *dst, size_t dst_len, const char *src)
{
    snprintf(dst, dst_len, "%s", src == NULL ? "" : src);
}

static void copy_address_without_cidr(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t len = strcspn(src, "/");
    if (len >= dst_len) {
        len = dst_len - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void yaml_normalize(en_yaml_config_t *config)
{
    if (config == NULL) {
        return;
    }
    for (size_t i = 0; i < config->path_count; i++) {
        en_path_t *path = &config->paths[i];
        if (path->egress_tunnel_id[0] == '\0' && path->segment_count > 0) {
            copy_id(path->egress_tunnel_id, sizeof(path->egress_tunnel_id), path->segments[0].tunnel_id);
        }
        if (path->route_destination_prefix[0] == '\0') {
            const en_tunnel_t *tunnel = yaml_find_tunnel(config, path->egress_tunnel_id);
            if (tunnel != NULL && tunnel->remote_traffic_selector[0] != '\0') {
                copy_id(path->route_destination_prefix, sizeof(path->route_destination_prefix), tunnel->remote_traffic_selector);
            }
        }
        if (path->route_next_hop[0] == '\0') {
            const en_tunnel_t *tunnel = yaml_find_tunnel(config, path->egress_tunnel_id);
            if (tunnel != NULL && tunnel->remote_endpoint[0] != '\0') {
                copy_id(path->route_next_hop, sizeof(path->route_next_hop), tunnel->remote_endpoint);
            }
        }
    }
    for (size_t i = 0; i < config->vpp_edge_count; i++) {
        en_vpp_edge_t *edge = &config->vpp_edges[i];
        if (edge->vpp_interface[0] == '\0' && edge->host_interface[0] != '\0') {
            char host_interface[EN_MAX_ID_LEN] = {0};
            copy_id(host_interface, sizeof(host_interface), edge->host_interface);
            snprintf(edge->vpp_interface, sizeof(edge->vpp_interface), "host-%.58s", host_interface);
        }
        if (edge->next_hop[0] == '\0' && edge->namespace_address[0] != '\0') {
            copy_address_without_cidr(edge->next_hop, sizeof(edge->next_hop), edge->namespace_address);
        }
    }
}

static bool yaml_has_path(const en_yaml_config_t *config, const char *path_id)
{
    for (size_t i = 0; i < config->path_count; i++) {
        if (strcmp(config->paths[i].path_id, path_id) == 0) {
            return true;
        }
    }
    return false;
}

static bool yaml_has_tunnel(const en_yaml_config_t *config, const char *tunnel_id)
{
    return yaml_find_tunnel(config, tunnel_id) != NULL;
}

static const en_tunnel_t *yaml_find_tunnel(const en_yaml_config_t *config, const char *tunnel_id)
{
    if (config == NULL || tunnel_id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < config->tunnel_count; i++) {
        if (strcmp(config->tunnels[i].tunnel_id, tunnel_id) == 0) {
            return &config->tunnels[i];
        }
    }
    return NULL;
}
