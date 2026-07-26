#include "eventnet/command_adapters.h"
#include "eventnet/render_commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static en_error_code_t strongswan_ensure(void *ctx, const en_tunnel_t *desired, en_tunnel_t *observed);
static en_error_code_t strongswan_remove(void *ctx, const en_tunnel_t *desired, en_tunnel_t *observed);
static en_error_code_t vpp_install(void *ctx, const char *traffic_key, const en_path_t *path);
static const char *vpp_active_path(void *ctx, const char *traffic_key);
static en_error_code_t health_validate(void *ctx, const en_path_t *path, en_path_health_t *health);
static en_error_code_t run_template(const char *template_text, bool dry_run, const en_tunnel_t *tunnel, const en_path_t *path, const char *traffic_key);
static en_error_code_t run_command(const char *command, bool dry_run);
static void expand_template(char *out, size_t out_len, const char *template_text, const en_tunnel_t *tunnel, const en_path_t *path, const char *traffic_key);
static void replace_all(char *text, size_t text_len, const char *needle, const char *replacement);
static void append_text(char *dst, size_t dst_len, const char *src);
static void remember_active_path(en_vpp_command_ctx_t *ctx, const char *traffic_key, const char *path_id);
static const en_tunnel_t *find_ctx_tunnel(const en_vpp_command_ctx_t *ctx, const char *tunnel_id);

en_strongswan_adapter_t en_strongswan_command_adapter(en_strongswan_command_ctx_t *ctx)
{
    en_strongswan_adapter_t adapter = {
        .ensure_tunnel = strongswan_ensure,
        .remove_tunnel = strongswan_remove,
        .ctx = ctx,
    };
    return adapter;
}

en_vpp_adapter_t en_vpp_command_adapter(en_vpp_command_ctx_t *ctx)
{
    en_vpp_adapter_t adapter = {
        .install_path = vpp_install,
        .active_path = vpp_active_path,
        .ctx = ctx,
    };
    return adapter;
}

en_health_probe_t en_health_command_probe(en_health_command_ctx_t *ctx)
{
    en_health_probe_t probe = {
        .validate_path = health_validate,
        .ctx = ctx,
    };
    return probe;
}

static en_error_code_t strongswan_ensure(void *ctx, const en_tunnel_t *desired, en_tunnel_t *observed)
{
    en_strongswan_command_ctx_t *command_ctx = ctx;
    if (command_ctx == NULL || desired == NULL || observed == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    en_error_code_t err = EN_ERR_NONE;
    if (command_ctx->ensure_tunnel_command[0] != '\0') {
        err = run_template(command_ctx->ensure_tunnel_command, command_ctx->dry_run, desired, NULL, NULL);
    } else {
        char command[512] = {0};
        err = en_render_swanctl_initiate(desired, command, sizeof(command));
        if (err == EN_ERR_NONE) {
            err = run_command(command, command_ctx->dry_run);
        }
    }
    if (err != EN_ERR_NONE) {
        return err;
    }
    *observed = *desired;
    observed->state = EN_TUNNEL_ESTABLISHED;
    observed->health = EN_HEALTH_HEALTHY;
    return EN_ERR_NONE;
}

static en_error_code_t strongswan_remove(void *ctx, const en_tunnel_t *desired, en_tunnel_t *observed)
{
    en_strongswan_command_ctx_t *command_ctx = ctx;
    if (command_ctx == NULL || desired == NULL || observed == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    en_error_code_t err = EN_ERR_NONE;
    if (command_ctx->remove_tunnel_command[0] != '\0') {
        err = run_template(command_ctx->remove_tunnel_command, command_ctx->dry_run, desired, NULL, NULL);
    } else {
        char command[512] = {0};
        err = en_render_swanctl_terminate(desired, command, sizeof(command));
        if (err == EN_ERR_NONE) {
            err = run_command(command, command_ctx->dry_run);
        }
    }
    if (err != EN_ERR_NONE) {
        return err;
    }
    *observed = *desired;
    observed->state = EN_TUNNEL_ABSENT;
    observed->health = EN_HEALTH_UNKNOWN;
    return EN_ERR_NONE;
}

static en_error_code_t vpp_install(void *ctx, const char *traffic_key, const en_path_t *path)
{
    en_vpp_command_ctx_t *command_ctx = ctx;
    if (command_ctx == NULL || traffic_key == NULL || path == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    en_error_code_t err = EN_ERR_NONE;
    if (command_ctx->install_path_command[0] != '\0') {
        err = run_template(command_ctx->install_path_command, command_ctx->dry_run, NULL, path, traffic_key);
    } else {
        const en_tunnel_t *egress_tunnel = find_ctx_tunnel(command_ctx, path->egress_tunnel_id);
        char command[512] = {0};
        err = en_render_vpp_route_replace(path, egress_tunnel, command, sizeof(command));
        if (err == EN_ERR_NONE) {
            err = run_command(command, command_ctx->dry_run);
        }
    }
    if (err != EN_ERR_NONE) {
        return err;
    }
    remember_active_path(command_ctx, traffic_key, path->path_id);
    return EN_ERR_NONE;
}

static const char *vpp_active_path(void *ctx, const char *traffic_key)
{
    en_vpp_command_ctx_t *command_ctx = ctx;
    if (command_ctx == NULL || traffic_key == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < command_ctx->active_count; i++) {
        if (strcmp(command_ctx->traffic_keys[i], traffic_key) == 0) {
            return command_ctx->active_paths[i];
        }
    }
    return NULL;
}

static en_error_code_t health_validate(void *ctx, const en_path_t *path, en_path_health_t *health)
{
    en_health_command_ctx_t *command_ctx = ctx;
    if (command_ctx == NULL || path == NULL || health == NULL) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    en_error_code_t err = run_template(command_ctx->validate_path_command, command_ctx->dry_run, NULL, path, NULL);
    snprintf(health->path_id, sizeof(health->path_id), "%s", path->path_id);
    health->state = err == EN_ERR_NONE ? EN_HEALTH_HEALTHY : EN_HEALTH_FAILED;
    health->rtt_ms = 0.0;
    health->packet_loss_percent = err == EN_ERR_NONE ? 0.0 : 100.0;
    health->jitter_ms = 0.0;
    health->consecutive_successes = err == EN_ERR_NONE ? 1 : 0;
    health->consecutive_failures = err == EN_ERR_NONE ? 0 : 1;
    return err;
}

static en_error_code_t run_template(const char *template_text, bool dry_run, const en_tunnel_t *tunnel, const en_path_t *path, const char *traffic_key)
{
    if (template_text == NULL || template_text[0] == '\0') {
        return EN_ERR_NONE;
    }
    char command[512] = {0};
    expand_template(command, sizeof(command), template_text, tunnel, path, traffic_key);
    return run_command(command, dry_run);
}

static en_error_code_t run_command(const char *command, bool dry_run)
{
    if (dry_run) {
        printf("[dry-run] %s\n", command);
        return EN_ERR_NONE;
    }
    int rc = system(command);
    return rc == 0 ? EN_ERR_NONE : EN_ERR_STATE_CONFLICT;
}

static void expand_template(char *out, size_t out_len, const char *template_text, const en_tunnel_t *tunnel, const en_path_t *path, const char *traffic_key)
{
    snprintf(out, out_len, "%s", template_text);
    if (tunnel != NULL) {
        replace_all(out, out_len, "{tunnel_id}", tunnel->tunnel_id);
        replace_all(out, out_len, "{local_node}", tunnel->local_node);
        replace_all(out, out_len, "{remote_node}", tunnel->remote_node);
        replace_all(out, out_len, "{local_endpoint}", tunnel->local_endpoint);
        replace_all(out, out_len, "{remote_endpoint}", tunnel->remote_endpoint);
        replace_all(out, out_len, "{local_ts}", tunnel->local_traffic_selector);
        replace_all(out, out_len, "{remote_ts}", tunnel->remote_traffic_selector);
    }
    if (path != NULL) {
        replace_all(out, out_len, "{path_id}", path->path_id);
        replace_all(out, out_len, "{source}", path->source);
        replace_all(out, out_len, "{destination}", path->destination);
    }
    if (traffic_key != NULL) {
        replace_all(out, out_len, "{traffic_key}", traffic_key);
    }
}

static void replace_all(char *text, size_t text_len, const char *needle, const char *replacement)
{
    char buffer[512] = {0};
    char *cursor = text;
    while (*cursor != '\0') {
        char *found = strstr(cursor, needle);
        if (found == NULL) {
            append_text(buffer, sizeof(buffer), cursor);
            break;
        }
        *found = '\0';
        append_text(buffer, sizeof(buffer), cursor);
        append_text(buffer, sizeof(buffer), replacement == NULL ? "" : replacement);
        cursor = found + strlen(needle);
    }
    snprintf(text, text_len, "%s", buffer);
}

static void append_text(char *dst, size_t dst_len, const char *src)
{
    size_t used = strlen(dst);
    if (used >= dst_len - 1) {
        return;
    }
    snprintf(dst + used, dst_len - used, "%s", src == NULL ? "" : src);
}

static void remember_active_path(en_vpp_command_ctx_t *ctx, const char *traffic_key, const char *path_id)
{
    for (size_t i = 0; i < ctx->active_count; i++) {
        if (strcmp(ctx->traffic_keys[i], traffic_key) == 0) {
            snprintf(ctx->active_paths[i], sizeof(ctx->active_paths[i]), "%s", path_id);
            return;
        }
    }
    if (ctx->active_count < EN_MAX_CANDIDATES) {
        size_t idx = ctx->active_count++;
        snprintf(ctx->traffic_keys[idx], sizeof(ctx->traffic_keys[idx]), "%s", traffic_key);
        snprintf(ctx->active_paths[idx], sizeof(ctx->active_paths[idx]), "%s", path_id);
    }
}

static const en_tunnel_t *find_ctx_tunnel(const en_vpp_command_ctx_t *ctx, const char *tunnel_id)
{
    if (ctx == NULL || tunnel_id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->tunnel_count; i++) {
        if (strcmp(ctx->tunnels[i].tunnel_id, tunnel_id) == 0) {
            return &ctx->tunnels[i];
        }
    }
    return NULL;
}
