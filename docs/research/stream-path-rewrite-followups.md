# Stream-path rewrite — follow-ups & live bugs

**Branch:** `feat/linux-stream-runtime`  
**Last updated:** 2026-07-28 (tip **`ad0ed6b`** live on lea; **user-proven** preview + client close + host stop; journal clean — see §3.8)  
**Host under test:** lea (NVIDIA + KDE + private gamescope portal stack)  
**Modularity target:** [`linux-stream-modularity.md`](./linux-stream-modularity.md)  
**Sunshine adopt plan:** §5 below (micro-adopts yes; **no** blind switch off idle portal stack until W3 gamescopegrab GO)

### Progress snapshot (solid-base-final + multimode + harden W0 + user prove)

| SB | Status | Notes |
|----|--------|--------|
| #1 Preview | **User-proven mid-stream (recommend close / residual idle-only)** | User + journal 20:05–20:08: live **4K portal DMA-BUF** + NVENC; capture streaming. W0 foundation gate also **preview=ok**. Full agent gate lifecycle still optional. |
| #2 Clean stop / RST | **User-proven on tip `ad0ed6b` (recommend close)** | Journal: `CLIENT DISCONNECTED` → portal **destroy done ~3 ms**; host stop via **`session_media`** → destroy done **~2 ms** → `Process terminated` → `stream_ended [idle]`. **No** force-shutdown / polaris core; service stayed **active**. Automated full gate still not re-run (`stream_stop=skip` on W0) — optional re-smoke for CI honesty. |
| #3 WebUI disconnect | **User-proven (recommend close under #2)** | User: closing from WebUI works. Host path matches `session_media` + terminate; polaris remains up. Exact log string `WebUI disconnect: responding…` not always present; behavior confirmed. |
| #4 Cancel 470 | **Code landed + partial** | Journal `cancel: client=[Bedroom] owner=yes preflight_outcome=0` after first session; full Moonlight quit matrix still optional. |
| #5 Mode-agnostic apps | **Verified inject/apps (recommend close)** | lea `apps.json` v9: **12** Steam library apps mode-neutral (`steam-appid` + detached `rungameid`); only optional **Steam Big Picture** keeps `polaris-hdr-session`. Inject `polaris-hdr-inject-app`: mode-neutral skip when already unwrapped. **Does not assume gamescope-only** for library titles. E2E labwc/dongle launch of a library game still optional residual. |
| #6 Smoke harness | **Partial** | W0 foundation pass; full lifecycle gate not re-run after user prove. Multimode: mode switchers wipe conf keys (see below). |
| #7 Portal units | **Ok** | units/screencast ok; private bus ScreenCast + cursor modes=7. |
| Portal token/cursor | **Code landed** | Keep restore_token; invalidate+retry once on SelectSources failure; wait for AvailableCursorModes≠0. |

**User prove (2026-07-28 ~20:05–20:08 CEST, tip `ad0ed6b`, store `polaris-stream-0-unstable-2026-07-28`):**  
preview works · client close works · WebUI/host close works · journal: portal destroy ms-scale · **no** SIGTRAP / force-shutdown · polaris **active** after stop.

**Gate line (stream-runtime-harden W0, earlier tip `6cd1777` then tip advanced to `ad0ed6b`):**  
`solid-base-gate: units=ok screencast=ok preview=ok stream_start=skip stream_preview=skip stream_stop=skip webui_stop=skip dual_socket=ok encode=skip spike=n/a` → **pass** (foundation only; user prove supersedes skip for SB-2/3 product status)

**Gate line (prior solid-base after SB-2 pin `90d4ca6` / lea gen 440 `c6m365nm` — full lifecycle):**  
`solid-base-gate: units=ok screencast=ok preview=fail stream_start=ok stream_preview=ok stream_stop=fail webui_stop=fail dual_socket=ok encode=ok` → **fail** (blockers: `preview`, `stream_stop`, `webui_stop`)

**Multimode gate (2026-07-28 ~08:30 CEST, log `/tmp/solid-base-multimode-gate-lea2.log` + labwc solo `/tmp/solid-base-labwc-solo.log`):**  

Harness fixes landed in `scripts/solid-base-multimode-gate.sh` / `solid-base-gate.sh`:
- restore full conf backup after `polaris-hdr-use-*` templates (preserve `browser_streaming`, bitrate, …)
- `GATE_SOFT_IDLE_PREVIEW=1` (idle preview is SB-1 residual; stream_preview still required)
- mode-aware units/screencast (labwc/desktop do not require private gamescope portal)
- labwc overlay: `hevc_mode=1` / `av1_mode=1` + clear encoder cache (see labwc note)

| Mode | stream_start | stream_preview | stream_stop | webui_stop | encode | Notes |
|------|--------------|----------------|-------------|------------|--------|-------|
| **gamescope_stream** | **ok** | **ok** | **fail** | **fail** | ok | Live mid-stream gamescopectl; stop hangs in portal release (SB-2 residual) |
| **headless_stream (labwc)** | **ok*** | **ok*** | **ok*** | **ok*** | **ok*** | *solo retest with SDR hevc/av1=1: labwc HEADLESS-1 + ext-image DMA-BUF + NVENC H.264 + clean stop. Multimode first pass failed encode with hevc_mode=3 (10-bit probe). |
| **desktop_display** | **ok** | **ok** | **fail** | **fail** | ok | Starts with portal/session; same SB-2 release hang as gamescope |
| **headless_dongle** | skip | — | — | — | — | `SKIP_DONGLE=1` (no `linux_streaming_output` / primary on lea) |

**Labwc residual:** cold cage reprob with `hevc_mode=3`/`av1_mode=3` fails NVENC on the 10-bit HDR validation path after successful SDR h264/hevc/av1 converters. SDR-only modes pass. Product fix still needed for true HDR labwc encode probe; multimode harness uses SDR for smoke.

**Root cause of earlier multimode start fail (tooling, fixed):** luxusAi mode helpers `cat >polaris.conf` minimal templates omit `browser_streaming`. Gate now restores backup then overlays mode keys.

**SB-2 root cause evolution:**
1. **Pre-bcd6f02 (`50zsr6n`):** `postBrowserStreamStop` → `prepare_for_session_teardown` called **unbounded** `stop_video_capture` (`thread::join`) **before** `portal::release_global_capture` on confighttp HTTPS thread → capture stuck in portal D-Bus / pipeline join → :47990 wedge → SIGTERM 10s force-shutdown **SIGTRAP** + gamescope torn down with PipeWire attached → CVulkanDevice dtor **SIGSEGV**.
2. **Tip order (bcd6f02+):** release-then-3s-bound-join + `shared_ptr` capture ownership; **`90d4ca6`** also answers JSON after bounded teardown / can defer app terminate.
3. **Post-deploy prove (`90d4ca6` / gen 440):** start+preview ok; **stream_stop/webui_stop still fail**. Hang is now **inside `portal::release_global_capture` (between teardown step2 and step3)** — not the old unbounded join-before-release. **No new polaris coredump** this run.
4. **W0 + user prove (`ad0ed6b`, 2026-07-28 ~20:05–20:08):** `session_media` sole owner; client disconnect and host terminate release portal in **milliseconds** (`async capture/session destroy done`); process terminates; polaris stays active. Treat SB-2 as **user-proven**; keep optional full-gate re-smoke for CI.

**Post-stop host (prior gen 439 / `50zsr6n` journal):** `Fatal: 10 seconds passed… Forcing shutdown` during prepare / `stop_video_capture`; coredumpctl `.polaris-wrappe` SIGTRAP + `gamescope-wl` SIGSEGV (08:04–08:05 CEST). **Not re-observed** on gen 440 stop prove (empty/timeout responses instead).

**Pin note:** harden **W0** deployed tip **`6cd1777`** (`session_media` single stop owner + stream_runtime facade). Prior full-lifecycle pin **`90d4ca6`** (gen 440) still the last measured **stream_stop/webui_stop fail**. Ordered stop + answer-before-terminate landed; **release_global_capture may still block under load** — **SB-2 remains open until full gate shows `stream_stop=ok webui_stop=ok`.**

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
5. clean stop (browser stream + Moonlight) — **user-proven 2026-07-28**
6. no false 470; WebUI disconnect works — **user-proven 2026-07-28**
7. preview works on active path — **user-proven 2026-07-28**

### 3.8 User prove journal (tip `ad0ed6b`, 2026-07-28 ~20:05–20:08 CEST)

| Event | Log evidence |
|-------|----------------|
| Stream up | `Session started for [Bedroom]`; portal **DMA-BUF** `3840x2160` BGRx; **nvenc** (hevc); private bus ScreenCast; cursor modes=7 |
| Client close | `CLIENT DISCONNECTED` → `Session ended` → `portal: async capture/session destroy done` (~3 ms) → session paused/resumable |
| Cancel follow-up | `cancel: client=[Bedroom] owner=yes preflight_outcome=0` + `session_media` steps (idempotent, had_media=false) |
| Host / WebUI-style stop | `session_media` step1–3 + **destroy done ~2 ms** → `Process terminated` → `stream_ended [idle]` |
| Host health | **No** `Fatal: 10 seconds… Forcing shutdown`; polaris **active** after stop |

---

## 4. Rewrite deliverables still open

### stream-runtime-harden phases

| Phase | Goal | Status |
|-------|------|--------|
| **W0** | Land `session_media` modularity + no foundation regression | **Done** tip **`ad0ed6b`** live; user-proven stop/preview |
| **W1** | Sunshine PipeWire **micro-adopts** (dtor order, serial, local core) | **Done** (code in tree — pin after commit) |
| **W2** | Portal hygiene: cage screencopy quarantine | **Partial** — marked; full extract deferred (no growth) |
| **W3** | SPIKE gamescopegrab | **GO-WITH-PORTAL-FALLBACK** — see `gamescopegrab-spike.md`; lea node 283 |
| **W4** | SPIKE kwingrab | **GO deferred** — see `kwingrab-spike.md` (not this wave) |
| **W5** | Integrate GO spikes | **Done** gamescopegrab prefer local Video/Source + portal fallback |
| **W6** | Docs + PR honesty | **In progress** |
| **W7** | Optional history rewrite + coordinated pin | After this commit + pin |

### Residual checklist

- [x] SB-1 mid-stream / product preview path works (user + journal 4K portal DMA-BUF)
- [ ] SB-1 optional: idle preview green under **full** automated lifecycle gate
- [x] **SB-2 user-proven on `ad0ed6b`:** client close + host stop; portal destroy ms-scale; no force-shutdown core
  - [x] prepare order: take capture state → **release portal** → **bounded join (3s)** then terminate (code)
  - [x] portal `g_capture` is `shared_ptr`; release wakes waiters
  - [x] gate curl `--max-time` on stop/disconnect
  - [x] HTTP stop/disconnect answer JSON before nested app/gamescope terminate
  - [x] **W0:** `session_media` sole schedule/prepare owner + stream_runtime facade (`ad0ed6b`)
  - [x] **User prove:** destroy done in ms; polaris stays active
  - [ ] Optional: full agent gate `stream_stop=ok webui_stop=ok` for CI/docs honesty
  - [ ] W1: Sunshine PW dtor order A/B still valuable under load (not blocking SB-2 close)
- [x] SB-3 WebUI/host disconnect works (user-proven; track optional gate line under #2)
- [x] Gate curl helpers: `--max-time` on stop/disconnect (2026-07-28 cleanup)
- [x] SB-2 cancel-before-teardown + portal PW lock teardown + graceful stop (2026-07-28 code)
- [x] SB-2 portal release before nested kill (`release_global_capture`) (2026-07-28 code)
- [x] SB-2 sync capture join before gamescope kill (browser_stream + terminate_impl + disconnect) (2026-07-28)
- [x] SB-2 prepare: portal release **before** capture join + bounded join (2026-07-28 PR cleanup)
- [x] SB-2 stop never wedges HTTPS (answer-then-`session_media` worker)
- [x] Portal restore_token invalidate+retry + cursor wait (2026-07-28)
- [x] Portal shared ownership + release wakes capture loop (2026-07-28 PR cleanup)
- [x] Docs honesty: runtime.md / stream-paths / changelog match gamescope+dongle available (2026-07-28 PR cleanup)
- [x] Availability dual-truth: `options_for_host` / `allowed_launch_modes` probe `gamescope_present` (2026-07-28 PR cleanup)
- [x] Multimode-gate re-asserts `browser_streaming=enabled` after use-portal/labwc (2026-07-28 PR cleanup) — host helpers still should merge conf (luxusAi follow-up)
- [x] SB-5 gamescope wrap detached steam-appid + inject always-neutral unwrap (2026-07-28)
- [x] SB-5 migration v9 + load normalize unwrap polaris-hdr-session library hardwire (2026-07-28) — **lea verified**
- [ ] SB-4 Moonlight `/cancel` re-smoke (optional; cancel owner path seen in journal)
- [ ] Optional: full solid-base-gate + multimode for CI honesty (product SB-2 user-proven)
- [ ] SB-5 optional: multimode gate launch one library game under labwc + gamescope with stock apps.json
- [ ] luxusAi: `polaris-hdr-use-portal` / `use-labwc` merge conf keys instead of full overwrite (host follow-up; gate works around it)
- [ ] Owned `gamescope_stream` start/wait/stop + attach-idle polish
- [ ] Dongle auto-detect polish
- [ ] Optional: #152 PR comment

---

## 5. Sunshine approach vs current “idle” stack — what we plan

**Short answer:** we are **not** planning a big-bang switch off the lea **idle gamescope + private portal** stack. We **are** planning to **steal Sunshine patterns surgically**, and only **optionally** move gamescope paint capture to Sunshine-style **direct PipeWire node (gamescopegrab)** if a spike proves it.

| Layer | Today (lea “idle”) | Sunshine-ish | Polaris plan |
|-------|--------------------|--------------|--------------|
| **Runtime** | Attach idle `gamescope-0` / `polaris-hdr-idle` (or own spawn) | Sunshine does **not** own gamescope | **Keep** `stream_runtime` + idle attach (product value) |
| **Capture gamescope** | Private portal D-Bus → ScreenCast → PW remote fd | Discussion #1007: grab gamescope’s **raw PW output node** (no portal) | **W3 spike** → GO only then prefer node; portal stays fallback |
| **Capture host KDE** | Portal (host bus) / dongle portal after topology | **kwingrab** (zkde screencast → local PW) | **W4 spike** → optional for dongle/desktop |
| **PW core** | `pipewire_capture` (stop-loop-first dtor) | lock→disconnect→stop loop; object serial; maxFramerate; Closed | **W1 micro-adopts** (safe, no stack flip) |
| **Stop** | `session_media` ordered release before nested kill | RAII dtor; empty `streaming_will_stop` | **Keep session_media** until capture is not portal-into-dying-gamescope |

### Decision rules

1. **W1 (next):** adopt Sunshine **PipeWire hygiene** only — dtor order A/B, object serial, session Closed. Does **not** remove idle units or private portal.  
2. **W3 gamescopegrab:** prototype connect to gamescope node **without** private ScreenCast.  
   - **GO** → W5 can make `gamescope_stream` prefer node capture (simpler stop, less portal hang class). Idle unit may still run gamescope; we just stop needing portal for paint.  
   - **NO-GO / GO-WITH-PORTAL-FALLBACK** → **keep idle + private portal** as today; polish only.  
3. **Do not** delete polaris-hdr-idle / private portal units until node path is gate-green on lea.  
4. **labwc** stays solid default path (wlroots capture); Sunshine has no labwc product model.

So: **idle stays the supported gamescope host layout** until W3 proves a better capture plug-in. Sunshine is a **source of capture/teardown ideas**, not a wholesale architecture replace.

---

## 6. Infra notes (not Polaris code, but blocks testing)

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
