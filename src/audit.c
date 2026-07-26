#include "internal.h"

#include <stdio.h>

void en_audit_append(en_controller_t *controller, const char *event_type, const char *message, const char *ref_id)
{
    if (controller == NULL || controller->audit_count >= EN_MAX_EVENTS) {
        return;
    }
    en_audit_event_t *event = &controller->audit_events[controller->audit_count++];
    en_copy_id(event->event_type, sizeof(event->event_type), event_type);
    snprintf(event->message, sizeof(event->message), "%s", message == NULL ? "" : message);
    en_copy_id(event->ref_id, sizeof(event->ref_id), ref_id);
    event->timestamp_ms = en_now_ms();
}

void en_error_append(en_controller_t *controller, en_error_code_t code, const char *message)
{
    if (controller == NULL || controller->state.error_count >= EN_MAX_ERRORS) {
        return;
    }
    en_error_t *error = &controller->state.errors[controller->state.error_count++];
    error->code = code;
    snprintf(error->message, sizeof(error->message), "%s", message == NULL ? "" : message);
}

size_t en_controller_audit_events(const en_controller_t *controller, const en_audit_event_t **events)
{
    if (events != NULL) {
        *events = controller == NULL ? NULL : controller->audit_events;
    }
    return controller == NULL ? 0 : controller->audit_count;
}

size_t en_controller_errors(const en_controller_t *controller, const en_error_t **errors)
{
    if (errors != NULL) {
        *errors = controller == NULL ? NULL : controller->state.errors;
    }
    return controller == NULL ? 0 : controller->state.error_count;
}
