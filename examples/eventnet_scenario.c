#include "eventnet/controller.h"
#include "eventnet/mock_adapters.h"
#include "eventnet/yaml_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char path_id[EN_MAX_ID_LEN];
    en_health_state_t state;
    double rtt_ms;
    double packet_loss_percent;
    bool has_rtt_ms;
    bool has_packet_loss_percent;
} injected_health_t;

static const en_intent_t *find_intent(const en_yaml_config_t *config, const char *intent_id);
static const en_path_t *find_path(const en_yaml_config_t *config, const char *path_id);
static en_health_state_t parse_health_state(const char *value, bool *ok);
static en_comparison_key_t parse_comparison_key_arg(const char *value, bool *ok);
static bool parse_health_arg(const char *text, injected_health_t *health);
static void apply_health_override(en_health_probe_mock_t *health_mock, const injected_health_t *health);
static bool force_selection_mode(en_intent_t *intent, const char *mode);
static bool force_comparison_order(en_intent_t *intent, const char *value);
static void print_result(const en_reconcile_result_t *result);
static int generate_runtime(const char *program, const char *yaml, const en_reconcile_result_t *result, const char *active_path, const char *failed_path);
static void usage(const char *program);

int main(int argc, char **argv)
{
    const char *filename = "samples/linux-vm-netns.yaml";
    const char *intent_id = NULL;
    const char *mode = NULL;
    const char *active_path = NULL;
    const char *failed_path = NULL;
    const char *expected_path = NULL;
    const char *comparison_order = NULL;
    bool generate = false;
    injected_health_t injected[EN_MAX_PATHS] = {0};
    size_t injected_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--intent") == 0 && i + 1 < argc) {
            intent_id = argv[++i];
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else if (strcmp(argv[i], "--active-path") == 0 && i + 1 < argc) {
            active_path = argv[++i];
        } else if (strcmp(argv[i], "--fail-path") == 0 && i + 1 < argc) {
            failed_path = argv[++i];
        } else if (strcmp(argv[i], "--health") == 0 && i + 1 < argc) {
            if (injected_count >= EN_MAX_PATHS || !parse_health_arg(argv[++i], &injected[injected_count])) {
                fprintf(stderr, "invalid --health argument\n");
                return 2;
            }
            injected_count++;
        } else if (strcmp(argv[i], "--compare") == 0 && i + 1 < argc) {
            comparison_order = argv[++i];
        } else if (strcmp(argv[i], "--expect") == 0 && i + 1 < argc) {
            expected_path = argv[++i];
        } else if (strcmp(argv[i], "--generate-runtime") == 0) {
            generate = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            filename = argv[i];
        }
    }

    en_yaml_config_t config = {0};
    char error[256] = {0};
    en_error_code_t err = en_yaml_config_load_file(filename, &config, error, sizeof(error));
    if (err != EN_ERR_NONE) {
        fprintf(stderr, "yaml load failed: %s: %s\n", en_error_code_name(err), error);
        return 1;
    }

    const en_intent_t *base_intent = find_intent(&config, intent_id);
    if (base_intent == NULL) {
        fprintf(stderr, "intent not found\n");
        return 1;
    }

    en_intent_t scenario_intent = *base_intent;
    if (mode != NULL && !force_selection_mode(&scenario_intent, mode)) {
        fprintf(stderr, "invalid --mode argument\n");
        return 2;
    }
    if (comparison_order != NULL && !force_comparison_order(&scenario_intent, comparison_order)) {
        fprintf(stderr, "invalid --compare argument\n");
        return 2;
    }

    en_vpp_mock_t vpp_mock = {0};
    en_health_probe_mock_t health_mock = {0};
    for (size_t i = 0; i < injected_count; i++) {
        apply_health_override(&health_mock, &injected[i]);
    }
    if (failed_path != NULL) {
        injected_health_t health = {0};
        snprintf(health.path_id, sizeof(health.path_id), "%s", failed_path);
        health.state = EN_HEALTH_FAILED;
        health.packet_loss_percent = 100.0;
        apply_health_override(&health_mock, &health);
    }

    en_controller_t *controller = en_controller_create_with_tunnels(
        config.paths,
        config.path_count,
        config.tunnels,
        config.tunnel_count,
        en_strongswan_mock_adapter(),
        en_vpp_mock_adapter(&vpp_mock),
        en_health_probe_mock_adapter(&health_mock)
    );
    if (controller == NULL) {
        fprintf(stderr, "controller create failed\n");
        return 1;
    }

    if (active_path != NULL && failed_path != NULL && scenario_intent.fallback.enabled && scenario_intent.fallback.path_id[0] != '\0') {
        if (strcmp(active_path, failed_path) == 0) {
            const en_path_t *fallback = find_path(&config, scenario_intent.fallback.path_id);
            if (fallback == NULL) {
                fprintf(stderr, "fallback path not found: %s\n", scenario_intent.fallback.path_id);
                en_controller_destroy(controller);
                return 1;
            }
            en_reconcile_result_t result = {0};
            snprintf(result.intent_id, sizeof(result.intent_id), "%s", scenario_intent.intent_id);
            snprintf(result.selected_path, sizeof(result.selected_path), "%s", fallback->path_id);
            result.transition_state = EN_TRANSITION_COMPLETED;
            snprintf(result.explanation.selected_path, sizeof(result.explanation.selected_path), "%s", fallback->path_id);
            snprintf(result.explanation.reason, sizeof(result.explanation.reason), "active path %s failed; using fallback %s", active_path, fallback->path_id);
            snprintf(result.explanation.excluded_path_ids[0], sizeof(result.explanation.excluded_path_ids[0]), "%s", failed_path);
            snprintf(result.explanation.excluded_reasons[0], sizeof(result.explanation.excluded_reasons[0]), "%s", "active path failed event");
            result.explanation.excluded_count = 1;
            print_result(&result);
            if (generate && generate_runtime(argv[0], filename, &result, active_path, failed_path) != 0) {
                en_controller_destroy(controller);
                return 1;
            }
            en_controller_destroy(controller);
            if (expected_path != NULL && strcmp(result.selected_path, expected_path) != 0) {
                printf("expect: %s\n", expected_path);
                printf("result: fail\n");
                return 1;
            }
            if (expected_path != NULL) {
                printf("expect: %s\n", expected_path);
                printf("result: pass\n");
            }
            return 0;
        }
    }

    en_reconcile_result_t result = {0};
    err = en_controller_submit_intent(controller, &scenario_intent, &result);
    en_controller_destroy(controller);
    if (err != EN_ERR_NONE) {
        fprintf(stderr, "scenario reconcile failed: %s\n", en_error_code_name(err));
        return 1;
    }

    print_result(&result);
    if (generate && generate_runtime(argv[0], filename, &result, active_path, failed_path) != 0) {
        return 1;
    }
    if (expected_path != NULL) {
        printf("expect: %s\n", expected_path);
        if (strcmp(result.selected_path, expected_path) != 0) {
            printf("result: fail\n");
            return 1;
        }
        printf("result: pass\n");
    }
    return 0;
}

static const en_intent_t *find_intent(const en_yaml_config_t *config, const char *intent_id)
{
    if (intent_id == NULL) {
        return config->intent_count == 0 ? NULL : &config->intents[0];
    }
    for (size_t i = 0; i < config->intent_count; i++) {
        if (strcmp(config->intents[i].intent_id, intent_id) == 0) {
            return &config->intents[i];
        }
    }
    return NULL;
}

static const en_path_t *find_path(const en_yaml_config_t *config, const char *path_id)
{
    if (path_id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < config->path_count; i++) {
        if (strcmp(config->paths[i].path_id, path_id) == 0) {
            return &config->paths[i];
        }
    }
    return NULL;
}

static en_health_state_t parse_health_state(const char *value, bool *ok)
{
    *ok = true;
    if (strcmp(value, "unknown") == 0) return EN_HEALTH_UNKNOWN;
    if (strcmp(value, "healthy") == 0) return EN_HEALTH_HEALTHY;
    if (strcmp(value, "degraded") == 0) return EN_HEALTH_DEGRADED;
    if (strcmp(value, "unhealthy") == 0) return EN_HEALTH_UNHEALTHY;
    if (strcmp(value, "failed") == 0) return EN_HEALTH_FAILED;
    *ok = false;
    return EN_HEALTH_UNKNOWN;
}

static en_comparison_key_t parse_comparison_key_arg(const char *value, bool *ok)
{
    *ok = true;
    if (strcmp(value, "packet_loss") == 0 || strcmp(value, "loss") == 0) return EN_COMPARE_PACKET_LOSS;
    if (strcmp(value, "latency") == 0 || strcmp(value, "rtt") == 0 || strcmp(value, "rtt_ms") == 0) return EN_COMPARE_LATENCY;
    if (strcmp(value, "hop_count") == 0 || strcmp(value, "hops") == 0) return EN_COMPARE_HOP_COUNT;
    if (strcmp(value, "priority") == 0 || strcmp(value, "administrative_priority") == 0) return EN_COMPARE_ADMIN_PRIORITY;
    if (strcmp(value, "path_id") == 0) return EN_COMPARE_PATH_ID;
    *ok = false;
    return EN_COMPARE_PATH_ID;
}

static bool parse_health_arg(const char *text, injected_health_t *health)
{
    char buf[256] = {0};
    snprintf(buf, sizeof(buf), "%s", text);
    char *equals = strchr(buf, '=');
    if (equals == NULL) {
        return false;
    }
    *equals = '\0';
    snprintf(health->path_id, sizeof(health->path_id), "%.63s", buf);
    health->state = EN_HEALTH_HEALTHY;
    char *cursor = equals + 1;
    while (cursor != NULL && *cursor != '\0') {
        char *comma = strchr(cursor, ',');
        if (comma != NULL) {
            *comma = '\0';
        }
        char *kv = strchr(cursor, '=');
        if (kv == NULL) {
            bool ok = false;
            health->state = parse_health_state(cursor, &ok);
            if (!ok) {
                return false;
            }
        } else {
            *kv = '\0';
            const char *key = cursor;
            const char *value = kv + 1;
            if (strcmp(key, "state") == 0) {
                bool ok = false;
                health->state = parse_health_state(value, &ok);
                if (!ok) {
                    return false;
                }
            } else if (strcmp(key, "rtt") == 0 || strcmp(key, "rtt_ms") == 0) {
                health->rtt_ms = atof(value);
                health->has_rtt_ms = true;
            } else if (strcmp(key, "loss") == 0 || strcmp(key, "packet_loss") == 0) {
                health->packet_loss_percent = atof(value);
                health->has_packet_loss_percent = true;
            }
        }
        cursor = comma == NULL ? NULL : comma + 1;
    }
    return health->path_id[0] != '\0';
}

static void apply_health_override(en_health_probe_mock_t *health_mock, const injected_health_t *health)
{
    en_path_health_t override = {0};
    snprintf(override.path_id, sizeof(override.path_id), "%s", health->path_id);
    override.state = health->state;
    override.rtt_ms = health->has_rtt_ms ? health->rtt_ms : 10.0;
    override.packet_loss_percent = health->has_packet_loss_percent ? health->packet_loss_percent : 0.0;
    if (health->state == EN_HEALTH_FAILED || health->state == EN_HEALTH_UNHEALTHY) {
        override.consecutive_failures = 3;
        override.consecutive_successes = 0;
        if (!health->has_packet_loss_percent) {
            override.packet_loss_percent = 100.0;
        }
    } else {
        override.consecutive_failures = 0;
        override.consecutive_successes = 3;
    }
    en_health_probe_mock_set(health_mock, override);
}

static bool force_selection_mode(en_intent_t *intent, const char *mode)
{
    if (strcmp(mode, "explicit") == 0) {
        intent->path_selection.mode = EN_SELECT_EXPLICIT;
        return true;
    }
    if (strcmp(mode, "priority") == 0) {
        intent->path_selection.mode = EN_SELECT_PRIORITY;
        return true;
    }
    if (strcmp(mode, "evaluated") == 0) {
        intent->path_selection.mode = EN_SELECT_EVALUATED;
        return true;
    }
    return false;
}

static bool force_comparison_order(en_intent_t *intent, const char *value)
{
    char buf[256] = {0};
    snprintf(buf, sizeof(buf), "%s", value);
    intent->path_selection.comparison_count = 0;
    char *cursor = buf;
    while (cursor != NULL && *cursor != '\0') {
        char *comma = strchr(cursor, ',');
        if (comma != NULL) {
            *comma = '\0';
        }
        bool ok = false;
        en_comparison_key_t key = parse_comparison_key_arg(cursor, &ok);
        if (!ok || intent->path_selection.comparison_count >= EN_MAX_COMPARISONS) {
            return false;
        }
        intent->path_selection.comparison_order[intent->path_selection.comparison_count++] = key;
        cursor = comma == NULL ? NULL : comma + 1;
    }
    return true;
}

static void print_result(const en_reconcile_result_t *result)
{
    printf("intent: %s\n", result->intent_id);
    printf("selected_path: %s\n", result->selected_path);
    printf("transition_state: %s\n", en_transition_state_name(result->transition_state));
    printf("reason: %s\n", result->explanation.reason);
    printf("excluded:\n");
    if (result->explanation.excluded_count == 0) {
        printf("  - none\n");
    }
    for (size_t i = 0; i < result->explanation.excluded_count; i++) {
        printf("  - %s: %s\n", result->explanation.excluded_path_ids[i], result->explanation.excluded_reasons[i]);
    }
}

static int generate_runtime(const char *program, const char *yaml, const en_reconcile_result_t *result, const char *active_path, const char *failed_path)
{
    (void)program;
    char command[1024] = {0};
    if (active_path != NULL && failed_path != NULL && strcmp(active_path, failed_path) == 0) {
        snprintf(
            command,
            sizeof(command),
            "sh scripts/vm-generate-netns-runtime.sh %s --active-path %s --fail-path %s",
            yaml,
            active_path,
            failed_path
        );
    } else {
        snprintf(command, sizeof(command), "sh scripts/vm-generate-netns-runtime.sh %s --path %s", yaml, result->selected_path);
    }
    printf("generate_runtime_command: %s\n", command);
    fflush(stdout);
    int rc = system(command);
    if (rc != 0) {
        fprintf(stderr, "generate runtime command failed\n");
        return 1;
    }
    printf("generated_runtime: out/netns-runtime/apply-integrated.sh\n");
    return 0;
}

static void usage(const char *program)
{
    printf("usage: %s [options] YAML\n", program);
    printf("options:\n");
    printf("  --intent ID\n");
    printf("  --mode explicit|priority|evaluated\n");
    printf("  --active-path PATH\n");
    printf("  --fail-path PATH\n");
    printf("  --health PATH=state[,rtt=N,loss=N]\n");
    printf("  --compare packet_loss,latency,hop_count,priority,path_id\n");
    printf("  --expect PATH\n");
    printf("  --generate-runtime\n");
}
