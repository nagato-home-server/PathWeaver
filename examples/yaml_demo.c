#include "eventnet/apply_plan.h"
#include "eventnet/controller.h"
#include "eventnet/mock_adapters.h"
#include "eventnet/yaml_config.h"

#include <stdio.h>
#include <string.h>

static const en_path_t *find_path(const en_yaml_config_t *config, const char *path_id)
{
    for (size_t i = 0; i < config->path_count; i++) {
        if (strcmp(config->paths[i].path_id, path_id) == 0) {
            return &config->paths[i];
        }
    }
    return NULL;
}

static void usage(const char *program)
{
    printf("usage: %s [--apply] [--conf FILE] [--emit-script FILE] [--intent ID] [YAML]\n", program);
    printf("  default is dry-run mode\n");
}

int main(int argc, char **argv)
{
    const char *filename = "samples/ipsec-routes.yaml";
    const char *conf_filename = "eventnet-swanctl.conf";
    const char *script_filename = NULL;
    const char *intent_filter = NULL;
    bool dry_run = true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--apply") == 0) {
            dry_run = false;
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (strcmp(argv[i], "--conf") == 0 && i + 1 < argc) {
            conf_filename = argv[++i];
        } else if (strcmp(argv[i], "--emit-script") == 0 && i + 1 < argc) {
            script_filename = argv[++i];
        } else if (strcmp(argv[i], "--intent") == 0 && i + 1 < argc) {
            intent_filter = argv[++i];
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

    en_vpp_mock_t vpp_mock = {0};
    en_health_probe_mock_t health_mock = {0};
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

    printf("loaded paths: %zu\n", config.path_count);
    printf("loaded tunnels: %zu\n", config.tunnel_count);
    printf("loaded intents: %zu\n", config.intent_count);
    bool matched_intent = false;
    for (size_t i = 0; i < config.intent_count; i++) {
        if (intent_filter != NULL && strcmp(config.intents[i].intent_id, intent_filter) != 0) {
            continue;
        }
        matched_intent = true;
        en_reconcile_result_t result = {0};
        err = en_controller_submit_intent(controller, &config.intents[i], &result);
        if (err != EN_ERR_NONE) {
            fprintf(stderr, "intent %s failed: %s\n", config.intents[i].intent_id, en_error_code_name(err));
            en_controller_destroy(controller);
            return 1;
        }
        printf("intent: %s\n", result.intent_id);
        printf("  selected_path: %s\n", result.selected_path);
        printf("  reason: %s\n", result.explanation.reason);

        const en_path_t *selected_path = find_path(&config, result.selected_path);
        en_apply_plan_t plan = {0};
        err = en_apply_plan_from_config_with_file(&config, &config.intents[i], selected_path, conf_filename, &plan);
        if (err != EN_ERR_NONE) {
            fprintf(stderr, "apply plan failed: %s\n", en_error_code_name(err));
            en_controller_destroy(controller);
            return 1;
        }
        err = en_apply_plan_write_swanctl_conf(&plan, conf_filename);
        if (err != EN_ERR_NONE) {
            fprintf(stderr, "failed to write swanctl conf: %s\n", en_error_code_name(err));
            en_controller_destroy(controller);
            return 1;
        }
        printf("  wrote: %s\n", conf_filename);
        if (script_filename != NULL) {
            err = en_apply_plan_write_shell_script(&plan, script_filename);
            if (err != EN_ERR_NONE) {
                fprintf(stderr, "failed to write script: %s\n", en_error_code_name(err));
                en_controller_destroy(controller);
                return 1;
            }
            printf("  wrote: %s\n", script_filename);
        }
        printf("  apply_plan:\n");
        for (size_t j = 0; j < plan.command_count; j++) {
            printf("    - %s\n", plan.commands[j]);
        }
        if (plan.rollback_command_count > 0) {
            printf("  rollback_plan:\n");
            for (size_t j = 0; j < plan.rollback_command_count; j++) {
                printf("    - %s\n", plan.rollback_commands[j]);
            }
        }
        if (!dry_run) {
            err = en_apply_plan_run(&plan, false);
            if (err != EN_ERR_NONE) {
                fprintf(stderr, "apply failed: %s\n", en_error_code_name(err));
                en_controller_destroy(controller);
                return 1;
            }
        }
    }
    if (!matched_intent) {
        fprintf(stderr, "intent not found: %s\n", intent_filter == NULL ? "(none)" : intent_filter);
        en_controller_destroy(controller);
        return 1;
    }

    en_controller_destroy(controller);
    return 0;
}
