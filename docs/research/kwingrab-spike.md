# Spike W4: kwingrab (KDE direct ScreenCast)

**Date:** 2026-07-28  
**Host:** lea (KWin Wayland)  
**Decision:** **GO** (feasibility) — **implementation deferred** behind portal for dongle/desktop until W1/W5 gamescope path settles

## Question

Can host/dongle paths capture KWin without xdg-desktop-portal picker, like Sunshine `kwingrab.cpp` (`zkde_screencast_unstable_v1` → local PW node)?

## Evidence

- `pw-dump` shows `.kwin_wayland-wrapped` on session bus  
- Sunshine ships ~772 LOC `kwingrab.cpp` reusing shared `pipewire.cpp` base  
- lea dongle path already works via **host portal** + topology (user/product residual: restore token bootstrap)

## Recommendation

| Path | Capture today | kwingrab value |
|------|---------------|----------------|
| `desktop_display` | host portal | High — no picker after first grant if we keep restore token; kwingrab avoids portal entirely on KDE |
| `headless_dongle` | host portal after topology | Medium — portal works; kwingrab is polish |
| `gamescope_stream` | portal or gamescopegrab | **N/A** — use gamescopegrab |

## Decision

**GO** for KDE hosts as a **future capture backend** (register `capture=kwin` or auto on KDE). **Not blocking** this rewrite wave: implement after gamescopegrab multimode green.

**Do not** block W5/W6/W7 on kwingrab code port.

## Implementation sketch (later)

1. Port Sunshine `kwingrab` against `pipewire_capture` (not include `.cpp`)  
2. `misc.cpp` source enum: try kwin before portal on Wayland+KWin  
3. Gate: desktop_display stop green without portal unit  
