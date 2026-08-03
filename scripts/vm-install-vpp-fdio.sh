#!/usr/bin/env sh
set -eu

DRY_RUN="${DRY_RUN:-1}"
CHANNEL="${CHANNEL:-release}"
PACKAGECLOUD_SCRIPT_URL="${PACKAGECLOUD_SCRIPT_URL:-https://packagecloud.io/install/repositories/fdio/$CHANNEL/script.deb.sh}"

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

if [ ! -r /etc/os-release ]; then
  printf 'missing: /etc/os-release\n' >&2
  exit 1
fi

. /etc/os-release

case "${ID:-}" in
  ubuntu|debian)
    ;;
  *)
    printf 'unsupported OS for this helper: ID=%s\n' "${ID:-unknown}" >&2
    printf 'Use the official FD.io/packagecloud instructions for your distribution.\n' >&2
    exit 1
    ;;
esac

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf 'missing: %s\n' "$1" >&2
    missing=1
  fi
}

missing=0
need_cmd curl
need_cmd apt-get
need_cmd getent
need_cmd bash
if [ "$missing" -ne 0 ]; then
  printf '\nInstall prerequisites first:\n' >&2
  printf '  sudo apt-get update\n' >&2
  printf '  sudo apt-get install -y curl ca-certificates gnupg\n' >&2
  exit 1
fi

cat <<MSG
FD.io VPP packagecloud install helper

OS:
  ID=${ID:-unknown}
  VERSION_ID=${VERSION_ID:-unknown}
  VERSION_CODENAME=${VERSION_CODENAME:-unknown}

Channel:
  $CHANNEL

Repository setup script:
  $PACKAGECLOUD_SCRIPT_URL

Mode:
  DRY_RUN=$DRY_RUN
MSG

if [ "$DRY_RUN" != "0" ]; then
  cat <<'MSG'

Dry-run only. To actually add the repository and install VPP:

  sudo DRY_RUN=0 sh scripts/vm-install-vpp-fdio.sh

If the release channel has no package for this OS, try another FD.io channel:

  sudo CHANNEL=master DRY_RUN=0 sh scripts/vm-install-vpp-fdio.sh

MSG
  exit 0
fi

tmp_script=$(mktemp)
trap 'rm -f "$tmp_script"' EXIT

if ! getent hosts packagecloud.io >/dev/null 2>&1; then
  cat <<'MSG' >&2
DNS lookup failed for packagecloud.io.

Check the VM DNS/outbound network, for example:
  getent hosts packagecloud.io
  curl -I https://packagecloud.io/
MSG
  exit 1
fi

if ! curl -fsSL "$PACKAGECLOUD_SCRIPT_URL" -o "$tmp_script"; then
  cat <<MSG >&2
Failed to download FD.io packagecloud setup script:
  $PACKAGECLOUD_SCRIPT_URL

Check DNS, proxy, TLS interception, or outbound firewall settings before retrying.
MSG
  exit 1
fi
bash "$tmp_script"
apt-get update
apt-get install -y vpp vpp-plugin-core

printf '\nVPP install attempt completed. Run:\n'
printf '  sh scripts/vm-vpp-preflight.sh\n'
