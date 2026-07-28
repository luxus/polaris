#!/usr/bin/env bash
# lea-mode-switch.sh — switch linux_stream_mode while keeping dongle + smoke keys.
#
# Usage:
#   ./scripts/lea-mode-switch.sh gamescope_stream
#   ./scripts/lea-mode-switch.sh headless_stream    # labwc
#   ./scripts/lea-mode-switch.sh headless_dongle
#   ./scripts/lea-mode-switch.sh desktop_display
#   ./scripts/lea-mode-switch.sh status
#
# Env:
#   STREAMING_OUTPUT  default HDMI-A-2 (4K60 HDR dongle path)
#   PRIMARY_OUTPUT    default DP-2
#   SETTLE_S          polaris settle after restart (default 10)
#
set -euo pipefail
export PATH=/run/wrappers/bin:${PATH:-}

CONF="${XDG_CONFIG_HOME:-$HOME/.config}/polaris/polaris.conf"
STREAMING_OUTPUT="${STREAMING_OUTPUT:-HDMI-A-2}"
PRIMARY_OUTPUT="${PRIMARY_OUTPUT:-DP-2}"
SETTLE_S="${SETTLE_S:-10}"
MODE="${1:-}"

log() { printf '[lea-mode] %s\n' "$*" >&2; }

set_conf_key() {
  local key="$1" val="$2"
  if grep -qE "^[[:space:]]*${key}[[:space:]]*=" "$CONF" 2>/dev/null; then
    sed -i -E "s|^[[:space:]]*${key}[[:space:]]*=.*|${key} = ${val}|" "$CONF"
  else
    printf '\n%s = %s\n' "$key" "$val" >>"$CONF"
  fi
}

del_conf_key() {
  local key="$1"
  sed -i -E "/^[[:space:]]*${key}[[:space:]]*=/d" "$CONF" 2>/dev/null || true
}

restart_polaris() {
  systemctl --user daemon-reload 2>/dev/null || true
  systemctl --user restart polaris.service
  local n=0
  while [[ $n -lt 40 ]]; do
    if systemctl --user is-active --quiet polaris.service 2>/dev/null; then
      sleep "$SETTLE_S"
      return 0
    fi
    sleep 0.5
    n=$((n + 1))
  done
  log "FAIL polaris not active"
  return 1
}

# Always keep dongle wiring + agent smoke keys.
preserve_common() {
  set_conf_key browser_streaming enabled
  set_conf_key encoder nvenc
  set_conf_key linux_streaming_output "$STREAMING_OUTPUT"
  set_conf_key linux_primary_output "$PRIMARY_OUTPUT"
  set_conf_key linux_auto_manage_displays enabled
  set_conf_key headless_swap_mode privacy
  # 4K-friendly encode; Moonlight client can still request lower
  set_conf_key hevc_mode 3
  set_conf_key av1_mode 3
  set_conf_key fec_percentage 20
}

# Best-effort: native 4K60 on streaming connector (kscreen).
ensure_dongle_4k60() {
  export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
  export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
  export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=${XDG_RUNTIME_DIR}/bus}"
  if ! command -v kscreen-doctor >/dev/null 2>&1; then
    log "kscreen-doctor missing — skip 4K mode set"
    return 0
  fi
  log "set ${STREAMING_OUTPUT} → 4K60 scale=1 HDR (best effort)"
  # Plasma 6 accepts mode id (preferred 4K60 is usually id 31 on lea HDMI-A-2)
  timeout 12 kscreen-doctor "output.${STREAMING_OUTPUT}.mode.31" 2>/dev/null \
    || timeout 12 kscreen-doctor "output.${STREAMING_OUTPUT}.mode.3840x2160@60" 2>/dev/null \
    || true
  timeout 8 kscreen-doctor "output.${STREAMING_OUTPUT}.scale.1" 2>/dev/null || true
}

status() {
  log "polaris=$(systemctl --user is-active polaris.service 2>/dev/null || echo n/a)"
  if [[ -f "$CONF" ]]; then
    grep -E '^(linux_stream_mode|linux_streaming_output|linux_primary_output|linux_auto_manage|headless_|browser_|encoder|hevc_|capture)[[:space:]]*=' "$CONF" || true
  fi
  if command -v kscreen-doctor >/dev/null 2>&1; then
    export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
    export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
    export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=${XDG_RUNTIME_DIR}/bus}"
    timeout 6 kscreen-doctor -o 2>/dev/null | grep -E 'Output:|enabled|connected|priority|Geometry|HDR:|Modes:' | head -40 || true
  fi
}

apply_gamescope() {
  if command -v polaris-hdr-use-portal >/dev/null 2>&1; then
    polaris-hdr-use-portal
  fi
  preserve_common
  set_conf_key linux_stream_mode gamescope_stream
  set_conf_key headless_mode disabled
  set_conf_key linux_use_cage_compositor disabled
  set_conf_key capture portal
  restart_polaris
}

apply_labwc() {
  if command -v polaris-hdr-use-labwc >/dev/null 2>&1; then
    polaris-hdr-use-labwc
  fi
  preserve_common
  set_conf_key linux_stream_mode headless_stream
  set_conf_key linux_private_runtime labwc
  set_conf_key headless_mode enabled
  set_conf_key linux_use_cage_compositor enabled
  del_conf_key capture
  # labwc cold NVENC reprob is more reliable in SDR modes on lea
  set_conf_key hevc_mode 1
  set_conf_key av1_mode 1
  rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/polaris/encoder_cache.txt" 2>/dev/null || true
  restart_polaris
}

apply_dongle() {
  # Drop private portal pin; host KDE + kscreen swap
  rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/polaris.service.d/hdr-portal.conf" 2>/dev/null || true
  systemctl --user daemon-reload 2>/dev/null || true
  systemctl --user stop polaris-portal.service polaris-portal-gamescope.service polaris-portal-dbus.service 2>/dev/null || true
  ensure_dongle_4k60
  preserve_common
  set_conf_key linux_stream_mode headless_dongle
  set_conf_key headless_mode disabled
  set_conf_key linux_use_cage_compositor disabled
  del_conf_key capture
  set_conf_key hevc_mode 3
  set_conf_key av1_mode 3
  restart_polaris
}

apply_desktop() {
  rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/polaris.service.d/hdr-portal.conf" 2>/dev/null || true
  systemctl --user daemon-reload 2>/dev/null || true
  systemctl --user stop polaris-portal.service polaris-portal-gamescope.service polaris-portal-dbus.service 2>/dev/null || true
  preserve_common
  set_conf_key linux_stream_mode desktop_display
  set_conf_key headless_mode disabled
  set_conf_key linux_use_cage_compositor disabled
  set_conf_key capture portal
  restart_polaris
}

case "$MODE" in
  status|"")
    status
    [[ -n "$MODE" ]] || { log "usage: $0 gamescope_stream|headless_stream|headless_dongle|desktop_display|status"; exit 2; }
    ;;
  gamescope_stream|gamescope)
    apply_gamescope
    status
    ;;
  headless_stream|labwc)
    apply_labwc
    status
    ;;
  headless_dongle|dongle)
    apply_dongle
    status
    ;;
  desktop_display|desktop)
    apply_desktop
    status
    ;;
  *)
    log "unknown mode: $MODE"
    exit 2
    ;;
esac
