#include "eventnet/controller.h"
#include "eventnet/mock_adapters.h"
#include "eventnet/topology.h"

#include <stdio.h>
#include <string.h>

static en_intent_t make_demo_intent(void)
{
    en_intent_t intent = {0};
    snprintf(intent.intent_id, sizeof(intent.intent_id), "%s", "intent-a-b");
    snprintf(intent.traffic.source, sizeof(intent.traffic.source), "%s", "site-a");
    snprintf(intent.traffic.destination, sizeof(intent.traffic.destination), "%s", "site-b");
    intent.path_selection.mode = EN_SELECT_PRIORITY;
    intent.path_selection.candidate_count = 3;
    snprintf(intent.path_selection.candidates[0], sizeof(intent.path_selection.candidates[0]), "%s", "path-direct");
    snprintf(intent.path_selection.candidates[1], sizeof(intent.path_selection.candidates[1]), "%s", "path-via-relay-c");
    snprintf(intent.path_selection.candidates[2], sizeof(intent.path_selection.candidates[2]), "%s", "path-via-hub");
    intent.transition.strategy = EN_TRANSITION_GRACEFUL;
    intent.transition.max_pause_ms = 10;
    intent.transition.drain_timeout_ms = 10;
    intent.transition.timeout_ms = 5000;
    intent.fallback.enabled = true;
    snprintf(intent.fallback.path_id, sizeof(intent.fallback.path_id), "%s", "path-via-hub");
    return intent;
}

int main(void)
{
    en_path_t paths[EN_MAX_PATHS];
    size_t path_count = en_initial_demo_paths(paths, EN_MAX_PATHS);
    en_vpp_mock_t vpp_mock = {0};
    en_health_probe_mock_t health_mock = {0};

    en_controller_t *controller = en_controller_create(
        paths,
        path_count,
        en_strongswan_mock_adapter(),
        en_vpp_mock_adapter(&vpp_mock),
        en_health_probe_mock_adapter(&health_mock)
    );
    if (controller == NULL) {
        fprintf(stderr, "failed to create controller\n");
        return 1;
    }

    en_intent_t intent = make_demo_intent();
    en_reconcile_result_t result = {0};
    en_error_code_t err = en_controller_submit_intent(controller, &intent, &result);
    if (err != EN_ERR_NONE) {
        fprintf(stderr, "reconcile failed: %s\n", en_error_code_name(err));
        en_controller_destroy(controller);
        return 1;
    }

    printf("selected_path: %s\n", result.selected_path);
    printf("reason: %s\n", result.explanation.reason);
    printf("transition_state: %s\n", en_transition_state_name(result.transition_state));
    printf("audit:\n");
    const en_audit_event_t *events = NULL;
    size_t event_count = en_controller_audit_events(controller, &events);
    for (size_t i = 0; i < event_count; i++) {
        printf("- %s: %s (%s)\n", events[i].event_type, events[i].message, events[i].ref_id);
    }

    en_controller_destroy(controller);
    return 0;
}
