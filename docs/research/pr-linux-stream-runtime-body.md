## Summary

This PR lays the **Linux stream-path foundation** for [papi-ux/polaris#152](https://github.com/papi-ux/polaris/issues/152) (productize true-headless HDR through Gamescope / Portal / PipeWire).

It replaces ad-hoc mode booleans with a **path registry** (runtime × capture × topology), pluggable **private runtimes** (labwc + gamescope), working **Gamescope Stream** and **Headless Dongle** paths, portal/PipeWire hardening, and solid-base stop/preview/cancel fixes—so modes are selectable, documentable, and testable without inventing parallel config trees.

**Honest scope claim:** #152 foundation + clearer modes for #111 / #235 / #226. This does **not** claim full #152 “done,” idle preview green, or clean stop under load proven on every host.

## Motivation (#152 foundation)

Today Linux streaming is a kitchen-sink of booleans (`headless_mode`, cage compositor, GPU-native preference) that cannot express Gamescope HDR, dongle privacy swap, host virtual, and mirror desktop as first-class options. Upstream #152 needs:

1. Stable **path ids** clients and UI can select
2. **Runtime ownership** that does not hard-code labwc/cage forever
3. Portal/PipeWire capture that coexists with private gamescope stacks
4. A phone-free **smoke gate** so agents and CI can prove start/preview/stop

This branch is that foundation. Community EVDI / Family Mode (PR #226) map onto reserved registry slots rather than a second config tree.

## What's included

### Path registry

- `stream_path` registry (`src/platform/linux/stream_path.{h,cpp}`): each path is runtime × capture × topology
- Config key `linux_stream_mode = <path id>` with legacy boolean compatibility mapping for one cycle
- `stream_display_policy` centralizes resolve/apply/labels; availability probes (`gamescope` on PATH; dongle outputs at apply)
- Honest `runtime_backend` / `stream_path_id` for portal, host, gamescope, labwc
- Docs: [`docs/stream-paths.md`](../../stream-paths.md) plugin contract

| Path id | Status on this branch |
|---------|------------------------|
| `headless_stream` / `windowed_stream` | Available (labwc + wlroots) |
| `desktop_display` | Available (portal mirror / external gamescope) |
| `host_virtual_display` | Available |
| `gamescope_stream` | Available when `gamescope` on PATH |
| `headless_dongle` | Available when streaming/primary outputs + auto_manage set |
| `family_isolated` / `headless_evdi` | Reserved for PR #226 |

### Runtimes

- `stream_runtime` interface with **labwc** and **gamescope** adapters
- `gamescope_stream`: attach idle `gamescope-0` (or start `polaris-hdr-idle` / spawn owned headless); wrap app launches into that runtime; never use `gamescope-1` for portal
- Dongle connector auto-detect + DRM sysfs / `/api/linux/display-outputs` suggest; topology swap via kscreen-doctor
- UI path cards write full config (dongle outputs, gamescope/portal capture)

### Portal / capture

- Portal restore-token keep + invalidate/retry on SelectSources failure
- Wait for supported cursor modes (`AvailableCursorModes` ≠ 0) or omit
- Shared capture ownership so release cannot UAF negotiate/capture waiters
- Disconnect PipeWire under loop lock; ordered teardown (portal release before nested kill)

### Solid-base fixes

- Cancel-before-teardown; Moonlight `/cancel` owner path ignores stale sessiontoken (case-insensitive UUID)
- Browser Stream / `terminate_impl` / WebUI disconnect share prepare path: signal shutdown → **release portal/PipeWire** → **bounded capture join** → then pidfd-kill gamescope/labwc
- SB-5 mode-neutral Steam apps: migration + load-time unwrap of `polaris-hdr-session` hardwires to `steam-appid` + detached `rungameid` (optional Big Picture may keep nested WSI)
- Dashboard preview tries labwc, gamescope-0/1, host Wayland (grim), spectacle across paths
- Agent gates: `scripts/solid-base-gate.sh`, `scripts/solid-base-multimode-gate.sh` (timeouts; re-assert `browser_streaming` after mode helpers)

## Test plan

### Gate (phone-free)

```bash
export POLARIS_URL=https://127.0.0.1:47990
export POLARIS_USER=… POLARIS_PASSWORD=…
./scripts/solid-base-gate.sh
# expect machine line, e.g.:
# solid-base-gate: units=ok screencast=ok preview=… stream_start=ok stream_preview=ok stream_stop=… webui_stop=… dual_socket=ok encode=ok
```

Multi-mode (restore gamescope after labwc + desktop):

```bash
RESTORE_MODE=gamescope_stream \
  MODES='gamescope_stream headless_stream desktop_display' \
  ./scripts/solid-base-multimode-gate.sh
```

Optional dongle: `SKIP_DONGLE=0` when outputs are configured.

### Moonlight / Nova

1. Pair Nova or a standard Moonlight client on LAN.
2. Launch under `gamescope_stream` (and optionally `headless_stream` / `desktop_display`).
3. Confirm encode path (NVENC/VAAPI as host supports)—not pure software fallback for smoke.
4. Client quit / `/cancel`: prefer clean stop without connection reset; note residual if stop still wedges (see Residuals).
5. WebUI disconnect ends the session without leaving dual gamescope sockets.

### Regression focus

- [ ] Cold start: private bus has ScreenCast (`units` + `screencast`)
- [ ] Path cards only offer available primary paths; unavailable apply rejects
- [ ] Library Steam apps mode-neutral (no blanket `polaris-hdr-session` hardwire)
- [ ] Browser Stream start/stop does not leave `gamescope-1`
- [ ] Multimode conf helpers preserve `browser_streaming=enabled` (or gate re-asserts it)

## Out of scope

- Full #152 productization (HDR encode contract, Phase 4 polish, official packaging of host portal stack)
- Closing #235 / #234 as “fixed” by registry alone (Proton exclusive FS / true EDID virtual display need deeper backends)
- Community EVDI grab + Family Mode isolation (reserved slots only; integrate via PR #226)
- Virtual input / gyro / face-button headless issues (#222, #232)
- Idle preview product guarantee (SB-1 residual; mid-stream preview is the current gate proof)
- Guaranteeing zero SEGV / zero force-shutdown on every stop under load until SB-2 is re-proven post-deploy

## Residuals

Documented in [`docs/research/stream-path-rewrite-followups.md`](stream-path-rewrite-followups.md) and [`docs/research/solid-base-workflow.md`](solid-base-workflow.md).

| Item | Status | Notes |
|------|--------|--------|
| **SB-2 clean stop / RST** | **User-proven (`ad0ed6b`+)** | `session_media` ordered stop; lea journal 2026-07-28: portal destroy ~ms, client + host close, polaris stays active. Optional full agent gate for CI. |
| **SB-3 WebUI disconnect** | **User-proven** | Same stop path as session_media host terminate. |
| **SB-1 preview** | **Mid-stream proven** | 4K portal DMA-BUF + NVENC; idle-only residual optional. |
| **SB-4 cancel 470** | Code landed | Owner path observed in journal; optional full matrix. |
| **gamescopegrab** | **GO-WITH-PORTAL-FALLBACK** | Prefer session-graph Video/Source; private portal fallback. Idle units kept. |
| **kwingrab** | Deferred | Feasible on KDE; not this wave. |
| **Dongle polish** | Partial | Portal default after topology; auto-detect present. |
| **Multimode tooling** | Partial | Gate re-asserts `browser_streaming` after mode helpers. |

**Ship posture:** foundation with known residuals. Idle gamescope + private portal remain supported; Sunshine micro-adopts and gamescopegrab are incremental—not a stack flip.

## Related

- Upstream: [#152](https://github.com/papi-ux/polaris/issues/152), [#111](https://github.com/papi-ux/polaris/issues/111), [#226](https://github.com/papi-ux/polaris/pull/226)
- Docs: `docs/stream-paths.md`, `docs/runtime.md`, changelog “Unreleased” Linux stream modes section
- Fork solid-base issues (tracking): luxus/polaris SB-1…SB-7
