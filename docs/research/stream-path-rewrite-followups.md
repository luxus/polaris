# Stream-path rewrite — follow-ups & live bugs

**Branch:** `feat/linux-stream-runtime` (tip `439b403`, deployed lea 2026-07-28)  
**Last updated:** 2026-07-28  
**Host under test:** lea (NVIDIA + KDE + private gamescope portal stack)

### Progress snapshot (solid-base kickoff)

| SB | Status | Notes |
|----|--------|--------|
| #1 Preview | **Open (regressed)** | Gate **preview=fail**: idle `/api/display/screenshot` black / ≤50KB. Mid-stream `stream_preview=ok` (gamescopectl/last-frame path works under load). Fix idle capture or gamescopectl fallback before stream start. |
| #2 Clean stop / RST | **Code landed (verify)** | Root cause: async capture stop raced pidfd kill of gamescope → polaris SEGV in `pw_stream_queue_buffer` / gamescope `CVulkanDevice` dtor (coredump stacks). Fix: sync `prepare_for_session_teardown` before terminate; shared by browser stop, terminate_impl, WebUI disconnect. Re-run gate after deploy. |
| #3 WebUI disconnect | **Code landed (verify)** | Same prepare path in `disconnect` + `terminate_impl`; should return body when polaris survives stop. Re-run gate after deploy. |
| #4 Cancel 470 | **Code landed** | Controller role; owner case-insensitive; owner skips stale sessiontoken; cancel preflight + respond-first. Not re-proven this gate run (no Moonlight cancel step). |
| #5 Mode-agnostic apps | **Code landed** | Import + apps.json stay mode-neutral (`steam-appid`). migration v9 / inject unwrap lea hardwires. gamescope path applies attach env via wrap_cmd (X11 on gamescope-0). Nested WSI only for optional Big Picture entry. E2E mode-switch still open. |
| #6 Smoke harness | **Partial** | `solid-base-gate.sh` runs end-to-end; this run **fail**. Need `--max-time` on curl stop/disconnect helpers so agent runs cannot hang forever. |
| #7 Portal units | **Ok this run** | Gate **units=ok screencast=ok**. |
| Portal token/cursor | **Code landed** | Keep restore_token; invalidate+retry once on SelectSources failure; wait for AvailableCursorModes≠0; never permanently disable tokens. |

**Gate line (this stabilize run, lea):**  
`solid-base-gate: units=ok screencast=ok preview=fail stream_start=ok stream_preview=ok stream_stop=fail webui_stop=fail dual_socket=ok encode=ok` → **fail** (blockers: `preview`, `stream_stop`, `webui_stop`)

**Pin note:** after deploying this tip, drop or refresh `polaris-hdr-linux-patches` `fix-cancel-owner-token.patch` (now upstreamed).

**Test preference (approved):** Browser Stream API for agent smoke.

Working memory for the Linux stream-path rewrite. Do not drop these when context compresses — re-read before claiming “done”.

Related product docs:

- [`docs/stream-paths.md`](../stream-paths.md) — path registry contract
- Upstream productization: [papi-ux/polaris#152](https://github.com/papi-ux/polaris/issues/152)
- Community EVDI/dongle: [PR #226](https://github.com/papi-ux/polaris/pull/226)

---

## 1. Upstream issue triage (headless / virtual)

### Strong fit for this rewrite

| Issue | Title | Rewrite claim |
|-------|--------|----------------|
| **#152** | Productize true-headless HDR through Gamescope Portal/PipeWire | **Primary.** Path registry + `gamescope_stream` lifecycle foundation; portal coexistence |
| **#111** | Headless not working unless physical monitor is on | True-headless paths (`gamescope_stream` / `headless_stream`) must not need a lit panel; host-virtual/KMS fail closed |
| **#235** (mode confusion slice) | “Virtual Display” kitchen-sink | Explicit path cards: Host Virtual ≠ Dongle swap ≠ Gamescope HDR ≠ Mirror Desktop |
| **#226** (PR) | EVDI + dongle + Family | Reserved path ids; dongle/swap topology filled; EVDI/Family still reserved |

### Partial fit (right mode helps; root cause elsewhere)

| Issue | Notes |
|-------|--------|
| **#234** | Fullscreen Proton paints on physical monitor. Env reaches process; windowed works. Prefer **gamescope nested WSI** as Proton path; document labwc limits — registry alone does not fix VKD3D exclusive FS |
| **#211** | Tearing on headless labwc — A/B with gamescope path; not a registry win |
| **#92** / **#212** | AMD labwc stutter / RDNA4 VAAPI crash — capture/encode (see PR #215); keep GPU-native optional |
| **#235** (virtual as real EDID) | Needs real Host Virtual backend (`docs/research/kde-wayland-virtual-display.md`); dongle swap is the privacy substitute today |
| **#235** (HDR too dark / not HDR-capable) | #152 Phase 4 encode contract — not path registry |

### Out of scope for path rewrite

| Issue | Why |
|-------|-----|
| **#222** face buttons in headless | Virtual input / uinput |
| **#232** gyro | Motion → DS5 |
| **#35** black desktop / Main10 | Encoder / client color |

**PR messaging rule:** claim **#152 foundation + clearer modes for #111/#235/#226**. Do **not** claim #235 or #234 “fixed” by the registry alone.

---

## 2. Solid-base tracking issues (luxus/polaris)

| SB | Issue | Topic |
|----|-------|--------|
| SB-1 | https://github.com/luxus/polaris/issues/1 | Multi-path preview (grim fails on gamescope) |
| SB-2 | https://github.com/luxus/polaris/issues/2 | Clean stop / no connection reset |
| SB-3 | https://github.com/luxus/polaris/issues/3 | WebUI disconnect incomplete |
| SB-4 | https://github.com/luxus/polaris/issues/4 | Cancel 470 “another client” while quit works |
| SB-5 | https://github.com/luxus/polaris/issues/5 | Mode-agnostic Steam import (no polaris-hdr-session hardwire) |
| SB-6 | https://github.com/luxus/polaris/issues/6 | Headless smoke harness |
| SB-7 | https://github.com/luxus/polaris/issues/7 | Private portal units NameTaken / inactive after boot+stream |

**Agent smoke preference:** **Browser Stream** (`/api/browser-stream/status|session/start|stop`) — same runtime/capture stack, no phone. Moonlight `/cancel` still required for SB-4/RST.

**Stabilize workflow:** [`solid-base-workflow.md`](./solid-base-workflow.md) — gate script, pin/deploy path, Grok workflow `solid-base-stabilize`.

## 3. Live lea bugs (observed during rewrite testing)

These are **host/runtime regressions or unfinished cleanup**. Fix under SB-1…SB-7 before claiming gamescope_stream solid.

### 3.1 First stream start: crash / flaky open

**Symptom:** first start crashed/failed, retry opened.

**Context (2026-07-28):** private ScreenCast missing until `NIX_XDG_DESKTOP_PORTAL_DIR` pinned on `polaris-portal-dbus` (luxusAi `4e37c216`). Still verify cold start.

### 3.2 Client close: unclean shutdown → connection reset — **SB-2**

Closing stream → connection reset (used to be clean). Portal/RTSP teardown order; gamescope reattach.

### 3.3 Quit app: “another client” (470) but stream quits — **SB-4**

Exact message from `/cancel`: `The current session belongs to another client` (`other_owner`).

### 3.4 Preview broken on gamescope — **SB-1**

```text
WAYLAND_DISPLAY=gamescope-0 grim … → compositor doesn't support the screen capture protocol
```

Need portal/pipewire one-shot (or last-frame cache), not grim.

### 3.5 WebUI stop does not end session — **SB-3**

`POST /api/clients/disconnect` only `find_and_stop_session(uuid)` — not full `request_session_shutdown` / app terminate.

### 3.6 Apps hardwired to gamescope session — **SB-5**

All Steam imports: `polaris-hdr-session start|wait`. Mode switches need manual apps.json today.

### 3.7 Stack health checklist (lea)

1. portal dbus/gamescope/portal + idle + polaris **active**
2. private bus has ScreenCast
3. only gamescope-0
4. first connect works
5. clean stop (browser stream + Moonlight)
6. no false 470; WebUI disconnect works
7. preview works on active path

---

## 4. Rewrite deliverables still open

- [ ] SB-1 idle preview green again (gate `preview=ok`; frames non-black and >50KB before stream start)
- [ ] SB-2 browser-stream `session/stop` returns body promptly (gate `stream_stop=ok`); no HTTPS wedge — **code ready, needs post-deploy gate**
- [ ] SB-3 `POST /api/clients/disconnect` after browser-stream teardown (gate `webui_stop=ok`) — **code ready, needs post-deploy gate**
- [ ] Gate curl helpers: `--max-time` on stop/disconnect so agent runs cannot hang forever
- [x] SB-2 cancel-before-teardown + portal PW lock teardown + graceful stop (2026-07-28 code)
- [x] SB-2 portal release before nested kill (`release_global_capture`) (2026-07-28 code)
- [x] SB-2 sync capture join before gamescope kill (browser_stream + terminate_impl + disconnect) (2026-07-28)
- [x] Portal restore_token invalidate+retry + cursor wait (2026-07-28)
- [x] SB-5 gamescope wrap detached steam-appid + inject always-neutral unwrap (2026-07-28)
- [x] SB-5 migration v9 + load normalize unwrap polaris-hdr-session library hardwire (2026-07-28)
- [ ] SB-4 Moonlight `/cancel` re-smoke (not in this gate line)
- [ ] SB-2 post-deploy Moonlight connect/disconnect without client-visible fail / host SEGV
- [ ] SB-5 E2E labwc / dongle / gamescope mode switch with stock apps.json
- [ ] Owned `gamescope_stream` start/wait/stop + attach-idle polish
- [ ] Dongle auto-detect polish
- [ ] Optional: #152 PR comment

---

## 4. Infra notes (not Polaris code, but blocks testing)

| Item | Where | Note |
|------|--------|------|
| Shellcheck SC2034 on idle wait loop | luxusAi `polaris-hdr-session.nix` | Fixed (`while n=…`) |
| Private ScreenCast missing | luxusAi `polaris-portal-dbus` | Must set `NIX_XDG_DESKTOP_PORTAL_DIR` to **gamescope** portals (not user profile kde/kwallet) |
| Patches pin | polaris-hdr-linux-patches → luxus/polaris fork | phase1 portal + pipewire teardown |

---

## 5. Quick capture template (paste new repros below)

```text
Date:
Path (linux_stream_mode):
Client:
Steps:
Expected:
Actual:
Logs (host):
Logs (client):
```
