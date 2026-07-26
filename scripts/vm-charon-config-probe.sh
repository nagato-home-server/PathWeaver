#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="${OUT_DIR:-$ROOT_DIR/out/charon-probe}"
RUN_BASE="${RUN_BASE:-/run/eventnet-charon-probe}"
CHARON="${CHARON:-/usr/lib/ipsec/charon}"
NS="${NS:-site-a}"

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

mkdir -p "$OUT_DIR" "$RUN_BASE"

try_default() {
  name="default_no_env"
  log="$RUN_BASE/$name.log"
  default_sock="/var/run/charon.vici"

  rm -f "$log" "$default_sock"
  printf '\n== trying %s ==\n' "$name"
  printf 'config: system default strongswan.conf\n'

  ip netns exec "$NS" "$CHARON" --debug-dmn 2 --debug-lib 2 --debug-cfg 2 --debug-net 2 --debug-knl 2 2>"$log" &
  pid="$!"

  for _ in 1 2 3 4 5 6; do
    if [ -S "$default_sock" ]; then
      printf 'OK: socket created: %s\n' "$default_sock"
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      rm -f "$default_sock"
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      break
    fi
    sleep 0.5
  done

  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  printf 'FAILED log:\n'
  cat "$log" || true
  rm -f "$default_sock"
  return 1
}

try_config() {
  name="$1"
  body="$2"
  conf="$RUN_BASE/$name.conf"
  saved_conf="$OUT_DIR/$name.conf"
  log="$RUN_BASE/$name.log"
  sock="$RUN_BASE/$name.vici"

  rm -f "$log" "$sock"
  printf '%s\n' "$body" > "$conf"
  cp "$conf" "$saved_conf"

  printf '\n== trying %s ==\n' "$name"
  printf 'config: %s\n' "$conf"
  cat "$conf"

  STRONGSWAN_CONF="$conf" ip netns exec "$NS" "$CHARON" --debug-dmn 2 --debug-lib 2 --debug-cfg 2 --debug-net 2 --debug-knl 2 2>"$log" &
  pid="$!"

  for _ in 1 2 3 4 5 6; do
    if [ -S "$sock" ]; then
      printf 'OK: socket created: %s\n' "$sock"
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      break
    fi
    sleep 0.5
  done

  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  printf 'FAILED log:\n'
  cat "$log" || true
  return 1
}

try_default || true

try_config minimal "charon {
}" || true

try_config vici_relative "charon {
  plugins {
    vici {
      socket = unix://$RUN_BASE/vici_relative.vici
    }
  }
}" || true

try_config vici_global "charon {
}
charon.plugins.vici.socket = unix://$RUN_BASE/vici_global.vici" || true

try_config with_include "charon {
  load_modular = yes
  plugins {
    include strongswan.d/charon/*.conf
    vici {
      socket = unix://$RUN_BASE/with_include.vici
    }
  }
}
include strongswan.d/*.conf" || true

try_config with_pid "charon {
  pid_file = $RUN_BASE/with_pid.pid
  load_modular = yes
  plugins {
    include strongswan.d/charon/*.conf
    vici {
      socket = unix://$RUN_BASE/with_pid.vici
    }
  }
}
include strongswan.d/*.conf" || true

try_config abs_include "charon {
  load_modular = yes
  plugins {
    include /etc/strongswan.d/charon/*.conf
    vici {
      socket = unix://$RUN_BASE/abs_include.vici
    }
  }
}
include /etc/strongswan.d/*.conf" || true

try_config abs_with_pid "charon {
  pid_file = $RUN_BASE/abs_with_pid.pid
  load_modular = yes
  plugins {
    include /etc/strongswan.d/charon/*.conf
    vici {
      socket = unix://$RUN_BASE/abs_with_pid.vici
    }
  }
}
include /etc/strongswan.d/*.conf" || true

printf '\nProbe completed. Paste this output back if all variants fail.\n'
