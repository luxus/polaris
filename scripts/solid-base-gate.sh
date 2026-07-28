#!/usr/bin/env bash
# solid-base-gate.sh — agent-runnable solid-base gate (SB-6 + SB-7 health).
#
# Phone-free: units, private ScreenCast, preview, browser-stream lifecycle,
# mid-stream frame (gamescopectl), webui disconnect, dual-socket, nvenc log.
#
# Env:
#   POLARIS_URL           default https://127.0.0.1:47990
#   POLARIS_USER          default luxus
#   POLARIS_PASSWORD      required (unless PREVIEW_ONLY=1 and only units?)
#   POLARIS_APP_UUID      optional; else Steam Big Picture / first app
#   SKIP_BROWSER_STREAM   1 = units+screencast+preview only
#   PREVIEW_ONLY          1 = units + preview (+ mid-stream if already streaming)
#   GATE_SAVE_DIR         default <repo>/images  (empty to skip saving PNGs)
#   GATE_STREAM_SETTLE_S  seconds after browser start before mid-preview (default 5)
#
# Exit 0 only if all enabled checks are ok.
# Last stdout line: solid-base-gate: key=ok|fail|skip ...
#
set -euo pipefail

URL="${POLARIS_URL:-https://127.0.0.1:47990}"
USER_NAME="${POLARIS_USER:-luxus}"
PASS="${POLARIS_PASSWORD:-}"
JAR="${POLARIS_COOKIE_JAR:-/tmp/polaris-solid-base-gate.cookies}"
SKIP_BS="${SKIP_BROWSER_STREAM:-0}"
PREVIEW_ONLY="${PREVIEW_ONLY:-0}"
SETTLE="${GATE_STREAM_SETTLE_S:-5}"
RT="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SAVE_DIR="${GATE_SAVE_DIR:-$REPO_ROOT/images}"

units_ok=fail
screencast_ok=fail
preview_ok=fail
stream_start_ok=skip
stream_preview_ok=skip
stream_stop_ok=skip
webui_stop_ok=skip
dual_socket_ok=fail
encode_ok=skip
CSRF=""

log() { printf '[solid-base-gate] %s\n' "$*" >&2; }

curl_json() {
  local method="$1" path="$2" data="${3:-}"
  local args=( -sk -c "$JAR" -b "$JAR" -X "$method" "${URL}${path}"
    -H "Content-Type: application/json" )
  if [[ -n "$CSRF" ]]; then
    args+=( -H "X-CSRF-TOKEN: $CSRF" )
  fi
  if [[ -n "$data" ]]; then
    args+=( -d "$data" )
  fi
  curl "${args[@]}"
}

png_nonblack() {
  # $1 path — true if PNG/JPEG larger than 50k and (if magick/convert) mean>0.02
  local f="$1"
  [[ -f "$f" ]] || return 1
  local sz
  sz=$(wc -c <"$f" | tr -d ' ')
  [[ "$sz" -gt 50000 ]] || return 1
  local magic
  magic=$(xxd -p -l 2 "$f" 2>/dev/null || true)
  # reject JSON {
  [[ "$magic" == 7b* ]] && return 1
  if command -v magick >/dev/null 2>&1; then
    local mean
    mean=$(magick "$f" -resize 32x18 -format '%[fx:mean]' info: 2>/dev/null || echo 0)
    python3 -c "import sys; sys.exit(0 if float('${mean:-0}')>0.02 else 1)"
  elif command -v convert >/dev/null 2>&1; then
    local mean
    mean=$(convert "$f" -resize 32x18 -format '%[fx:mean]' info: 2>/dev/null || echo 0)
    python3 -c "import sys; sys.exit(0 if float('${mean:-0}')>0.02 else 1)"
  else
    # size-only fallback (gamescopectl 4K PNG ~5MB)
    [[ "$sz" -gt 200000 ]]
  fi
}

gamescopectl_shot() {
  local out="$1"
  rm -f "$out"
  if [[ ! -S "$RT/gamescope-0" ]]; then
    return 1
  fi
  GAMESCOPE_WAYLAND_DISPLAY=gamescope-0 timeout 8 gamescopectl screenshot "$out" 2>/dev/null || true
  # async write
  local i=0
  while [[ $i -lt 15 ]]; do
    if [[ -f "$out" ]] && [[ "$(wc -c <"$out" | tr -d ' ')" -gt 1000 ]]; then
      return 0
    fi
    sleep 0.2
    i=$((i + 1))
  done
  return 1
}

# --- units (SB-7) ---
if systemctl --user is-active --quiet polaris.service 2>/dev/null; then
  log "polaris.service active"
else
  log "FAIL polaris.service not active"
fi

dbus_ok=0
systemctl --user is-active --quiet polaris-portal-dbus.service 2>/dev/null && dbus_ok=1

# gamescope portal: unit active OR name owned without restart storm
gs_unit_active=0
systemctl --user is-active --quiet polaris-portal-gamescope.service 2>/dev/null && gs_unit_active=1
nrestarts=$(systemctl --user show polaris-portal-gamescope.service -p NRestarts --value 2>/dev/null || echo 999)
# storm if more than 5 restarts since boot of unit (heuristic)
gs_storm=0
if [[ "${nrestarts:-0}" =~ ^[0-9]+$ ]] && [[ "$nrestarts" -gt 5 ]]; then
  gs_storm=1
  log "WARN polaris-portal-gamescope NRestarts=$nrestarts (NameTaken storm?)"
fi

name_owned=0
if [[ -S "$RT/polaris-portal/bus" ]]; then
  if DBUS_SESSION_BUS_ADDRESS="unix:path=$RT/polaris-portal/bus" \
      busctl --user status org.freedesktop.impl.portal.desktop.gamescope >/dev/null 2>&1; then
    name_owned=1
  fi
fi

portal_main_ok=0
if systemctl --user is-active --quiet polaris-portal.service 2>/dev/null; then
  portal_main_ok=1
elif DBUS_SESSION_BUS_ADDRESS="unix:path=$RT/polaris-portal/bus" \
    busctl --user status org.freedesktop.portal.Desktop >/dev/null 2>&1; then
  # activated Desktop without unit — acceptable if no storm
  portal_main_ok=1
  log "polaris-portal unit inactive but Desktop name present on private bus"
fi

if [[ "$dbus_ok" -eq 1 ]] && [[ "$gs_storm" -eq 0 ]] && { [[ "$gs_unit_active" -eq 1 ]] || [[ "$name_owned" -eq 1 ]]; } \
  && [[ "$portal_main_ok" -eq 1 ]] && systemctl --user is-active --quiet polaris.service 2>/dev/null; then
  units_ok=ok
  log "units ok (dbus=$dbus_ok gs_unit=$gs_unit_active name_owned=$name_owned storm=$gs_storm)"
else
  units_ok=fail
  log "units fail dbus=$dbus_ok gs_unit=$gs_unit_active name_owned=$name_owned storm=$gs_storm portal_main=$portal_main_ok"
fi

# --- ScreenCast ---
if [[ -S "$RT/polaris-portal/bus" ]]; then
  if DBUS_SESSION_BUS_ADDRESS="unix:path=$RT/polaris-portal/bus" \
      busctl --user introspect org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop 2>/dev/null \
      | grep -q 'org.freedesktop.portal.ScreenCast'; then
    screencast_ok=ok
    log "private ScreenCast present"
  else
    screencast_ok=fail
    log "FAIL private bus without ScreenCast"
  fi
else
  screencast_ok=fail
  log "FAIL no polaris-portal/bus socket"
fi

if [[ "$PREVIEW_ONLY" == "1" ]]; then
  SKIP_BS=1
fi

if [[ -z "$PASS" && "$PREVIEW_ONLY" != "1" ]]; then
  log "set POLARIS_PASSWORD (or PREVIEW_ONLY=1 for unit-only path with gamescopectl)"
fi

# --- login (if password) ---
if [[ -n "$PASS" ]]; then
  rm -f "$JAR"
  login_body=$(printf '{"username":"%s","password":"%s"}' "$USER_NAME" "$PASS")
  login_code=$(curl -sk -c "$JAR" -b "$JAR" -o /tmp/polaris-gate-login.json -w '%{http_code}' \
    -X POST "${URL}/api/login" \
    -H "Content-Type: application/json" -d "$login_body" || echo 000)
  log "login http=$login_code"
  if [[ "$login_code" != "200" ]]; then
    log "login failed"
    echo "solid-base-gate: units=$units_ok screencast=$screencast_ok preview=fail stream_start=fail stream_preview=skip stream_stop=skip webui_stop=skip dual_socket=$dual_socket_ok encode=skip"
    exit 2
  fi
  CSRF=$(curl -sk -c "$JAR" -b "$JAR" "${URL}/" 2>/dev/null \
    | python3 -c 'import sys,re; m=re.search(r"name=\"csrf-token\" content=\"([^\"]+)\"", sys.stdin.read()); print(m.group(1) if m else "")' \
    || true)
  log "csrf present: $([[ -n "$CSRF" ]] && echo yes || echo no)"
fi

# --- preview (idle or any) ---
prev_api=/tmp/polaris-gate-preview-api.bin
prev_gs="$RT/polaris-gate-gs.png"
rm -f "$prev_api" "$prev_gs"
if [[ -n "$PASS" ]]; then
  code=$(curl -sk -c "$JAR" -b "$JAR" -o "$prev_api" -w '%{http_code}' \
    ${CSRF:+-H "X-CSRF-TOKEN: $CSRF"} \
    "${URL}/api/display/screenshot" 2>/dev/null || echo 000)
  if [[ "$code" == "200" ]] && png_nonblack "$prev_api"; then
    preview_ok=ok
    log "preview API ok"
    if [[ -n "$SAVE_DIR" ]]; then
      mkdir -p "$SAVE_DIR"
      cp -f "$prev_api" "$SAVE_DIR/gate-preview-api.png" 2>/dev/null || true
    fi
  else
    log "preview API fail code=$code size=$(wc -c <"$prev_api" 2>/dev/null || echo 0)"
  fi
fi
if [[ "$preview_ok" != ok ]]; then
  if gamescopectl_shot "$prev_gs" && png_nonblack "$prev_gs"; then
    preview_ok=ok
    log "preview gamescopectl ok"
    if [[ -n "$SAVE_DIR" ]]; then
      mkdir -p "$SAVE_DIR"
      cp -f "$prev_gs" "$SAVE_DIR/gate-preview-gamescope.png" 2>/dev/null || true
    fi
  else
    preview_ok=fail
    log "preview fail (API + gamescopectl)"
  fi
fi

# --- browser stream ---
if [[ "$SKIP_BS" != "1" && -n "$PASS" ]]; then
  status_bs=$(curl_json GET /api/browser-stream/status || true)
  log "browser-stream status: ${status_bs:0:200}"
  app_uuid="${POLARIS_APP_UUID:-}"
  if [[ -z "$app_uuid" ]]; then
    apps=$(curl_json GET /api/apps || true)
    app_uuid=$(printf '%s' "$apps" | python3 -c '
import sys, json
try:
    d = json.load(sys.stdin)
except Exception:
    raise SystemExit
apps = d.get("apps", d) if isinstance(d, dict) else d
if not isinstance(apps, list):
    raise SystemExit
for a in apps:
    n = (a.get("name") or "").lower()
    if "big picture" in n or "desktop" in n:
        print(a.get("uuid") or "")
        raise SystemExit
if apps:
    print(apps[0].get("uuid") or "")
' 2>/dev/null || true)
  fi
  if [[ -z "$app_uuid" ]]; then
    log "no app uuid"
    stream_start_ok=fail
  else
    log "using app_uuid=$app_uuid"
    # journal cursor for encode check
    jcursor=$(journalctl --user -u polaris.service -n 0 --show-cursor 2>/dev/null | sed -n 's/^-- cursor: //p' || true)
    start_body=$(printf '{"app_uuid":"%s","stream_profile":"balanced"}' "$app_uuid")
    start_resp=$(curl_json POST /api/browser-stream/session/start "$start_body" || true)
    log "browser start: ${start_resp:0:240}"
    token=$(printf '%s' "$start_resp" | python3 -c '
import sys, json
try:
    j = json.load(sys.stdin)
except Exception:
    print("")
    raise SystemExit
print(j.get("token") or j.get("session_token") or (j.get("session") or {}).get("token") or "")
' 2>/dev/null || true)
    if [[ -n "$token" ]]; then
      stream_start_ok=ok
    else
      stream_start_ok=fail
    fi

    sleep "$SETTLE"

    # mid-stream preview
    mid="$RT/polaris-gate-mid.png"
    stream_preview_ok=fail
    if gamescopectl_shot "$mid" && png_nonblack "$mid"; then
      stream_preview_ok=ok
      log "stream_preview gamescopectl ok"
      if [[ -n "$SAVE_DIR" ]]; then
        mkdir -p "$SAVE_DIR"
        cp -f "$mid" "$SAVE_DIR/gate-stream-preview.png" 2>/dev/null || true
      fi
    else
      mid_api=/tmp/polaris-gate-mid-api.bin
      code=$(curl -sk -c "$JAR" -b "$JAR" -o "$mid_api" -w '%{http_code}' \
        ${CSRF:+-H "X-CSRF-TOKEN: $CSRF"} \
        "${URL}/api/display/screenshot" 2>/dev/null || echo 000)
      if [[ "$code" == "200" ]] && png_nonblack "$mid_api"; then
        stream_preview_ok=ok
        log "stream_preview API ok"
      else
        log "stream_preview fail"
      fi
    fi

    # encode path from journal since start
    encode_ok=fail
    if journalctl --user -u polaris.service --after-cursor="${jcursor:-}" --no-pager 2>/dev/null \
        | rg -q 'hevc_nvenc|h264_nvenc|av1_nvenc|Encoder cache saved: nvenc'; then
      if journalctl --user -u polaris.service --after-cursor="${jcursor:-}" --no-pager 2>/dev/null \
          | rg -q 'software|libx264|Encoder cache saved: software'; then
        log "WARN software encoder mentioned after start"
        encode_ok=fail
      else
        encode_ok=ok
        log "encode nvenc ok"
      fi
    else
      # may still be ok if cache hit silent
      if rg -q '^nvenc$' "${XDG_CONFIG_HOME:-$HOME/.config}/polaris/encoder_cache.txt" 2>/dev/null; then
        encode_ok=ok
        log "encode cache says nvenc"
      else
        log "encode path unclear"
        encode_ok=fail
      fi
    fi

    stop_body=$(printf '{"session_token":"%s"}' "${token:-}")
    stop_resp=$(curl_json POST /api/browser-stream/session/stop "$stop_body" || true)
    log "browser stop: ${stop_resp:0:200}"
    if printf '%s' "$stop_resp" | grep -qiE 'true|stopped'; then
      stream_stop_ok=ok
    else
      stream_stop_ok=fail
    fi

    disc=$(curl_json POST /api/clients/disconnect '{}' || true)
    log "webui disconnect: ${disc:0:200}"
    if printf '%s' "$disc" | grep -qiE '"status"[[:space:]]*:[[:space:]]*true|true'; then
      webui_stop_ok=ok
    else
      # disconnect may no-op if already stopped — treat stop_ok as enough when no active session
      if [[ "$stream_stop_ok" == ok ]]; then
        webui_stop_ok=ok
        log "webui disconnect soft-ok (session already stopped)"
      else
        webui_stop_ok=fail
      fi
    fi
    sleep 1
  fi
fi

# --- dual socket ---
if [[ ! -S "$RT/gamescope-1" ]]; then
  dual_socket_ok=ok
else
  dual_socket_ok=fail
  log "WARN gamescope-1 present"
fi

gate="solid-base-gate: units=$units_ok screencast=$screencast_ok preview=$preview_ok stream_start=$stream_start_ok stream_preview=$stream_preview_ok stream_stop=$stream_stop_ok webui_stop=$webui_stop_ok dual_socket=$dual_socket_ok encode=$encode_ok"
echo "$gate"

fail=0
[[ "$units_ok" == ok ]] || fail=1
[[ "$screencast_ok" == ok ]] || fail=1
[[ "$preview_ok" == ok ]] || fail=1
[[ "$dual_socket_ok" == ok ]] || fail=1
if [[ "$SKIP_BS" != "1" && -n "$PASS" ]]; then
  [[ "$stream_start_ok" == ok ]] || fail=1
  [[ "$stream_preview_ok" == ok ]] || fail=1
  [[ "$stream_stop_ok" == ok ]] || fail=1
  [[ "$webui_stop_ok" == ok ]] || fail=1
  [[ "$encode_ok" == ok ]] || fail=1
fi
exit "$fail"
