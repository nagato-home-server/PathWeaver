#ifndef EVENTNET_RENDER_COMMANDS_H
#define EVENTNET_RENDER_COMMANDS_H

#include "eventnet/types.h"

en_error_code_t en_render_swanctl_initiate(
    const en_tunnel_t *tunnel,
    char *buf,
    size_t buf_len
);

en_error_code_t en_render_swanctl_terminate(
    const en_tunnel_t *tunnel,
    char *buf,
    size_t buf_len
);

en_error_code_t en_render_swanctl_conf(
    const en_tunnel_t *tunnel,
    char *buf,
    size_t buf_len
);

en_error_code_t en_render_vpp_route_replace(
    const en_path_t *path,
    const en_tunnel_t *egress_tunnel,
    char *buf,
    size_t buf_len
);

en_error_code_t en_render_vpp_route_delete(
    const en_path_t *path,
    char *buf,
    size_t buf_len
);

#endif
