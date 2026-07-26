#include "eventnet/apply_plan.h"
#include "eventnet/controller.h"
#include "eventnet/command_adapters.h"
#include "eventnet/mock_adapters.h"
#include "eventnet/render_commands.h"
#include "eventnet/topology.h"
#include "eventnet/yaml_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "assert failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#define ASSERT_STREQ(left, right) ASSERT_TRUE(strcmp((left), (right)) == 0)

static en_controller_t *make_controller(en_vpp_mock_t *vpp_mock, en_health_probe_mock_t *health_mock)
{
    en_path_t paths[EN_MAX_PATHS];
    size_t path_count = en_initial_demo_paths(paths, EN_MAX_PATHS);
    en_controller_t *controller = en_controller_create(
        paths,
        path_count,
        en_strongswan_mock_adapter(),
        en_vpp_mock_adapter(vpp_mock),
        en_health_probe_mock_adapter(health_mock)
    );
    ASSERT_TRUE(controller != NULL);
    return controller;
}

static en_intent_t base_intent(en_path_selection_mode_t mode)
{
    en_intent_t intent = {0};
    snprintf(intent.intent_id, sizeof(intent.intent_id), "%s", "intent-a-b");
    snprintf(intent.traffic.source, sizeof(intent.traffic.source), "%s", "site-a");
    snprintf(intent.traffic.destination, sizeof(intent.traffic.destination), "%s", "site-b");
    intent.path_selection.mode = mode;
    intent.transition.strategy = EN_TRANSITION_IMMEDIATE;
    intent.transition.max_pause_ms = 0;
    intent.transition.drain_timeout_ms = 0;
    intent.transition.timeout_ms = 5000;
    intent.fallback.enabled = true;
    snprintf(intent.fallback.path_id, sizeof(intent.fallback.path_id), "%s", "path-via-hub");
    return intent;
}

static void test_priority_selects_first_healthy_path(void)
{
    en_vpp_mock_t vpp_mock = {0};
    en_health_probe_mock_t health_mock = {0};
    en_controller_t *controller = make_controller(&vpp_mock, &health_mock);
    en_intent_t intent = base_intent(EN_SELECT_PRIORITY);
    intent.path_selection.candidate_count = 2;
    snprintf(intent.path_selection.candidates[0], sizeof(intent.path_selection.candidates[0]), "%s", "path-direct");
    snprintf(intent.path_selection.candidates[1], sizeof(intent.path_selection.candidates[1]), "%s", "path-via-hub");

    en_reconcile_result_t result = {0};
    ASSERT_TRUE(en_controller_submit_intent(controller, &intent, &result) == EN_ERR_NONE);
    ASSERT_STREQ(result.selected_path, "path-direct");

    en_controller_destroy(controller);
}

static void test_evaluated_excludes_failed_path(void)
{
    en_vpp_mock_t vpp_mock = {0};
    en_health_probe_mock_t health_mock = {0};
    en_path_health_t failed = {0};
    snprintf(failed.path_id, sizeof(failed.path_id), "%s", "path-direct");
    failed.state = EN_HEALTH_FAILED;
    en_health_probe_mock_set(&health_mock, failed);

    en_controller_t *controller = make_controller(&vpp_mock, &health_mock);
    en_intent_t intent = base_intent(EN_SELECT_EVALUATED);
    intent.path_selection.candidate_count = 2;
    snprintf(intent.path_selection.candidates[0], sizeof(intent.path_selection.candidates[0]), "%s", "path-direct");
    snprintf(intent.path_selection.candidates[1], sizeof(intent.path_selection.candidates[1]), "%s", "path-via-relay-c");
    intent.path_selection.comparison_count = 2;
    intent.path_selection.comparison_order[0] = EN_COMPARE_HOP_COUNT;
    intent.path_selection.comparison_order[1] = EN_COMPARE_PATH_ID;

    en_reconcile_result_t result = {0};
    ASSERT_TRUE(en_controller_submit_intent(controller, &intent, &result) == EN_ERR_NONE);
    ASSERT_STREQ(result.selected_path, "path-via-relay-c");
    ASSERT_TRUE(result.explanation.excluded_count == 1);
    ASSERT_STREQ(result.explanation.excluded_path_ids[0], "path-direct");

    en_controller_destroy(controller);
}

static void test_failed_forwarding_rolls_back_to_previous_path(void)
{
    en_vpp_mock_t vpp_mock = {0};
    en_health_probe_mock_t health_mock = {0};
    en_controller_t *controller = make_controller(&vpp_mock, &health_mock);

    en_intent_t first = base_intent(EN_SELECT_EXPLICIT);
    snprintf(first.path_selection.path_id, sizeof(first.path_selection.path_id), "%s", "path-via-hub");
    en_reconcile_result_t first_result = {0};
    ASSERT_TRUE(en_controller_submit_intent(controller, &first, &first_result) == EN_ERR_NONE);

    en_intent_t second = base_intent(EN_SELECT_EXPLICIT);
    snprintf(second.intent_id, sizeof(second.intent_id), "%s", "intent-a-b-2");
    snprintf(second.path_selection.path_id, sizeof(second.path_selection.path_id), "%s", "path-direct");
    vpp_mock.fail_next_update = true;
    en_reconcile_result_t second_result = {0};
    ASSERT_TRUE(en_controller_submit_intent(controller, &second, &second_result) == EN_ERR_FORWARDING_UPDATE_FAILED);

    char traffic_key[EN_MAX_ID_LEN * 2] = {0};
    en_make_traffic_key(&first.traffic, traffic_key, sizeof(traffic_key));
    ASSERT_STREQ(en_controller_applied_path(controller, traffic_key), "path-via-hub");

    en_controller_destroy(controller);
}

static void test_yaml_config_loads_paths_and_intents(void)
{
    const char *filename = "eventnet-test-routes.yaml";
    FILE *file = fopen(filename, "w");
    ASSERT_TRUE(file != NULL);
    fputs(
        "tunnels:\n"
        "  - id: tun-a-b\n"
        "    local_node: site-a\n"
        "    remote_node: site-b\n"
        "    local_endpoint: 203.0.113.10\n"
        "    remote_endpoint: 203.0.113.20\n"
        "    local_ts: 10.0.1.0/24\n"
        "    remote_ts: 10.0.2.0/24\n"
        "  - id: tun-a-hub\n"
        "    local_node: site-a\n"
        "    remote_node: hub-1\n"
        "    local_endpoint: 203.0.113.10\n"
        "    remote_endpoint: 203.0.113.1\n"
        "  - id: tun-hub-b\n"
        "    local_node: hub-1\n"
        "    remote_node: site-b\n"
        "    remote_endpoint: 203.0.113.20\n"
        "paths:\n"
        "  - id: path-direct\n"
        "    source: site-a\n"
        "    destination: site-b\n"
        "    priority: 10\n"
        "    segments:\n"
        "      - id: seg-a-b\n"
        "        from: site-a\n"
        "        to: site-b\n"
        "        tunnel_id: tun-a-b\n"
        "  - id: path-via-hub\n"
        "    source: site-a\n"
        "    destination: site-b\n"
        "    waypoints:\n"
        "      - hub-1\n"
        "    priority: 30\n"
        "    segments:\n"
        "      - id: seg-a-hub\n"
        "        from: site-a\n"
        "        to: hub-1\n"
        "        tunnel_id: tun-a-hub\n"
        "      - id: seg-hub-b\n"
        "        from: hub-1\n"
        "        to: site-b\n"
        "        tunnel_id: tun-hub-b\n"
        "intents:\n"
        "  - id: intent-a-b\n"
        "    traffic:\n"
        "      source: site-a\n"
        "      destination: site-b\n"
        "    path_selection:\n"
        "      mode: priority\n"
        "      candidates:\n"
        "        - path-direct\n"
        "        - path-via-hub\n"
        "    transition:\n"
        "      strategy: immediate\n"
        "    fallback:\n"
        "      enabled: true\n"
        "      path_id: path-via-hub\n",
        file
    );
    fclose(file);

    en_yaml_config_t config = {0};
    char error[256] = {0};
    ASSERT_TRUE(en_yaml_config_load_file(filename, &config, error, sizeof(error)) == EN_ERR_NONE);
    ASSERT_TRUE(config.tunnel_count == 3);
    ASSERT_TRUE(config.path_count == 2);
    ASSERT_TRUE(config.intent_count == 1);
    ASSERT_STREQ(config.paths[0].path_id, "path-direct");
    ASSERT_STREQ(config.paths[1].waypoints[0], "hub-1");
    ASSERT_TRUE(config.intents[0].path_selection.mode == EN_SELECT_PRIORITY);
    ASSERT_STREQ(config.intents[0].path_selection.candidates[0], "path-direct");

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
    ASSERT_TRUE(controller != NULL);
    en_reconcile_result_t result = {0};
    ASSERT_TRUE(en_controller_submit_intent(controller, &config.intents[0], &result) == EN_ERR_NONE);
    ASSERT_STREQ(result.selected_path, "path-direct");

    en_controller_destroy(controller);
    remove(filename);
}

static void test_command_adapters_can_drive_controller_dry_run(void)
{
    en_path_t paths[EN_MAX_PATHS];
    size_t path_count = en_initial_demo_paths(paths, EN_MAX_PATHS);
    en_tunnel_t tunnels[1] = {0};
    snprintf(tunnels[0].tunnel_id, sizeof(tunnels[0].tunnel_id), "%s", "tun-a-b");
    snprintf(tunnels[0].remote_endpoint, sizeof(tunnels[0].remote_endpoint), "%s", "203.0.113.20");
    en_strongswan_command_ctx_t strongswan_ctx = {
        .dry_run = true,
        .ensure_tunnel_command = "swanctl --initiate --child {tunnel_id}",
        .remove_tunnel_command = "swanctl --terminate --child {tunnel_id}",
    };
    en_vpp_command_ctx_t vpp_ctx = {
        .dry_run = true,
        .tunnels = tunnels,
        .tunnel_count = 1,
    };
    en_health_probe_mock_t health_mock = {0};
    en_controller_t *controller = en_controller_create(
        paths,
        path_count,
        en_strongswan_command_adapter(&strongswan_ctx),
        en_vpp_command_adapter(&vpp_ctx),
        en_health_probe_mock_adapter(&health_mock)
    );
    ASSERT_TRUE(controller != NULL);

    en_intent_t intent = base_intent(EN_SELECT_EXPLICIT);
    snprintf(intent.path_selection.path_id, sizeof(intent.path_selection.path_id), "%s", "path-direct");
    en_reconcile_result_t result = {0};
    ASSERT_TRUE(en_controller_submit_intent(controller, &intent, &result) == EN_ERR_NONE);
    ASSERT_STREQ(result.selected_path, "path-direct");

    char traffic_key[EN_MAX_ID_LEN * 2] = {0};
    en_make_traffic_key(&intent.traffic, traffic_key, sizeof(traffic_key));
    ASSERT_STREQ(en_controller_applied_path(controller, traffic_key), "path-direct");
    en_controller_destroy(controller);
}

static void test_renderers_generate_swanctl_and_vppctl(void)
{
    en_tunnel_t tunnel = {0};
    snprintf(tunnel.tunnel_id, sizeof(tunnel.tunnel_id), "%s", "tun-a-b");
    snprintf(tunnel.local_endpoint, sizeof(tunnel.local_endpoint), "%s", "203.0.113.10");
    snprintf(tunnel.remote_endpoint, sizeof(tunnel.remote_endpoint), "%s", "203.0.113.20");
    snprintf(tunnel.local_traffic_selector, sizeof(tunnel.local_traffic_selector), "%s", "10.0.1.0/24");
    snprintf(tunnel.remote_traffic_selector, sizeof(tunnel.remote_traffic_selector), "%s", "10.0.2.0/24");

    en_path_t path = {0};
    snprintf(path.path_id, sizeof(path.path_id), "%s", "path-direct");
    snprintf(path.route_destination_prefix, sizeof(path.route_destination_prefix), "%s", "10.0.2.0/24");
    snprintf(path.egress_tunnel_id, sizeof(path.egress_tunnel_id), "%s", "tun-a-b");

    char command[512] = {0};
    ASSERT_TRUE(en_render_swanctl_initiate(&tunnel, command, sizeof(command)) == EN_ERR_NONE);
    ASSERT_STREQ(command, "swanctl --initiate --child tun-a-b");
    ASSERT_TRUE(en_render_vpp_route_replace(&path, &tunnel, command, sizeof(command)) == EN_ERR_NONE);
    ASSERT_STREQ(command, "vppctl ip route add 10.0.2.0/24 via 203.0.113.20");
}

static void test_apply_plan_generates_swanctl_conf_and_commands(void)
{
    en_yaml_config_t config = {0};
    snprintf(config.tunnels[0].tunnel_id, sizeof(config.tunnels[0].tunnel_id), "%s", "tun-a-b");
    snprintf(config.tunnels[0].local_node, sizeof(config.tunnels[0].local_node), "%s", "site-a");
    snprintf(config.tunnels[0].remote_node, sizeof(config.tunnels[0].remote_node), "%s", "site-b");
    snprintf(config.tunnels[0].local_endpoint, sizeof(config.tunnels[0].local_endpoint), "%s", "203.0.113.10");
    snprintf(config.tunnels[0].remote_endpoint, sizeof(config.tunnels[0].remote_endpoint), "%s", "203.0.113.20");
    snprintf(config.tunnels[0].local_traffic_selector, sizeof(config.tunnels[0].local_traffic_selector), "%s", "10.0.1.0/24");
    snprintf(config.tunnels[0].remote_traffic_selector, sizeof(config.tunnels[0].remote_traffic_selector), "%s", "10.0.2.0/24");
    config.tunnel_count = 1;

    snprintf(config.paths[0].path_id, sizeof(config.paths[0].path_id), "%s", "path-direct");
    snprintf(config.paths[0].source, sizeof(config.paths[0].source), "%s", "site-a");
    snprintf(config.paths[0].destination, sizeof(config.paths[0].destination), "%s", "site-b");
    snprintf(config.paths[0].route_destination_prefix, sizeof(config.paths[0].route_destination_prefix), "%s", "10.0.2.0/24");
    snprintf(config.paths[0].egress_tunnel_id, sizeof(config.paths[0].egress_tunnel_id), "%s", "tun-a-b");
    snprintf(config.paths[0].segments[0].segment_id, sizeof(config.paths[0].segments[0].segment_id), "%s", "seg-a-b");
    snprintf(config.paths[0].segments[0].tunnel_id, sizeof(config.paths[0].segments[0].tunnel_id), "%s", "tun-a-b");
    config.paths[0].segment_count = 1;
    config.path_count = 1;

    snprintf(config.intents[0].intent_id, sizeof(config.intents[0].intent_id), "%s", "intent-a-b");
    config.intents[0].path_selection.mode = EN_SELECT_EXPLICIT;
    snprintf(config.intents[0].path_selection.path_id, sizeof(config.intents[0].path_selection.path_id), "%s", "path-direct");
    config.intent_count = 1;

    char error[256] = {0};
    ASSERT_TRUE(en_yaml_config_validate(&config, error, sizeof(error)) == EN_ERR_NONE);

    en_apply_plan_t plan = {0};
    ASSERT_TRUE(en_apply_plan_from_config(&config, &config.intents[0], &config.paths[0], &plan) == EN_ERR_NONE);
    ASSERT_TRUE(strstr(plan.swanctl_conf, "connections") != NULL);
    ASSERT_TRUE(strstr(plan.swanctl_conf, "tun-a-b") != NULL);
    ASSERT_TRUE(plan.command_count == 3);
    ASSERT_STREQ(plan.commands[0], "swanctl --load-conns --file eventnet-swanctl.conf");
    ASSERT_STREQ(plan.commands[1], "swanctl --initiate --child tun-a-b");
    ASSERT_STREQ(plan.commands[2], "vppctl ip route add 10.0.2.0/24 via 203.0.113.20");
    ASSERT_TRUE(plan.rollback_command_count == 2);
    ASSERT_STREQ(plan.rollback_commands[0], "vppctl ip route del 10.0.2.0/24");
    ASSERT_STREQ(plan.rollback_commands[1], "swanctl --terminate --child tun-a-b");
    ASSERT_TRUE(en_apply_plan_write_shell_script(&plan, "eventnet-test-apply.sh") == EN_ERR_NONE);
    remove("eventnet-test-apply.sh");
}

static void test_yaml_validation_rejects_unknown_tunnel(void)
{
    en_yaml_config_t config = {0};
    snprintf(config.paths[0].path_id, sizeof(config.paths[0].path_id), "%s", "path-bad");
    snprintf(config.paths[0].source, sizeof(config.paths[0].source), "%s", "site-a");
    snprintf(config.paths[0].destination, sizeof(config.paths[0].destination), "%s", "site-b");
    snprintf(config.paths[0].segments[0].tunnel_id, sizeof(config.paths[0].segments[0].tunnel_id), "%s", "missing-tunnel");
    config.paths[0].segment_count = 1;
    config.path_count = 1;
    char error[256] = {0};
    ASSERT_TRUE(en_yaml_config_validate(&config, error, sizeof(error)) == EN_ERR_NOT_FOUND);
}

static void test_yaml_validation_rejects_duplicate_path_id(void)
{
    en_yaml_config_t config = {0};
    snprintf(config.tunnels[0].tunnel_id, sizeof(config.tunnels[0].tunnel_id), "%s", "tun-a-b");
    snprintf(config.tunnels[0].local_node, sizeof(config.tunnels[0].local_node), "%s", "site-a");
    snprintf(config.tunnels[0].remote_node, sizeof(config.tunnels[0].remote_node), "%s", "site-b");
    config.tunnel_count = 1;
    for (size_t i = 0; i < 2; i++) {
        snprintf(config.paths[i].path_id, sizeof(config.paths[i].path_id), "%s", "path-dup");
        snprintf(config.paths[i].source, sizeof(config.paths[i].source), "%s", "site-a");
        snprintf(config.paths[i].destination, sizeof(config.paths[i].destination), "%s", "site-b");
        snprintf(config.paths[i].segments[0].tunnel_id, sizeof(config.paths[i].segments[0].tunnel_id), "%s", "tun-a-b");
        config.paths[i].segment_count = 1;
    }
    config.path_count = 2;
    char error[256] = {0};
    ASSERT_TRUE(en_yaml_config_validate(&config, error, sizeof(error)) == EN_ERR_INVALID_ARGUMENT);
}

int main(void)
{
    test_priority_selects_first_healthy_path();
    test_evaluated_excludes_failed_path();
    test_failed_forwarding_rolls_back_to_previous_path();
    test_yaml_config_loads_paths_and_intents();
    test_command_adapters_can_drive_controller_dry_run();
    test_renderers_generate_swanctl_and_vppctl();
    test_apply_plan_generates_swanctl_conf_and_commands();
    test_yaml_validation_rejects_unknown_tunnel();
    test_yaml_validation_rejects_duplicate_path_id();
    printf("all tests passed\n");
    return 0;
}
