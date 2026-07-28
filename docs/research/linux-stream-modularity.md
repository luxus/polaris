# Linux stream modularity (target architecture)

**Branch:** `feat/linux-stream-runtime`  
**Goal:** stop the fix-on-fix stack by giving each concern one owner.

## Layers (top → bottom)

```text
┌─────────────────────────────────────────────────────────────┐
│  WebUI / client-settings / confighttp API                   │
│  path cards write linux_stream_mode + capture defaults      │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  stream_path          vocabulary (id × runtime × capture)   │
│  stream_display_policy resolve/apply + legacy bool bridge   │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  process (launch)  +  stream_runtime (labwc / gamescope)    │
│  display_topology (dongle prepare/restore)                  │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  capture backends: portal_grab → pipewire_capture           │
│                    wlgrab / kmsgrab / …                     │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  session_media   SINGLE ordered stop for media + PW/portal  │
│  process::terminate_impl   nested kill AFTER session_media  │
└─────────────────────────────────────────────────────────────┘
```

## Rules

1. **Path ids only** for new modes — never new booleans.
2. **`stream_runtime`** is the only entry for private compositor start/stop/socket (cage_display_router is an implementation detail of the labwc adapter).
3. **`session_media::prepare_for_stop`** is the only ordered media teardown used by process/confighttp.
4. **`session_media::schedule`** is the only post-HTTPS stop worker (no bare `.detach()` for portal teardown).
5. **Defaults:** labwc `headless_stream` is the solid product default; `gamescope_stream` when host stack ready; dongle capture **portal** (kms advanced).
6. **WebUI:** hide labwc-only advanced flags when gamescope/host paths are selected; checklist is path-scoped.

## Setup for a new host (minimal)

1. Install Polaris (CUDA on NVIDIA).
2. WebUI → Audio/Video → **Private Stream** (labwc) or **Gamescope Stream**.
3. Pair client; set encoder if needed; leave capture alone.
4. Dongle only: Detect connectors → save streaming + primary → privacy swap.
5. Optional host stack (gamescope): private portal units + `gamescope` on PATH (lea / polaris-hdr-session).

No conf template rewrite required for Browser Stream smoke if `browser_streaming=enabled` is already set.

## Residual (known issues OK for foundation ship)

- Idle preview may stay soft-fail; mid-stream preview is the proof.
- Portal destroy may still exceed the short budget and finish on the session_media worker — HTTPS always returns.
- History rewrite to 8–12 commits after this hardening lands.
