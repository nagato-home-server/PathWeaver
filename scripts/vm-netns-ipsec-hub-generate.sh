#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="${OUT_DIR:-$ROOT_DIR/out/netns-ipsec-hub}"
PSK="${PSK:-change-me}"

mkdir -p "$OUT_DIR/site-a" "$OUT_DIR/hub-1" "$OUT_DIR/site-b"

cat > "$OUT_DIR/site-a/swanctl.conf" <<EOF
connections {
  tun-a-hub {
    version = 2
    local_addrs = 203.0.113.14
    remote_addrs = 203.0.113.13
    local {
      auth = psk
      id = site-a
    }
    remote {
      auth = psk
      id = hub-1
    }
    children {
      tun-a-hub {
        local_ts = 0.0.0.0/0
        remote_ts = 0.0.0.0/0
        mode = tunnel
        if_id_in = 101
        if_id_out = 101
        esp_proposals = aes128-sha256
        start_action = trap
      }
    }
    proposals = aes128-sha256-modp2048
  }
}

secrets {
  ike-tun-a-hub {
    id-1 = site-a
    id-2 = hub-1
    secret = $PSK
  }
}
EOF

cat > "$OUT_DIR/hub-1/swanctl.conf" <<EOF
connections {
  tun-a-hub {
    version = 2
    local_addrs = 203.0.113.13
    remote_addrs = 203.0.113.14
    local {
      auth = psk
      id = hub-1
    }
    remote {
      auth = psk
      id = site-a
    }
    children {
      tun-a-hub {
        local_ts = 0.0.0.0/0
        remote_ts = 0.0.0.0/0
        mode = tunnel
        if_id_in = 101
        if_id_out = 101
        esp_proposals = aes128-sha256
        start_action = trap
      }
    }
    proposals = aes128-sha256-modp2048
  }

  tun-hub-b {
    version = 2
    local_addrs = 203.0.113.17
    remote_addrs = 203.0.113.18
    local {
      auth = psk
      id = hub-1
    }
    remote {
      auth = psk
      id = site-b
    }
    children {
      tun-hub-b {
        local_ts = 0.0.0.0/0
        remote_ts = 0.0.0.0/0
        mode = tunnel
        if_id_in = 102
        if_id_out = 102
        esp_proposals = aes128-sha256
        start_action = trap
      }
    }
    proposals = aes128-sha256-modp2048
  }
}

secrets {
  ike-tun-a-hub {
    id-1 = hub-1
    id-2 = site-a
    secret = $PSK
  }
  ike-tun-hub-b {
    id-1 = hub-1
    id-2 = site-b
    secret = $PSK
  }
}
EOF

cat > "$OUT_DIR/site-b/swanctl.conf" <<EOF
connections {
  tun-hub-b {
    version = 2
    local_addrs = 203.0.113.18
    remote_addrs = 203.0.113.17
    local {
      auth = psk
      id = site-b
    }
    remote {
      auth = psk
      id = hub-1
    }
    children {
      tun-hub-b {
        local_ts = 0.0.0.0/0
        remote_ts = 0.0.0.0/0
        mode = tunnel
        if_id_in = 102
        if_id_out = 102
        esp_proposals = aes128-sha256
        start_action = trap
      }
    }
    proposals = aes128-sha256-modp2048
  }
}

secrets {
  ike-tun-hub-b {
    id-1 = site-b
    id-2 = hub-1
    secret = $PSK
  }
}
EOF

cat > "$OUT_DIR/README.txt" <<EOF
Generated hub IPsec configs:
  $OUT_DIR/site-a/swanctl.conf
  $OUT_DIR/hub-1/swanctl.conf
  $OUT_DIR/site-b/swanctl.conf

Route-based IPsec / XFRM interfaces:
  site-a xfrm-a-hub if_id 101
  hub-1  xfrm-a-hub if_id 101
  hub-1  xfrm-hub-b if_id 102
  site-b xfrm-hub-b if_id 102
EOF

printf 'Generated hub IPsec configs under %s\n' "$OUT_DIR"
