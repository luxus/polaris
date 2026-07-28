# Stream-path rewrite — follow-ups & live bugs

**Branch:** `feat/linux-stream-runtime` (tip post-cleanup; prior deploy lea gen 439 as `50zsr6n…-2026-07-28` was `50929ef`)  
**Last updated:** 2026-07-28 (PR cleanup: docs + portal UAF + stop order + availability)  
**Host under test:** lea (NVIDIA + KDE + private gamescope portal stack)

### Progress snapshot (solid-base-final + multimode)

| SB | Status | Notes |
|----|--------|--------|
| #1 Preview | **Residual (issue closed)** | Gate still **preview=fail** idle: API ~32KB / gamescopectl empty. Mid-stream `stream_preview=ok` when a real stream starts. Reopen only if product needs idle preview green again. |
| #2 Clean stop / RST | **Open — not done** | Code `904a1a2` sync `prepare_for_session_teardown` deployed; single-mode gate still **stream_stop=fail webui_stop=fail**. Stop path hangs in `stop_video_capture` → 10s force shutdown → polaris **SIGTRAP** core + gamescope SEGV; empty stop/disconnect bodies; HTTPS :47990 wedged until restart. **Do not close #2.** Multimode did **not** re-prove stop (stream never started — see multimode note). |
| #3 WebUI disconnect | **Blocked by #2** | Same hang; `webui_stop=fail` when stop kills polaris. Issue already closed earlier — residual is stop path. |
| #4 Cancel 470 | **Code landed** | Not re-proven this run (no Moonlight cancel step). |
| #5 Mode-agnostic apps | **Verified inject/apps (recommend close)** | lea `apps.json` v9: **12** Steam library apps mode-neutral (`steam-appid` + detached `rungameid`); only optional **Steam Big Picture** keeps `polaris-hdr-session`. Inject `polaris-hdr-inject-app`: mode-neutral skip when already unwrapped. **Does not assume gamescope-only** for library titles. E2E labwc/dongle launch of a library game still optional residual. |
| #6 Smoke harness | **Partial** | Gate runs but stop can hang forever: `curl_json` still lacks `--max-time` (agent used temp curl wrap). Multimode: mode switchers wipe conf keys (see below). |
| #7 Portal units | **Ok this run** | Gate **units=ok screencast=ok** on all multimode paths. |
| Portal token/cursor | **Code landed** | Keep restore_token; invalidate+retry once on SelectSources failure; wait for AvailableCursorModes≠0. |

**Gate line (solid-base-final, lea post-deploy `50zsr6n`):**  
`solid-base-gate: units=ok screencast=ok preview=fail stream_start=ok stream_preview=ok stream_stop=fail webui_stop=fail dual_socket=ok encode=ok` → **fail** (blockers: `preview`, `stream_stop`, `webui_stop`)

**Multimode gate (2026-07-28 ~08:10 CEST, log `/tmp/solid-base-multimode-gate-lea.log`):**  
`multimode-gate: fail=1 modes=gamescope_stream headless_stream desktop_display restore=gamescope_stream`  
Per mode (all three identical shape):  
`units=ok screencast=ok preview=fail stream_start=fail stream_preview=fail stream_stop=ok webui_stop=ok dual_socket=ok encode=ok`

| Check | gamescope_stream | headless_stream | desktop_display | Why |
|-------|------------------|-----------------|-----------------|-----|
| units / screencast | ok | ok | ok | portal stack healthy |
| preview (idle) | fail | fail | fail | API PNG ~32KB / gamescopectl empty (SB-1 residual) |
| stream_start | fail | fail | fail | `Browser Stream is disabled in configuration` — `polaris-hdr-use-portal` / `polaris-hdr-use-labwc` **overwrite** conf without `browser_streaming = enabled` (default off) |
| stream_preview | fail | fail | fail | cascade (no session) |
| stream_stop / webui_stop | ok* | ok* | ok* | *vacuous — no live session; does **not** clear SB-2 |
| dual_socket | ok | ok | ok | no dual gamescope after stop |
| encode | ok* | ok* | ok* | *conf/cache says nvenc; no live encode proof without start |

**Root cause of multimode start fail (tooling, not path):** luxusAi mode helpers `cat >polaris.conf` minimal templates omit `browser_streaming` (and other lea keys). Multimode restore via `use-portal` also left host without browser stream until conf restored from `.bak-multimode-*`. Fix candidates: preserve/merge conf keys in use-portal/labwc; multimode-gate re-assert `browser_streaming=enabled` after each `apply_mode`; or gate refuses to run when `config_enabled=false`.

**Post-stop host (journal, single-mode final):** `Fatal: 10 seconds passed… Forcing shutdown` during `browser_stream::prepare_for_session_teardown` / `stop_video_capture`; coredumpctl shows `.polaris-wrappe` SIGTRAP + `gamescope-wl` SIGSEGV after gate stop attempts (08:04–08:05 CEST).

**Pin note:** tip includes SB-2 ordered stop + SB-5 unwrap; cancel-owner patch already upstreamed.

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

### 3.6 Apps hardwired to gamescope session — **SB-5** (largely fixed on lea)

**Was:** all Steam imports used `polaris-hdr-session start|wait`.  
**Now (lea v9):** library apps = mode-neutral `steam-appid` + detached `steam://rungameid/…`; only optional Steam Big Picture keeps nested HDR session. Inject skips when already neutral. Residual: multimode E2E launch of one library game under labwc + gamescope.

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
- [ ] **SB-2 blocker (needs post-deploy gate):** live `stream_stop`/`webui_stop` green with no polaris core. Code fixes in cleanup commit (not yet proven on lea):
  - prepare order: take capture state → **release portal** → **bounded join (3s)** then terminate
  - portal `g_capture` is `shared_ptr`; release sets `running=false` + `notify_all` (no UAF on negotiate wait / capture loop)
  - gate curl `--max-time` on stop/disconnect
- [ ] SB-3 residual: `webui_stop=ok` once stop path no longer kills polaris (issue closed; track under #2)
- [x] Gate curl helpers: `--max-time` on stop/disconnect (2026-07-28 cleanup)
- [x] SB-2 cancel-before-teardown + portal PW lock teardown + graceful stop (2026-07-28 code)
- [x] SB-2 portal release before nested kill (`release_global_capture`) (2026-07-28 code)
- [x] SB-2 sync capture join before gamescope kill (browser_stream + terminate_impl + disconnect) (2026-07-28)
- [x] SB-2 prepare: portal release **before** capture join + bounded join (2026-07-28 PR cleanup)
- [x] Portal restore_token invalidate+retry + cursor wait (2026-07-28)
- [x] Portal shared ownership + release wakes capture loop (2026-07-28 PR cleanup)
- [x] Docs honesty: runtime.md / stream-paths / changelog match gamescope+dongle available (2026-07-28 PR cleanup)
- [x] Availability dual-truth: `options_for_host` / `allowed_launch_modes` probe `gamescope_present` (2026-07-28 PR cleanup)
- [x] Multimode-gate re-asserts `browser_streaming=enabled` after use-portal/labwc (2026-07-28 PR cleanup) — host helpers still should merge conf (luxusAi follow-up)
- [x] SB-5 gamescope wrap detached steam-appid + inject always-neutral unwrap (2026-07-28)
- [x] SB-5 migration v9 + load normalize unwrap polaris-hdr-session library hardwire (2026-07-28) — **lea verified**
- [ ] SB-4 Moonlight `/cancel` re-smoke (not in this gate line)
- [ ] SB-2 post-deploy solid-base-gate + multimode with real starts (browser_streaming preserved)
- [ ] SB-5 optional: multimode gate launch one library game under labwc + gamescope with stock apps.json
- [ ] luxusAi: `polaris-hdr-use-portal` / `use-labwc` merge conf keys instead of full overwrite (host follow-up; gate works around it)
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
