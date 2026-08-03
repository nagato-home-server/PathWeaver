#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="${OUT_DIR:-$ROOT_DIR/out/netns-ipsec-direct}"
PSK="${PSK:-}"

if [ -z "$PSK" ]; then
  PSK=$(od -An -N24 -tx1 /dev/urandom | tr -d ' \n')
elif [ "$PSK" = "change-me" ]; then
  printf 'Refusing insecure PSK value: change-me\n' >&2
  exit 1
fi

umask 077
mkdir -p "$OUT_DIR/site-a" "$OUT_DIR/site-b"

cat > "$OUT_DIR/site-a/swanctl.conf" <<EOF
connections {
  tun-a-b {
    version = 2
    local_addrs = 203.0.113.10
    remote_addrs = 203.0.113.9
    local {
      auth = psk
      id = site-a
    }
    remote {
      auth = psk
      id = site-b
    }
    children {
      tun-a-b {
        local_ts = 10.10.1.0/24
        remote_ts = 10.10.2.0/24
        mode = tunnel
        esp_proposals = aes128-sha256
        start_action = trap
      }
    }
    proposals = aes128-sha256-modp2048
  }
}

secrets {
  ike-tun-a-b {
    id-1 = site-a
    id-2 = site-b
    secret = $PSK
  }
}
EOF
chmod 600 "$OUT_DIR/site-a/swanctl.conf"

cat > "$OUT_DIR/site-b/swanctl.conf" <<EOF
connections {
  tun-a-b {
    version = 2
    local_addrs = 203.0.113.9
    remote_addrs = 203.0.113.10
    local {
      auth = psk
      id = site-b
    }
    remote {
      auth = psk
      id = site-a
    }
    children {
      tun-a-b {
        local_ts = 10.10.2.0/24
        remote_ts = 10.10.1.0/24
        mode = tunnel
        esp_proposals = aes128-sha256
        start_action = trap
      }
    }
    proposals = aes128-sha256-modp2048
  }
}

secrets {
  ike-tun-a-b {
    id-1 = site-b
    id-2 = site-a
    secret = $PSK
  }
}
EOF
chmod 600 "$OUT_DIR/site-b/swanctl.conf"

cat > "$OUT_DIR/README.txt" <<EOF
Generated direct IPsec configs:
  $OUT_DIR/site-a/swanctl.conf
  $OUT_DIR/site-b/swanctl.conf

Endpoints:
  site-a 203.0.113.10, protected LAN 10.10.1.0/24
  site-b 203.0.113.9,  protected LAN 10.10.2.0/24
EOF

printf 'Generated direct IPsec configs under %s\n' "$OUT_DIR"
