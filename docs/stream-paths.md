# Linux stream paths (plugin contract)

Polaris models each user-facing Linux streaming option as a **stream path**: a stable id plus three orthogonal concerns.

| Concern | Meaning | Examples |
|---------|---------|----------|
| **Runtime** | Who owns app paint | `labwc`, `gamescope`, none (host) |
| **Capture** | How frames are taken | `wlroots`, `portal`, `kms`, `evdi`, `auto` |
| **Topology** | Host display layout policy | `leave_alone`, `host_virtual`, `swap_primary` |

Config key: `linux_stream_mode = <path id>`. Legacy booleans (`headless_mode`, `linux_use_cage_compositor`, `linux_prefer_gpu_native_capture`) still map to/from primary paths.

## Built-in path ids

| Id | Runtime | Capture | Topology | Status |
|----|---------|---------|----------|--------|
| `headless_stream` | labwc | wlroots | leave_alone | Available (Private Stream) |
| `windowed_stream` | labwc | wlroots | leave_alone | Available (GPU-native preference) |
| `desktop_display` | none | portal | leave_alone | Available (Mirror Desktop / external gamescope) |
| `host_virtual_display` | none | auto | host_virtual | Available |
| `gamescope_stream` | gamescope | portal | leave_alone | **Reserved** — runtime ownership TBD |
| `family_isolated` | labwc | wlroots | leave_alone | **Reserved** — Family Mode / isolated per-app (PR #226) |
| `headless_evdi` | none | evdi | swap_primary | **Reserved** — EVDI-as-primary (PR #226) |
| `headless_dongle` | none | kms | swap_primary | **Reserved** — dummy-plug swap (PR #226) |

Source of truth: `src/platform/linux/stream_path.{h,cpp}` registry.

## Adding a new path (checklist)

1. **Register** a `stream_path::descriptor_t` in `stream_path::registry()` with a stable id.
2. **Runtime** (if the path needs a private compositor):
   - Implement `stream_runtime::stream_runtime_t` (see `stream_runtime_labwc.cpp`).
   - Extend `stream_runtime::acquire()` for the new `runtime_kind_e`.
3. **Capture** (if not covered by existing portal/kms/wlroots paths):
   - Add grab backend + wire via `capture_kind_e` negotiation in platform init — do not hard-code capture inside the path id switch in `process.cpp`.
4. **Topology** (if rearranging host outputs):
   - Implement prepare/restore hooks keyed by `topology_kind_e` (swap primary / host virtual), callable from session prep — not as ad-hoc booleans.
5. **Policy facade**: `stream_display_policy` maps path → legacy booleans for one release cycle.
6. **UI**: Audio/Video path cards read the same ids; mark `available: false` until the runtime works.
7. **Stats**: set `runtime_backend` + `stream_path_id` via `stream_stats::update_runtime_state` (or rely on policy `backend_name` when idle).
8. **Tests**: selection ↔ legacy round-trip; unavailable apply rejects; launch contract lists only available primary paths.

## What not to do

- Do not add a fourth boolean to encode a new mode.
- Do not special-case gamescope/EVDI only inside `cage_display_router`.
- Do not report `runtime_backend` empty — use `portal`, `host`, `gamescope`, `labwc`, etc.

## Relation to community PR #226

[Headless Streaming Display](https://github.com/papi-ux/polaris/pull/226) introduces EVDI grab, display swap, Family Mode isolation, and `headless_source` / `headless_swap_mode`. Those map cleanly onto:

- paths `headless_evdi`, `headless_dongle`, `family_isolated`
- topology `swap_primary` + capture `evdi`/`kms`
- optional per-app override (Family Mode) on top of the labwc runtime

Integrate by filling the reserved registry slots and implementing runtime/capture/topology hooks — not by inventing parallel config trees.

## Relation to gamescope (lea / polaris-hdr-linux-patches)

Today: **Mirror Desktop** + `capture=portal` + external gamescope session scripts.

Target: flip `gamescope_stream` to `available` once `stream_runtime_gamescope` owns start/wait/stop (and optional attach). Capture stays portal-oriented; WSI remains a presentation sub-option, not a top-level path.
