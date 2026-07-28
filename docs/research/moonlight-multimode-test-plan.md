# Moonlight multi-mode test plan (lea)

Host: lea · Dongle: HDMI-A-2 @ 4K60 HDR · Desk: DP-2  
Mode switch (from lea shell):

```bash
~/projects/polaris/scripts/lea-mode-switch.sh gamescope_stream   # 1
~/projects/polaris/scripts/lea-mode-switch.sh headless_stream    # 2 labwc
~/projects/polaris/scripts/lea-mode-switch.sh headless_dongle    # 3 4K dongle
~/projects/polaris/scripts/lea-mode-switch.sh status
```

## Moonlight client settings (laptop)

| Setting | Value |
|---------|--------|
| Resolution | **3840×2160** |
| FPS | **60** |
| Video codec | **HEVC** (prefer) / AV1 if available |
| HDR | **On** (when mode supports it) |
| Bitrate | start **50–80 Mbps**, raise if clean |
| Video frame pacing / VSync | as you prefer |

### HDR reality per mode (lea)
- **gamescope_stream**: best chance for real HDR (portal + gamescope, `POLARIS_HDR_*=3840x2160@120` host idle; request 4K60 from client)
- **headless_dongle**: HDMI-A-2 is 4K60 HDR-capable; stream may report HDR when display metadata is honest after privacy swap
- **labwc (headless_stream)**: usually **SDR** honest; still test 4K60 if client requests it (may downscale/encode SDR)

## Per-mode checklist (do all 3 modes)

For each mode, switch on host first, wait ~10s, then on Moonlight:

### A. Clean launch
1. Pair if needed; select lea
2. Launch **Steam Big Picture** (or a library game)
3. Confirm picture: resolution / HDR badge / no black screen
4. Note: audio ok?

### B. End session (quit game) — soft end
1. In-game or BP: **Exit / Quit to desktop** or Moonlight **End stream** / disconnect menu
2. Expect: host stays up, no “connection reset” spam, next connect works cold
3. Desk layout: DP-2 still usable (dongle mode should restore primary after stop)

### C. Force-quit session
1. Start stream again
2. **Force quit** Moonlight (kill app / force stop on OS) mid-stream  
   **or** host: WebUI **Disconnect** while stream is live
3. Expect: polaris stays active; next Moonlight connect works without reboot

### D. End game only (app quit, client stays?)
1. Start a **library game** (not only Big Picture)
2. Quit the game from inside (to BP or desktop inside stream)
3. Then End stream from Moonlight

## Pass/fail notes template

```
mode=gamescope_stream
  launch: ok/fail  res=?  hdr=?  notes=
  end stream: ok/fail  RST?=  reconnect=
  force quit: ok/fail  polaris alive?=  reconnect=
  end game: ok/fail

mode=headless_stream (labwc)
  ...

mode=headless_dongle
  ...
  privacy swap (desk blank / dongle shows)? ok/fail
  restore after stop: ok/fail
```

## Host help while you test

```bash
# live logs
journalctl --user -u polaris.service -f

# look for HDR / path
journalctl --user -u polaris.service -n 80 --no-pager | rg -i 'stream_hdr|HDR decision|path=|display_topology|session_runtime'

# if polaris died
systemctl --user status polaris.service --no-pager
```

After all tests, leave daily mode:
```bash
~/projects/polaris/scripts/lea-mode-switch.sh gamescope_stream
```
