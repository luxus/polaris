# Solid-base stabilize workflow

**Goal:** close SB-1…SB-7 into a **wonderful stable baseline** on lea, keep **polaris-hdr-linux-patches** and **luxusAi** in lockstep, and give agents a **phone-free** way to spin up a stream and prove the stack.

**Last updated:** 2026-07-28

---

## 1. Three layers (do not collapse them)

```text
┌─────────────────────────────────────────────────────────────┐
│  A. Grok workflow  (.grok/workflows/solid-base-stabilize)   │
│     Orchestrates agents: fix → pin → deploy → gate → report │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  B. Deploy path  (lea-build-deploy / nh os switch)           │
│     polaris tip → patches rev+hash → luxusAi lock → switch  │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  C. Gate  (scripts/solid-base-gate.sh)                      │
│     Deterministic checks agent can run without Moonlight    │
└─────────────────────────────────────────────────────────────┘
```

| Layer | Who runs it | Side effects | Pass criterion |
|-------|-------------|--------------|----------------|
| **C Gate** | any agent / human | browser stream start/stop, unit queries | exit 0 + machine-readable line |
| **B Deploy** | deploy agent only | store builds, nh switch, unit restart | gen active + polaris path CUDA |
| **A Workflow** | `/workflow solid-base-stabilize` | all of the above + optional issue comments | gate green after deploy |

Agents **must not** invent ad-hoc `systemctl` drop-ins or impure non-CUDA polaris binaries (black-screen footgun). Deploy always goes through B.

---

## 2. Issue order (dependency DAG)

Fix and verify in this order. Parallel only where listed.

```text
SB-7  portal units NameTaken / inactive     [luxusAi]     ──┐
                                                            │
SB-1  multi-path preview                    [polaris]     ──┤
SB-3  WebUI disconnect                      [polaris]     ──┼─ code (can parallel after SB-7 host healthy)
SB-4  cancel 470 other_owner                [polaris]     ──┤
SB-2  clean stop / no RST                   [polaris]     ──┘
         │
SB-5  mode-agnostic Steam import            [polaris+luxusAi inject]
         │
SB-6  smoke harness (= this gate)           [scripts + CI]
```

**Why SB-7 first:** a NameTaken storm + inactive `polaris-portal` makes cold-start and restart flaky; every other test is noise until units are honest.

**Why Browser Stream for most checks:** same portal/NVENC/runtime path as Moonlight without a phone. Moonlight `/cancel` still required to fully close **SB-2/SB-4** client-protocol edges (optional second gate phase).

---

## 3. Agent-testable stream (layer C)

### 3.1 Credentials

```bash
export POLARIS_URL=https://127.0.0.1:47990
export POLARIS_USER=luxus          # lea web UI user
export POLARIS_PASSWORD='…'        # never log this
# optional: POLARIS_APP_UUID=…     # default: Steam Big Picture
```

Do **not** commit passwords. Agents may read `~/.config/polaris/` only when the user has already set a password; prefer env.

### 3.2 One command gate

```bash
# Full solid-base gate (units + ScreenCast + preview + browser stream lifecycle)
./scripts/solid-base-gate.sh

# Faster: skip browser stream (units + preview only)
SKIP_BROWSER_STREAM=1 ./scripts/solid-base-gate.sh

# During an existing Moonlight session (preview-only stress)
PREVIEW_ONLY=1 ./scripts/solid-base-gate.sh

# Multi-mode: gamescope + labwc (headless_stream) + desktop_display, then restore gamescope
RESTORE_MODE=gamescope_stream \
  MODES='gamescope_stream headless_stream desktop_display' \
  ./scripts/solid-base-multimode-gate.sh

# Optional dongle (needs configured outputs)
SKIP_DONGLE=0 MODES='... headless_dongle' ./scripts/solid-base-multimode-gate.sh
```

Workflow: `solid-base-multimode` runs the multi-mode gate and restores `gamescope_stream`.

Exit **0** only if every enabled check is `ok`. Last line is machine-parseable:

```text
solid-base-gate: units=ok screencast=ok preview=ok stream_start=ok stream_preview=ok stream_stop=ok webui_stop=ok dual_socket=ok encode=ok
```

### 3.3 What the gate checks

| Check | How | Maps to |
|-------|-----|---------|
| `units` | `systemctl --user is-active` polaris + portal-dbus; gamescope portal **either** unit active **or** name owned on private bus without restart storm (`NRestarts` flat) | SB-7 |
| `screencast` | private bus has `org.freedesktop.portal.ScreenCast` | cold start |
| `preview` | `/api/display/screenshot` → PNG/JPEG **or** fallback `gamescopectl screenshot` non-black | SB-1 |
| `stream_start` | `POST /api/browser-stream/session/start` with Big Picture uuid | SB-6 |
| `stream_preview` | while session up: API or `gamescopectl` frame mean > threshold | SB-1 mid-stream |
| `stream_stop` | browser-stream stop returns stopped | SB-2 partial |
| `webui_stop` | `POST /api/clients/disconnect` ends session | SB-3 |
| `dual_socket` | no `gamescope-1` after stop | dual-socket footgun |
| `encode` | journal slice shows `nvenc` / not software fallback after start | black-screen footgun |

**Preferred test app:** Steam Big Picture (`polaris-hdr-session` wait) — lighter than full games, still exercises gamescope portal path. Override with `POLARIS_APP_UUID` for a real title when needed.

### 3.4 What the agent still cannot fully automate

| Gap | Mitigation |
|-----|------------|
| Real Moonlight `/cancel` RTSP | Optional `scripts/solid-base-moonlight-notes.md` manual checklist; or host a tiny moonlight-qt headless later |
| Human visual quality | Gate uses luminance + size; optional save PNG under `images/gate-*.png` for human glance |
| Full Steam download | Assume library already present on lea |

---

## 4. Pin / deploy path (layer B)

Canonical path (never skip CUDA):

```text
1. polaris (feat/linux-stream-runtime)
   git push origin HEAD

2. polaris-hdr-linux-patches
   - set pkgs/polaris-stream rev = polaris tip
   - nix build .#polaris-stream   # cuda forced in flake
   - update src hash if needed (prefetch with fetchSubmodules)
   - commit + push

3. luxusAi
   nix flake update polaris-hdr-linux-patches
   commit flake.lock
   PATH=/run/wrappers/bin:$PATH nh os switch
   # only if polaris binary path changed:
   systemctl --user restart polaris.service
   # portal unit changes:
   systemctl --user daemon-reload
   systemctl --user restart polaris-portal-dbus polaris-portal-gamescope polaris-portal polaris-hdr-idle

4. ./scripts/solid-base-gate.sh
```

**Rules:**

- Pure `nix build .#polaris-stream` must be **CUDA** (patches flake forces `cudaSupport = true`).
- No `polaris.service.d/*local-build*` drop-ins.
- Keep `/run/wrappers/bin` first for `nh` (setuid sudo).

Helper (optional): `scripts/pin-polaris-to-patches.sh <rev>` updates rev+hash in the patches package and prints the commit message.

Deploy long work can use the **lea-build-deploy** agent so chat stays free; parent always re-runs the **gate** itself after deploy returns.

---

## 5. Grok workflow (layer A)

**File:** `.grok/workflows/solid-base-stabilize.rhai`  
**Invoke:** `/workflow solid-base-stabilize` or tool `workflow` with `name: "solid-base-stabilize"`.

### Phases

| Phase | Agents | Capability | Output |
|-------|--------|------------|--------|
| **0 Baseline** | 1 read-only | status of SB issues, unit health, git tips | snapshot JSON |
| **1 Host SB-7** | 1 execute on luxusAi | fix portal unit ownership; no polaris C++ | PR or local commit note |
| **2 Polaris code** | sequential agents per open SB in {1,3,4,2,5} | worktree optional; tests + local logic | commits on feat branch |
| **3 Pin + deploy** | 1 execute | patches pin + luxusAi lock + nh | generation + polaris store path |
| **4 Gate** | 1 execute | `solid-base-gate.sh` | pass/fail line |
| **5 Report** | 1 read-only | update followups + draft issue comments | report path |

### Args

```json
{
  "skip_deploy": false,
  "skip_host": false,
  "issues": [7, 1, 3, 4, 2, 5],
  "polaris_branch": "feat/linux-stream-runtime",
  "password_env": "POLARIS_PASSWORD"
}
```

### Pause points

- Missing `POLARIS_PASSWORD` → `pause(verification, …)`.
- Gate fail after deploy → `await_user` with log tail (don’t infinite-retry builds).
- nh/sudo failure → stop; human fixes PATH/wrappers.

### Budget

Default ~20–40 agent calls per full run (not 128). Prefer sequential issue fixes over huge parallel fan-out so deploys stay ordered.

---

## 6. Definition of “wonderful stable baseline”

All true on lea after cold boot + one agent-driven gate:

1. Portal units honest: no NameTaken restart storm; ScreenCast on private bus.
2. `solid-base-gate.sh` exit 0 twice in a row (re-entrancy).
3. Mid-stream preview non-black (API or gamescopectl).
4. Browser stream start/stop leaves session idle; no `gamescope-1`.
5. WebUI disconnect ends session (SB-3).
6. Encoder path stays **nvenc** (not software cache).
7. Patches pin matches polaris tip used on host; luxusAi lock current.
8. SB-1…SB-7 either **closed** or explicitly deferred with reason in followups.

Optional gold: one Moonlight connect/disconnect without RST (SB-2) logged once by human or later automation.

---

## 7. Day-to-day agent recipe (short)

```bash
# 0. env
export PATH=/run/wrappers/bin:/run/current-system/sw/bin:$PATH
export POLARIS_USER=luxus POLARIS_PASSWORD='…'

# 1. am I broken before coding?
./scripts/solid-base-gate.sh || true   # capture baseline

# 2. fix one SB (host or polaris), then if polaris binary changed:
#    pin → nh → restart units (layer B)

# 3. prove it
./scripts/solid-base-gate.sh

# 4. only then mark issue fixed / update followups
```

Or run the full orchestrator when several SBs are open:

```text
/workflow solid-base-stabilize
```

---

## 8. File map

| Path | Role |
|------|------|
| `docs/research/solid-base-workflow.md` | this design |
| `docs/research/stream-path-rewrite-followups.md` | live SB status |
| `scripts/solid-base-smoke.sh` | lighter smoke (subset) |
| `scripts/solid-base-gate.sh` | full agent gate (SB-6 implementation); curl `--max-time` on stop/disconnect |
| `scripts/solid-base-multimode-gate.sh` | run gate under gamescope / labwc / desktop (+ optional dongle); re-asserts `browser_streaming` after mode helpers |
| `scripts/pin-polaris-to-patches.sh` | rev+hash helper for patches flake |
| `.grok/workflows/solid-base-stabilize.rhai` | orchestrator |
| luxusAi `modules/nixos/polaris-hdr-session.nix` | SB-7 units |
| polaris-hdr-linux-patches | package pin + CUDA default |

---

## 9. Portal restore token + cursor modes

| Item | Policy |
|------|--------|
| **Restore token** (`portal_restore_token.txt`) | **Keep enabled** — headless auto-select without picker |
| **Do not** permanently disable tokens | Worse UX for GameStream |
| **On SelectSources failure** | Invalidate saved token and retry once without it; save new token on success |
| **Cursor mode** | Query `AvailableCursorModes`; pick Embedded→Metadata→Hidden; omit if 0 (client-correct) |
| **Root fix** | Portal must advertise modes ≠ 0 once gamescope control is live (host bind order / rebind) — not “omit forever” |

Workflow `solid-base-stabilize` residual phases encode this policy.

## 10. Anti-patterns (learned the hard way)

| Don’t | Do |
|-------|-----|
| Impure non-CUDA polaris + systemd drop-in | Always CUDA package via nh |
| Fix five SBs then one giant deploy | Fix → pin → deploy → gate per host-critical change |
| Treat “stream works” as units healthy | Gate checks both |
| X11 grab for gamescope preview | gamescopectl or portal/last-frame |
| `find /nix/store` in smoke paths | Known store paths / nix shell packages only |
| `nh` without `/run/wrappers/bin` | PATH prefix for setuid sudo |
| Disable restore token to “fix” capture | Invalidate + retry; fix portal modes |
