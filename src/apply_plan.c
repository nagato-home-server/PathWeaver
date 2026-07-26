#include "eventnet/apply_plan.h"
#include "eventnet/render_commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const en_tunnel_t *find_tunnel(const en_yaml_config_t *config, const char *tunnel_id);
static void append_conf(char *dst, size_t dst_len, const char *text);
static void append_command(en_apply_plan_t *plan, const char *command);
static void append_rollback_command(en_apply_plan_t *plan, const char *command);
static en_error_code_t append_tunnel_conf(en_apply_plan_t *plan, const en_tunnel_t *tunnel);

en_error_code_t en_apply_plan_from_config(
    const en_yaml_config_t *config,
    const en_intent_t *intent,
    const en_path_t *selected_path,
    en_apply_plan_t *plan
)
{
    return en_apply_plan_from_config_with_file(config, intent, selected_path, "eventnet-swanctl.conf", plan);
}

en_error_code_t en_apply_plan_from_config_with_file(
    const en_yaml_config_t *config,
    const en_intent_t *intent,
    const en_path_t *selected_path,
    const char *swanctl_conf_filename,
    en_apply_plan_t *plan
)
{
    if (config == NULL || intent == NULL || selected_path == NULL || swanctl_conf_filename == NULL || plan == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }

    memset(plan, 0, sizeof(*plan));
    append_conf(plan->swanctl_conf, sizeof(plan->swanctl_conf), "connections {\n");

    for (size_t i = 0; i < selected_path->segment_count; i++) {
        const en_segment_t *segment = &selected_path->segments[i];
        const en_tunnel_t *tunnel = find_tunnel(config, segment->tunnel_id);
        if (tunnel == NULL) {
            return EN_ERR_NOT_FOUND;
        }
        en_error_code_t err = append_tunnel_conf(plan, tunnel);
        if (err != EN_ERR_NONE) {
            return err;
        }
    }

    append_conf(plan->swanctl_conf, sizeof(plan->swanctl_conf), "}\n\nsecrets {\n");
    for (size_t i = 0; i < selected_path->segment_count; i++) {
        const en_tunnel_t *tunnel = find_tunnel(config, selected_path->segments[i].tunnel_id);
        if (tunnel == NULL) {
            return EN_ERR_NOT_FOUND;
        }
        if (tunnel->psk[0] != '\0') {
            char secret[512] = {0};
            snprintf(
                secret,
                sizeof(secret),
                "  ike-%s {\n"
                "    id-1 = %s\n"
                "    id-2 = %s\n"
                "    secret = %s\n"
                "  }\n",
                tunnel->tunnel_id,
                tunnel->local_id[0] == '\0' ? tunnel->local_endpoint : tunnel->local_id,
                tunnel->remote_id[0] == '\0' ? tunnel->remote_endpoint : tunnel->remote_id,
                tunnel->psk
            );
            append_conf(plan->swanctl_conf, sizeof(plan->swanctl_conf), secret);
        }
    }
    append_conf(plan->swanctl_conf, sizeof(plan->swanctl_conf), "}\n");
    char load_command[EN_MAX_COMMAND_LEN] = {0};
    snprintf(load_command, sizeof(load_command), "swanctl --load-conns --file %s", swanctl_conf_filename);
    append_command(plan, load_command);

    for (size_t i = 0; i < selected_path->segment_count; i++) {
        const en_tunnel_t *tunnel = find_tunnel(config, selected_path->segments[i].tunnel_id);
        char command[EN_MAX_COMMAND_LEN] = {0};
        en_error_code_t err = en_render_swanctl_initiate(tunnel, command, sizeof(command));
        if (err != EN_ERR_NONE) {
            return err;
        }
        append_command(plan, command);
    }

    const en_tunnel_t *egress_tunnel = find_tunnel(config, selected_path->egress_tunnel_id);
    char vpp_command[EN_MAX_COMMAND_LEN] = {0};
    en_error_code_t err = en_render_vpp_route_replace(selected_path, egress_tunnel, vpp_command, sizeof(vpp_command));
    if (err != EN_ERR_NONE) {
        return err;
    }
    append_command(plan, vpp_command);

    char delete_route_command[EN_MAX_COMMAND_LEN] = {0};
    err = en_render_vpp_route_delete(selected_path, delete_route_command, sizeof(delete_route_command));
    if (err == EN_ERR_NONE) {
        append_rollback_command(plan, delete_route_command);
    }
    for (size_t i = selected_path->segment_count; i > 0; i--) {
        const en_tunnel_t *tunnel = find_tunnel(config, selected_path->segments[i - 1].tunnel_id);
        char terminate_command[EN_MAX_COMMAND_LEN] = {0};
        err = en_render_swanctl_terminate(tunnel, terminate_command, sizeof(terminate_command));
        if (err != EN_ERR_NONE) {
            return err;
        }
        append_rollback_command(plan, terminate_command);
    }

    (void)intent;
    return EN_ERR_NONE;
}

en_error_code_t en_apply_plan_write_swanctl_conf(
    const en_apply_plan_t *plan,
    const char *filename
)
{
    if (plan == NULL || filename == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        return EN_ERR_STATE_CONFLICT;
    }
    fputs(plan->swanctl_conf, file);
    fclose(file);
    return EN_ERR_NONE;
}

en_error_code_t en_apply_plan_write_shell_script(
    const en_apply_plan_t *plan,
    const char *filename
)
{
    if (plan == NULL || filename == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        return EN_ERR_STATE_CONFLICT;
    }
    fputs("#!/usr/bin/env sh\n", file);
    fputs("set -eu\n\n", file);
    fputs("if [ \"$(id -u)\" != \"0\" ]; then\n", file);
    fputs("  echo \"warning: applying IPsec/VPP routes usually requires root\" >&2\n", file);
    fputs("fi\n", file);
    fputs("command -v swanctl >/dev/null\n", file);
    fputs("command -v vppctl >/dev/null\n\n", file);
    for (size_t i = 0; i < plan->command_count; i++) {
        fprintf(file, "%s\n", plan->commands[i]);
    }
    if (plan->rollback_command_count > 0) {
        fputs("\n# Rollback commands, run manually if validation fails:\n", file);
        for (size_t i = 0; i < plan->rollback_command_count; i++) {
            fprintf(file, "# %s\n", plan->rollback_commands[i]);
        }
    }
    fclose(file);
    return EN_ERR_NONE;
}

en_error_code_t en_apply_plan_run(
    const en_apply_plan_t *plan,
    bool dry_run
)
{
    if (plan == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < plan->command_count; i++) {
        if (dry_run) {
            printf("[dry-run] %s\n", plan->commands[i]);
            continue;
        }
        int rc = system(plan->commands[i]);
        if (rc != 0) {
            return EN_ERR_STATE_CONFLICT;
        }
    }
    return EN_ERR_NONE;
}

static const en_tunnel_t *find_tunnel(const en_yaml_config_t *config, const char *tunnel_id)
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

static en_error_code_t append_tunnel_conf(en_apply_plan_t *plan, const en_tunnel_t *tunnel)
{
    if (tunnel == NULL) {
        return EN_ERR_NOT_FOUND;
    }
    char block[1024] = {0};
    snprintf(
        block,
        sizeof(block),
        "  %s {\n"
        "    version = 2\n"
        "    local_addrs = %s\n"
        "    remote_addrs = %s\n"
        "    local {\n"
        "      auth = psk\n"
        "      id = %s\n"
        "    }\n"
        "    remote {\n"
        "      auth = psk\n"
        "      id = %s\n"
        "    }\n"
        "    children {\n"
        "      %s {\n"
        "        local_ts = %s\n"
        "        remote_ts = %s\n"
        "        start_action = trap\n"
        "      }\n"
        "    }\n"
        "  }\n",
        tunnel->tunnel_id,
        tunnel->local_endpoint,
        tunnel->remote_endpoint,
        tunnel->local_id[0] == '\0' ? tunnel->local_endpoint : tunnel->local_id,
        tunnel->remote_id[0] == '\0' ? tunnel->remote_endpoint : tunnel->remote_id,
        tunnel->tunnel_id,
        tunnel->local_traffic_selector,
        tunnel->remote_traffic_selector
    );
    append_conf(plan->swanctl_conf, sizeof(plan->swanctl_conf), block);
    return EN_ERR_NONE;
}

static void append_conf(char *dst, size_t dst_len, const char *text)
{
    size_t used = strlen(dst);
    if (used >= dst_len - 1) {
        return;
    }
    snprintf(dst + used, dst_len - used, "%s", text);
}

static void append_command(en_apply_plan_t *plan, const char *command)
{
    if (plan->command_count >= EN_MAX_PLAN_COMMANDS) {
        return;
    }
    snprintf(plan->commands[plan->command_count++], EN_MAX_COMMAND_LEN, "%s", command);
}

static void append_rollback_command(en_apply_plan_t *plan, const char *command)
{
    if (plan->rollback_command_count >= EN_MAX_PLAN_COMMANDS) {
        return;
    }
    snprintf(plan->rollback_commands[plan->rollback_command_count++], EN_MAX_COMMAND_LEN, "%s", command);
}
