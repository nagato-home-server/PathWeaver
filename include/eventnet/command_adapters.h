#ifndef EVENTNET_COMMAND_ADAPTERS_H
#define EVENTNET_COMMAND_ADAPTERS_H

#include "eventnet/controller.h"

typedef struct {
    bool dry_run;
    char ensure_tunnel_command[256];
    char remove_tunnel_command[256];
} en_strongswan_command_ctx_t;

typedef struct {
    bool dry_run;
    char install_path_command[256];
    const en_tunnel_t *tunnels;
    size_t tunnel_count;
    char active_paths[EN_MAX_CANDIDATES][EN_MAX_ID_LEN];
    char traffic_keys[EN_MAX_CANDIDATES][EN_MAX_ID_LEN * 2];
    size_t active_count;
} en_vpp_command_ctx_t;

typedef struct {
    bool dry_run;
    char validate_path_command[256];
} en_health_command_ctx_t;

en_strongswan_adapter_t en_strongswan_command_adapter(en_strongswan_command_ctx_t *ctx);
en_vpp_adapter_t en_vpp_command_adapter(en_vpp_command_ctx_t *ctx);
en_health_probe_t en_health_command_probe(en_health_command_ctx_t *ctx);

#endif
