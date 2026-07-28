# Spike W3: gamescopegrab (direct PipeWire Video/Source)

**Date:** 2026-07-28  
**Host:** lea  
**Decision:** **GO-WITH-PORTAL-FALLBACK**

## Question

Can `gamescope_stream` capture gamescope paint **without** private portal ScreenCast, by connecting to gamescope’s exported PipeWire node (Sunshine discussion #1007)?

## Evidence (lea)

```text
pw-dump → Video/Source
  id=283 serial=35059
  media.class=Video/Source
  node.name=.gamescope-wrapped
  media.name=gamescope
  default.video.source → .gamescope-wrapped

WAYLAND: gamescope-0 present
polaris-hdr-idle: active
```

Gamescope **does** export a session-graph `Video/Source` while idle units run. No portal required to *see* the node.

## Prototype in tree

- `pipewire_capture::find_gamescope_video_source()` — registry walk + `POLARIS_GAMESCOPE_PW_NODE` override  
- `capture_t::start()` accepts `remote_fd=-1` (default PW core) + serial-then-node connect (Sunshine style)  
- `ensure_global_capture`: when `private_runtime=gamescope` / `gamescope_stream`, try local node first; on failure fall back to private portal  

## Risks / residual

| Risk | Mitigation |
|------|------------|
| Node missing until gamescope paints | Portal fallback |
| Multi-instance / wrong node | Prefer `media.name=gamescope` Video/Source; env override |
| HDR modifiers on local graph | Same dmabuf offer path as portal |
| Permissions | Session graph already exports to user PW (observed) |

## Decision

**GO-WITH-PORTAL-FALLBACK** — prefer gamescopegrab when the node exists; keep private portal as backup and for non-gamescope paths. **Do not** remove idle units or private portal packages until multimode gate proves local-only path cold-start.

## Follow-up

- Multimode gate with journal line `gamescopegrab local Video/Source`  
- Optional: drop private ScreenCast when local node stable for N sessions  
