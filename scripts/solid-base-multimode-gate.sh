#!/usr/bin/env bash
# solid-base-multimode-gate.sh — run solid-base-gate under multiple linux_stream_mode values.
#
# Modes (default set; override with MODES=... space-separated):
#   gamescope_stream  — polaris-hdr-use-portal + mode key (lea default)
#   headless_stream   — polaris-hdr-use-labwc + mode key (labwc private stream)
#   desktop_display   — mirror/portal desktop-oriented conf
#   headless_dongle   — only if DONGLE_OUTPUTS set or conf already has streaming+primary
#
# Env (inherits solid-base-gate.sh):
#   POLARIS_PASSWORD, POLARIS_USER, POLARIS_URL, POLARIS_APP_UUID, ...
#   MODES             default "gamescope_stream headless_stream desktop_display"
#   RESTORE_MODE      mode to leave conf in after run (default gamescope_stream)
#   SKIP_DONGLE       1 to never try headless_dongle (default 1)
#   SETTLE_S          wait after polaris restart before gate (default 8)
#
# Exit 0 only if every attempted mode reports gate ok (or skip with reason).
# Prints one line per mode + final summary.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GATE="${SCRIPT_DIR}/solid-base-gate.sh"
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/polaris/polaris.conf"
RT="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
MODES="${MODES:-gamescope_stream headless_stream desktop_display}"
RESTORE_MODE="${RESTORE_MODE:-gamescope_stream}"
SKIP_DONGLE="${SKIP_DONGLE:-1}"
SETTLE_S="${SETTLE_S:-8}"
BACKUP=""

log() { printf '[multimode-gate] %s\n' "$*" >&2; }

if [[ ! -x "$GATE" ]]; then
  log "missing executable $GATE"
  exit 2
fi
if [[ -z "${POLARIS_PASSWORD:-}" ]]; then
  log "set POLARIS_PASSWORD"
  exit 2
fi

cleanup() {
  if [[ -n "$BACKUP" && -f "$BACKUP" ]]; then
    log "restoring conf from $BACKUP → mode $RESTORE_MODE"
    apply_mode "$RESTORE_MODE" || true
  fi
}
trap cleanup EXIT

set_conf_key() {
  local key="$1" val="$2"
  if grep -qE "^[[:space:]]*${key}[[:space:]]*=" "$CONF" 2>/dev/null; then
    sed -i -E "s|^[[:space:]]*${key}[[:space:]]*=.*|${key} = ${val}|" "$CONF"
  else
    printf '\n%s = %s\n' "$key" "$val" >>"$CONF"
  fi
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
  log "polaris failed to become active"
  return 1
}

# Host helpers (polaris-hdr-use-portal / use-labwc) may rewrite polaris.conf from
# minimal templates and drop lea keys. Re-assert agent-smoke essentials after apply.
preserve_agent_smoke_keys() {
  # Browser Stream is the approved agent smoke path; default conf has it off.
  set_conf_key browser_streaming enabled
  # Keep encoder honesty for encode gate when helpers wipe it.
  if ! grep -qE '^[[:space:]]*encoder[[:space:]]*=' "$CONF" 2>/dev/null; then
    set_conf_key encoder nvenc
  fi
}

apply_mode() {
  local mode="$1"
  log "apply mode=$mode"
  case "$mode" in
    gamescope_stream)
      if command -v polaris-hdr-use-portal >/dev/null 2>&1; then
        polaris-hdr-use-portal
      else
        set_conf_key capture portal
        set_conf_key headless_mode disabled
        set_conf_key linux_use_cage_compositor disabled
        restart_polaris
      fi
      set_conf_key linux_stream_mode gamescope_stream
      preserve_agent_smoke_keys
      # use-portal already restarted; re-set mode and restart once more
      restart_polaris
      ;;
    headless_stream)
      if command -v polaris-hdr-use-labwc >/dev/null 2>&1; then
        polaris-hdr-use-labwc
      else
        set_conf_key headless_mode enabled
        set_conf_key linux_use_cage_compositor enabled
        set_conf_key capture ""
        restart_polaris
      fi
      set_conf_key linux_stream_mode headless_stream
      preserve_agent_smoke_keys
      restart_polaris
      ;;
    desktop_display)
      # Mirror-style: no private cage, portal on session (KDE) or ambient
      rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/polaris.service.d/hdr-portal.conf" 2>/dev/null || true
      systemctl --user daemon-reload 2>/dev/null || true
      systemctl --user stop polaris-portal.service polaris-portal-gamescope.service polaris-portal-dbus.service 2>/dev/null || true
      set_conf_key headless_mode disabled
      set_conf_key linux_use_cage_compositor disabled
      set_conf_key capture portal
      set_conf_key linux_stream_mode desktop_display
      set_conf_key encoder nvenc
      preserve_agent_smoke_keys
      restart_polaris
      ;;
    headless_dongle)
      if [[ "$SKIP_DONGLE" == "1" ]]; then
        log "skip headless_dongle (SKIP_DONGLE=1)"
        return 2
      fi
      # Requires configured outputs — set mode and let path fail closed if unavailable
      set_conf_key linux_stream_mode headless_dongle
      set_conf_key headless_mode disabled
      set_conf_key linux_use_cage_compositor disabled
      preserve_agent_smoke_keys
      restart_polaris
      ;;
    *)
      log "unknown mode $mode"
      return 1
      ;;
  esac
  # Confirm conf
  local got
  got=$(grep -E '^[[:space:]]*linux_stream_mode[[:space:]]*=' "$CONF" 2>/dev/null | tail -1 | sed -E 's/.*=[[:space:]]*//')
  log "conf linux_stream_mode=$got"
  if grep -qE '^[[:space:]]*browser_streaming[[:space:]]*=[[:space:]]*enabled' "$CONF" 2>/dev/null; then
    log "conf browser_streaming=enabled"
  else
    log "WARN conf missing browser_streaming=enabled after apply"
  fi
}

run_gate_for_mode() {
  local mode="$1"
  local out
  out=$(mktemp)
  # Labwc: grim preview possible; gamescope: gamescopectl; desktop: may skip dual_socket strictness
  local extra=()
  if [[ "$mode" == "desktop_display" ]]; then
    # dual gamescope sockets irrelevant
    :
  fi
  set +e
  POLARIS_COOKIE_JAR="/tmp/polaris-mm-${mode}.cookies" \
    "$GATE" >"$out" 2> >(while read -r line; do log "[$mode] $line"; done)
  local rc=$?
  set -e
  local line
  line=$(grep -E '^solid-base-gate:' "$out" | tail -1 || true)
  rm -f "$out"
  if [[ -z "$line" ]]; then
    line="solid-base-gate: (no line) exit=$rc"
  fi
  printf 'multimode: mode=%s rc=%s %s\n' "$mode" "$rc" "$line"
  return "$rc"
}

# backup
BACKUP="${CONF}.bak-multimode-$(date +%s)"
if [[ -f "$CONF" ]]; then
  cp -a "$CONF" "$BACKUP"
  log "backup $BACKUP"
fi

fail=0
declare -a results=()

for mode in $MODES; do
  if [[ "$mode" == "headless_dongle" && "$SKIP_DONGLE" == "1" ]]; then
    results+=("mode=$mode result=skip reason=SKIP_DONGLE")
    printf 'multimode: mode=%s rc=skip\n' "$mode"
    continue
  fi
  if ! apply_mode "$mode"; then
    if [[ $? -eq 2 ]]; then
      results+=("mode=$mode result=skip")
      continue
    fi
    results+=("mode=$mode result=fail apply")
    fail=1
    continue
  fi
  if run_gate_for_mode "$mode"; then
    results+=("mode=$mode result=ok")
  else
    results+=("mode=$mode result=fail gate")
    fail=1
  fi
done

log "=== summary ==="
for r in "${results[@]}"; do
  log "  $r"
done

# trap restores RESTORE_MODE
printf 'multimode-gate: fail=%s modes=%s restore=%s\n' "$fail" "$MODES" "$RESTORE_MODE"
exit "$fail"
