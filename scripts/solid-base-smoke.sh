#!/usr/bin/env bash
# solid-base-smoke.sh — agent-friendly gate for stream-path solid base (SB-6).
#
# Prefers Browser Stream for launch/stop; also checks display preview and
# WebUI force-disconnect. No phone / Moonlight required for the default gate.
#
# Env:
#   POLARIS_URL          default https://127.0.0.1:47990
#   POLARIS_USER         web UI username (default admin)
#   POLARIS_PASSWORD     web UI password (required)
#   POLARIS_COOKIE_JAR   path to cookie jar (default /tmp/polaris-solid-base.cookies)
#   POLARIS_APP_UUID     optional app uuid for browser stream
#   SKIP_BROWSER_STREAM  set to 1 to only check health + preview
#
set -euo pipefail

URL="${POLARIS_URL:-https://127.0.0.1:47990}"
USER_NAME="${POLARIS_USER:-admin}"
PASS="${POLARIS_PASSWORD:-}"
JAR="${POLARIS_COOKIE_JAR:-/tmp/polaris-solid-base.cookies}"
SKIP_BS="${SKIP_BROWSER_STREAM:-0}"
RT="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

preview_ok=fail
browser_start_ok=skip
browser_stop_ok=skip
stop_webui_ok=skip
session_idle_ok=fail
health_ok=fail
CSRF=""

log() { printf '[solid-base] %s\n' "$*" >&2; }

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

extract_csrf() {
  python3 - "$JAR" "$1" <<'PY'
import json, re, sys
jar, body = sys.argv[1], sys.argv[2]
try:
    for line in open(jar, encoding="utf-8", errors="ignore"):
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) >= 7 and "csrf" in parts[5].lower():
            print(parts[6].strip())
            raise SystemExit
except SystemExit:
    raise
except Exception:
    pass
try:
    j = json.loads(body)
    print(j.get("csrfToken") or j.get("csrf") or "")
except Exception:
    m = re.search(r"csrfToken[\"']?\s*[:=]\s*[\"']([^\"']+)", body)
    print(m.group(1) if m else "")
PY
}

# --- health ---
if systemctl --user is-active --quiet polaris.service 2>/dev/null; then
  health_ok=ok
  log "polaris.service active"
else
  log "polaris.service not active"
fi

if [[ -S "$RT/polaris-portal/bus" ]]; then
  if DBUS_SESSION_BUS_ADDRESS="unix:path=$RT/polaris-portal/bus" \
      busctl --user introspect org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop 2>/dev/null \
      | grep -q 'org.freedesktop.portal.ScreenCast'; then
    log "private ScreenCast present"
  else
    log "WARN: private bus without ScreenCast"
  fi
fi

if [[ -z "$PASS" ]]; then
  log "set POLARIS_PASSWORD"
  echo "solid-base-smoke: health=$health_ok preview=$preview_ok browser_start=$browser_start_ok browser_stop=$browser_stop_ok stop_webui=$stop_webui_ok session_idle=$session_idle_ok"
  exit 2
fi

rm -f "$JAR"
login_body=$(printf '{"username":"%s","password":"%s"}' "$USER_NAME" "$PASS")
login_resp=$(curl -sk -c "$JAR" -b "$JAR" -X POST "${URL}/api/login" \
  -H "Content-Type: application/json" -d "$login_body" || true)
log "login: ${login_resp:0:120}"
CSRF=$(extract_csrf "$login_resp" || true)
log "csrf present: $([[ -n "$CSRF" ]] && echo yes || echo no)"

# --- preview ---
prev_file=$(mktemp /tmp/polaris-preview-XXXXXX.bin)
code=$(curl -sk -c "$JAR" -b "$JAR" -o "$prev_file" -w '%{http_code}' \
  ${CSRF:+-H "X-CSRF-TOKEN: $CSRF"} \
  "${URL}/api/display/screenshot" 2>/dev/null || echo 000)
size=$(wc -c < "$prev_file" 2>/dev/null || echo 0)
magic=$(xxd -p -l 2 "$prev_file" 2>/dev/null || true)
# JPEG ffd8, PNG 8950 — reject JSON 7b
if [[ "$code" == "200" && "$size" -gt 200 && "$magic" != 7b* ]]; then
  preview_ok=ok
  log "preview ok code=$code size=$size magic=$magic"
else
  preview_ok=fail
  log "preview fail code=$code size=$size head=$(head -c 180 "$prev_file" | tr '\n' ' ')"
fi
rm -f "$prev_file"

# --- browser stream ---
if [[ "$SKIP_BS" != "1" ]]; then
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
    if "desktop" in n or "big picture" in n:
        print(a.get("uuid") or "")
        raise SystemExit
if apps:
    print(apps[0].get("uuid") or "")
' 2>/dev/null || true)
  fi
  if [[ -n "$app_uuid" ]]; then
    start_body=$(printf '{"appUuid":"%s","streamProfile":"balanced"}' "$app_uuid")
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
    if [[ -n "$token" ]] || printf '%s' "$start_resp" | grep -qiE 'token|url|true'; then
      browser_start_ok=ok
    else
      browser_start_ok=fail
    fi
    sleep 2
    stop_body=$(printf '{"token":"%s"}' "${token:-}")
    stop_resp=$(curl_json POST /api/browser-stream/session/stop "$stop_body" || true)
    log "browser stop: ${stop_resp:0:200}"
    if printf '%s' "$stop_resp" | grep -qiE 'true|stopped'; then
      browser_stop_ok=ok
    else
      browser_stop_ok=fail
    fi
    disc=$(curl_json POST /api/clients/disconnect '{}' || true)
    log "webui disconnect: ${disc:0:200}"
    if printf '%s' "$disc" | grep -qiE '"status"[[:space:]]*:[[:space:]]*true|true'; then
      stop_webui_ok=ok
    else
      stop_webui_ok=fail
    fi
    sleep 1
  else
    log "no app uuid for browser stream"
    browser_start_ok=fail
  fi
fi

# --- idle / dual socket ---
if systemctl --user is-active --quiet polaris.service 2>/dev/null; then
  if [[ ! -S "$RT/gamescope-1" ]]; then
    session_idle_ok=ok
  else
    session_idle_ok=fail
    log "WARN: gamescope-1 present (dual socket)"
  fi
else
  session_idle_ok=fail
fi

gate="solid-base-smoke: health=$health_ok preview=$preview_ok browser_start=$browser_start_ok browser_stop=$browser_stop_ok stop_webui=$stop_webui_ok session_idle=$session_idle_ok"
echo "$gate"

fail=0
[[ "$health_ok" == ok ]] || fail=1
[[ "$preview_ok" == ok ]] || fail=1
[[ "$session_idle_ok" == ok ]] || fail=1
if [[ "$SKIP_BS" != "1" ]]; then
  [[ "$browser_start_ok" == ok ]] || fail=1
  [[ "$browser_stop_ok" == ok ]] || fail=1
  [[ "$stop_webui_ok" == ok ]] || fail=1
fi
exit "$fail"
