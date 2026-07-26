#include "eventnet/render_commands.h"

#include <stdio.h>

en_error_code_t en_render_swanctl_initiate(
    const en_tunnel_t *tunnel,
    char *buf,
    size_t buf_len
)
{
    if (tunnel == NULL || buf == NULL || buf_len == 0 || tunnel->tunnel_id[0] == '\0') {
        return EN_ERR_INVALID_ARGUMENT;
    }
    snprintf(buf, buf_len, "swanctl --initiate --child %s", tunnel->tunnel_id);
    return EN_ERR_NONE;
}

en_error_code_t en_render_swanctl_terminate(
    const en_tunnel_t *tunnel,
    char *buf,
    size_t buf_len
)
{
    if (tunnel == NULL || buf == NULL || buf_len == 0 || tunnel->tunnel_id[0] == '\0') {
        return EN_ERR_INVALID_ARGUMENT;
    }
    snprintf(buf, buf_len, "swanctl --terminate --child %s", tunnel->tunnel_id);
    return EN_ERR_NONE;
}

en_error_code_t en_render_swanctl_conf(
    const en_tunnel_t *tunnel,
    char *buf,
    size_t buf_len
)
{
    if (tunnel == NULL || buf == NULL || buf_len == 0 || tunnel->tunnel_id[0] == '\0') {
        return EN_ERR_INVALID_ARGUMENT;
    }
    snprintf(
        buf,
        buf_len,
        "connections.%s.local_addrs=%s connections.%s.remote_addrs=%s "
        "connections.%s.children.%s.local_ts=%s connections.%s.children.%s.remote_ts=%s",
        tunnel->tunnel_id,
        tunnel->local_endpoint,
        tunnel->tunnel_id,
        tunnel->remote_endpoint,
        tunnel->tunnel_id,
        tunnel->tunnel_id,
        tunnel->local_traffic_selector,
        tunnel->tunnel_id,
        tunnel->tunnel_id,
        tunnel->remote_traffic_selector
    );
    return EN_ERR_NONE;
}

en_error_code_t en_render_vpp_route_replace(
    const en_path_t *path,
    const en_tunnel_t *egress_tunnel,
    char *buf,
    size_t buf_len
)
{
    if (path == NULL || egress_tunnel == NULL || buf == NULL || buf_len == 0) {
        return EN_ERR_INVALID_ARGUMENT;
    }
    const char *next_hop = path->route_next_hop[0] == '\0' ? egress_tunnel->remote_endpoint : path->route_next_hop;
    if (path->route_destination_prefix[0] == '\0' || next_hop[0] == '\0') {
        return EN_ERR_INVALID_ARGUMENT;
    }
    snprintf(
        buf,
        buf_len,
        "vppctl ip route add %s via %s",
        path->route_destination_prefix,
        next_hop
    );
    return EN_ERR_NONE;
}

en_error_code_t en_render_vpp_route_delete(
    const en_path_t *path,
    char *buf,
    size_t buf_len
)
{
    if (path == NULL || buf == NULL || buf_len == 0 || path->route_destination_prefix[0] == '\0') {
        return EN_ERR_INVALID_ARGUMENT;
    }
    snprintf(buf, buf_len, "vppctl ip route del %s", path->route_destination_prefix);
    return EN_ERR_NONE;
}
