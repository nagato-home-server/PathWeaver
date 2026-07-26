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

static const en_tunnel_t *find_tunnel(const en_yaml_config_t *config, const char *tunnel_id)
{
    for (size_t i = 0; i < config->tunnel_count; i++) {
        if (strcmp(config->tunnels[i].tunnel_id, tunnel_id) == 0) {
            return &config->tunnels[i];
        }
    }
    return NULL;
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

static const char *runtime_kind(const en_path_t *path)
{
    if (path->segment_count == 1 && strcmp(path->segments[0].tunnel_id, "tun-a-b") == 0) {
        return "direct";
    }
    if (
        path->segment_count == 2 &&
        strcmp(path->segments[0].tunnel_id, "tun-a-hub") == 0 &&
        strcmp(path->segments[1].tunnel_id, "tun-hub-b") == 0
    ) {
        return "hub";
    }
    return "unsupported";
}

static int write_apply_script(const char *filename, const en_path_t *path, const char *kind)
{
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        return 1;
    }
    fprintf(file, "#!/usr/bin/env sh\n");
    fprintf(file, "set -eu\n\n");
    fprintf(file, "ROOT_DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")/../..\" && pwd)\n");
    fprintf(file, "cd \"$ROOT_DIR\"\n\n");
    fprintf(file, "printf 'eventnet selected path: %s\\n'\n", path->path_id);
    fprintf(file, "printf 'eventnet runtime kind: %s\\n'\n\n", kind);
    if (strcmp(kind, "direct") == 0) {
        fprintf(file, "sudo sh scripts/vm-netns-ipsec-hub-stop.sh 2>/dev/null || true\n");
        fprintf(file, "sudo sh scripts/vm-netns-ipsec-direct-start.sh\n");
        fprintf(file, "sudo sh scripts/vm-netns-ipsec-direct-smoke.sh\n");
    } else if (strcmp(kind, "hub") == 0) {
        fprintf(file, "sudo sh scripts/vm-netns-ipsec-direct-stop.sh 2>/dev/null || true\n");
        fprintf(file, "sudo sh scripts/vm-netns-ipsec-hub-start.sh\n");
        fprintf(file, "sudo sh scripts/vm-netns-ipsec-hub-smoke.sh\n");
    } else {
        fprintf(file, "echo 'unsupported path for current netns runtime: %s' >&2\n", path->path_id);
        fprintf(file, "exit 1\n");
    }
    fclose(file);
    return 0;
}

static int write_summary(const char *filename, const en_yaml_config_t *config, const en_path_t *path, const char *kind)
{
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        return 1;
    }
    fprintf(file, "selected_path: %s\n", path->path_id);
    fprintf(file, "runtime_kind: %s\n", kind);
    fprintf(file, "source: %s\n", path->source);
    fprintf(file, "destination: %s\n", path->destination);
    fprintf(file, "route_destination_prefix: %s\n", path->route_destination_prefix);
    fprintf(file, "route_next_hop: %s\n", path->route_next_hop);
    fprintf(file, "segments:\n");
    for (size_t i = 0; i < path->segment_count; i++) {
        const en_segment_t *segment = &path->segments[i];
        const en_tunnel_t *tunnel = find_tunnel(config, segment->tunnel_id);
        fprintf(file, "  - id: %s\n", segment->segment_id);
        fprintf(file, "    from: %s\n", segment->from_node);
        fprintf(file, "    to: %s\n", segment->to_node);
        fprintf(file, "    tunnel_id: %s\n", segment->tunnel_id);
        if (tunnel != NULL) {
            fprintf(file, "    local_endpoint: %s\n", tunnel->local_endpoint);
            fprintf(file, "    remote_endpoint: %s\n", tunnel->remote_endpoint);
            fprintf(file, "    local_ts: %s\n", tunnel->local_traffic_selector);
            fprintf(file, "    remote_ts: %s\n", tunnel->remote_traffic_selector);
        }
    }
    fclose(file);
    return 0;
}

static int write_vpp_route_plan(const char *filename, const en_yaml_config_t *config, const en_path_t *path)
{
    const en_tunnel_t *first_tunnel = NULL;
    const en_tunnel_t *last_tunnel = NULL;
    if (path->segment_count > 0) {
        first_tunnel = find_tunnel(config, path->segments[0].tunnel_id);
        last_tunnel = find_tunnel(config, path->segments[path->segment_count - 1].tunnel_id);
    }
    if (first_tunnel == NULL || last_tunnel == NULL) {
        return 1;
    }

    const char *source_prefix = first_tunnel->local_traffic_selector;
    const char *destination_prefix = path->route_destination_prefix[0] == '\0' ?
        last_tunnel->remote_traffic_selector :
        path->route_destination_prefix;

    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        return 1;
    }
    fprintf(file, "#!/usr/bin/env sh\n");
    fprintf(file, "set -eu\n\n");
    fprintf(file, "VPPCTL=\"${VPPCTL:-vppctl}\"\n");
    fprintf(file, "DRY_RUN=\"${DRY_RUN:-1}\"\n\n");
    fprintf(file, "run_vpp() {\n");
    fprintf(file, "  if [ \"$DRY_RUN\" = \"1\" ]; then\n");
    fprintf(file, "    printf '[dry-run] %%s %%s\\n' \"$VPPCTL\" \"$*\"\n");
    fprintf(file, "  else\n");
    fprintf(file, "    \"$VPPCTL\" \"$@\"\n");
    fprintf(file, "  fi\n");
    fprintf(file, "}\n\n");
    fprintf(file, "printf 'VPP route plan for path: %s\\n'\n", path->path_id);
    fprintf(file, "printf 'source_prefix: %s\\n'\n", source_prefix);
    fprintf(file, "printf 'destination_prefix: %s\\n'\n\n", destination_prefix);

    if (path->segment_count == 1) {
        fprintf(file, "printf '# node %s: destination route\\n'\n", path->source);
        fprintf(file, "run_vpp ip route add %s via %s\n", destination_prefix, first_tunnel->remote_endpoint);
        fprintf(file, "printf '# node %s: source return route\\n'\n", path->destination);
        fprintf(file, "run_vpp ip route add %s via %s\n", source_prefix, first_tunnel->local_endpoint);
    } else {
        fprintf(file, "printf '# node %s: destination route to first waypoint\\n'\n", path->source);
        fprintf(file, "run_vpp ip route add %s via %s\n", destination_prefix, first_tunnel->remote_endpoint);
        for (size_t i = 0; i < path->segment_count - 1; i++) {
            const en_segment_t *incoming_segment = &path->segments[i];
            const en_segment_t *outgoing_segment = &path->segments[i + 1];
            const en_tunnel_t *incoming_tunnel = find_tunnel(config, incoming_segment->tunnel_id);
            const en_tunnel_t *outgoing_tunnel = find_tunnel(config, outgoing_segment->tunnel_id);
            if (incoming_tunnel == NULL || outgoing_tunnel == NULL) {
                fclose(file);
                return 1;
            }
            fprintf(file, "printf '# node %s: source return route\\n'\n", incoming_segment->to_node);
            fprintf(file, "run_vpp ip route add %s via %s\n", source_prefix, incoming_tunnel->local_endpoint);
            fprintf(file, "printf '# node %s: destination route\\n'\n", outgoing_segment->from_node);
            fprintf(file, "run_vpp ip route add %s via %s\n", destination_prefix, outgoing_tunnel->remote_endpoint);
        }
        fprintf(file, "printf '# node %s: source return route\\n'\n", path->destination);
        fprintf(file, "run_vpp ip route add %s via %s\n", source_prefix, last_tunnel->local_endpoint);
    }

    fclose(file);
    return 0;
}

static int write_vpp_netns_route_plan(const char *filename, const en_yaml_config_t *config, const en_path_t *path)
{
    const en_tunnel_t *first_tunnel = NULL;
    const en_tunnel_t *last_tunnel = NULL;
    if (path->segment_count > 0) {
        first_tunnel = find_tunnel(config, path->segments[0].tunnel_id);
        last_tunnel = find_tunnel(config, path->segments[path->segment_count - 1].tunnel_id);
    }
    if (first_tunnel == NULL || last_tunnel == NULL) {
        return 1;
    }

    const char *source_prefix = first_tunnel->local_traffic_selector;
    const char *destination_prefix = path->route_destination_prefix[0] == '\0' ?
        last_tunnel->remote_traffic_selector :
        path->route_destination_prefix;

    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        return 1;
    }
    fprintf(file, "#!/usr/bin/env sh\n");
    fprintf(file, "set -eu\n\n");
    fprintf(file, "VPPCTL=\"${VPPCTL:-vppctl}\"\n");
    fprintf(file, "DRY_RUN=\"${DRY_RUN:-1}\"\n\n");
    fprintf(file, "run_vpp() {\n");
    fprintf(file, "  if [ \"$DRY_RUN\" = \"1\" ]; then\n");
    fprintf(file, "    printf '[dry-run] %%s %%s\\n' \"$VPPCTL\" \"$*\"\n");
    fprintf(file, "  else\n");
    fprintf(file, "    \"$VPPCTL\" \"$@\"\n");
    fprintf(file, "  fi\n");
    fprintf(file, "}\n\n");
    fprintf(file, "printf 'VPP netns route plan for path: %s\\n'\n", path->path_id);
    fprintf(file, "printf 'source_prefix: %s via site-a VPP edge\\n'\n", source_prefix);
    fprintf(file, "printf 'destination_prefix: %s via site-b VPP edge\\n'\n\n", destination_prefix);
    fprintf(file, "run_vpp ip route add %s via 172.16.1.2\n", source_prefix);
    fprintf(file, "run_vpp ip route add %s via 172.16.2.2\n", destination_prefix);
    fclose(file);
    return 0;
}

static void set_failed_health(en_health_probe_mock_t *health_mock, const char *path_id)
{
    en_path_health_t health = {0};
    snprintf(health.path_id, sizeof(health.path_id), "%s", path_id);
    health.state = EN_HEALTH_FAILED;
    health.rtt_ms = 0.0;
    health.packet_loss_percent = 100.0;
    health.consecutive_failures = 3;
    health.consecutive_successes = 0;
    en_health_probe_mock_set(health_mock, health);
}

static void usage(const char *program)
{
    printf("usage: %s [--intent ID] [--path PATH_ID] [--active-path PATH_ID] [--fail-path PATH_ID] [--out-dir DIR] YAML\n", program);
}

int main(int argc, char **argv)
{
    const char *filename = "samples/linux-vm-netns.yaml";
    const char *intent_id = NULL;
    const char *forced_path_id = NULL;
    const char *active_path_id = NULL;
    const char *failed_paths[EN_MAX_PATHS] = {0};
    size_t failed_path_count = 0;
    const char *out_dir = "out/netns-runtime";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--intent") == 0 && i + 1 < argc) {
            intent_id = argv[++i];
        } else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            forced_path_id = argv[++i];
        } else if (strcmp(argv[i], "--active-path") == 0 && i + 1 < argc) {
            active_path_id = argv[++i];
        } else if (strcmp(argv[i], "--fail-path") == 0 && i + 1 < argc) {
            if (failed_path_count >= EN_MAX_PATHS) {
                fprintf(stderr, "too many --fail-path values\n");
                return 1;
            }
            failed_paths[failed_path_count++] = argv[++i];
        } else if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
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

    const en_intent_t *intent = find_intent(&config, intent_id);
    if (intent == NULL) {
        fprintf(stderr, "intent not found\n");
        return 1;
    }

    const en_path_t *selected_path = NULL;
    char selected_path_id[EN_MAX_ID_LEN] = {0};
    char reason[128] = "forced path";
    if (forced_path_id != NULL) {
        selected_path = find_path(&config, forced_path_id);
        if (selected_path == NULL) {
            fprintf(stderr, "path not found: %s\n", forced_path_id);
            return 1;
        }
        snprintf(selected_path_id, sizeof(selected_path_id), "%s", forced_path_id);
    } else {
        en_vpp_mock_t vpp_mock = {0};
        en_health_probe_mock_t health_mock = {0};
        for (size_t i = 0; i < failed_path_count; i++) {
            set_failed_health(&health_mock, failed_paths[i]);
        }
        if (
            active_path_id != NULL &&
            failed_path_count > 0 &&
            intent->fallback.enabled &&
            intent->fallback.path_id[0] != '\0'
        ) {
            for (size_t i = 0; i < failed_path_count; i++) {
                if (strcmp(active_path_id, failed_paths[i]) == 0) {
                    selected_path = find_path(&config, intent->fallback.path_id);
                    if (selected_path == NULL) {
                        fprintf(stderr, "fallback path not found: %s\n", intent->fallback.path_id);
                        return 1;
                    }
                    snprintf(selected_path_id, sizeof(selected_path_id), "%s", selected_path->path_id);
                    snprintf(reason, sizeof(reason), "active path %s failed; using fallback %s", active_path_id, selected_path->path_id);
                    break;
                }
            }
        }
        if (selected_path != NULL) {
            goto selected;
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
        en_reconcile_result_t result = {0};
        err = en_controller_submit_intent(controller, intent, &result);
        en_controller_destroy(controller);
        if (err != EN_ERR_NONE) {
            fprintf(stderr, "intent reconcile failed: %s\n", en_error_code_name(err));
            return 1;
        }
        snprintf(selected_path_id, sizeof(selected_path_id), "%s", result.selected_path);
        selected_path = find_path(&config, selected_path_id);
        snprintf(reason, sizeof(reason), "%s", result.explanation.reason);
    }

selected:
    if (selected_path == NULL) {
        fprintf(stderr, "selected path not found: %s\n", selected_path_id);
        return 1;
    }

    const char *kind = runtime_kind(selected_path);
    char apply_script[256] = {0};
    char summary[256] = {0};
    char vpp_plan[256] = {0};
    char vpp_netns_plan[256] = {0};
    snprintf(apply_script, sizeof(apply_script), "%s/apply-selected.sh", out_dir);
    snprintf(summary, sizeof(summary), "%s/selected-path.txt", out_dir);
    snprintf(vpp_plan, sizeof(vpp_plan), "%s/vpp-route-plan.sh", out_dir);
    snprintf(vpp_netns_plan, sizeof(vpp_netns_plan), "%s/vpp-netns-route-plan.sh", out_dir);

    if (write_apply_script(apply_script, selected_path, kind) != 0) {
        fprintf(stderr, "failed to write %s\n", apply_script);
        return 1;
    }
    if (write_summary(summary, &config, selected_path, kind) != 0) {
        fprintf(stderr, "failed to write %s\n", summary);
        return 1;
    }
    if (write_vpp_route_plan(vpp_plan, &config, selected_path) != 0) {
        fprintf(stderr, "failed to write %s\n", vpp_plan);
        return 1;
    }
    if (write_vpp_netns_route_plan(vpp_netns_plan, &config, selected_path) != 0) {
        fprintf(stderr, "failed to write %s\n", vpp_netns_plan);
        return 1;
    }

    printf("intent: %s\n", intent->intent_id);
    printf("selected_path: %s\n", selected_path->path_id);
    printf("runtime_kind: %s\n", kind);
    printf("reason: %s\n", reason);
    printf("wrote: %s\n", apply_script);
    printf("wrote: %s\n", summary);
    printf("wrote: %s\n", vpp_plan);
    printf("wrote: %s\n", vpp_netns_plan);

    return strcmp(kind, "unsupported") == 0 ? 2 : 0;
}
