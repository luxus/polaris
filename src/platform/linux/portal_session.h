/**
 * @file src/platform/linux/portal_session.h
 * @brief Public XDG Desktop Portal ScreenCast / PipeWire capture surface.
 *
 * Implementation lives in portal_grab.cpp. Callers must not re-declare these
 * symbols — include this header.
 *
 * Teardown contract (session_media owns the ordered stop path):
 *   1. signal capture shutdown
 *   2. portal::release_global_capture()  — non-blocking after short budget
 *   3. bounded join of capture threads
 *   4. only then kill gamescope/labwc
 */
#pragma once

#ifdef __linux__

namespace portal {
  /**
   * @brief Drop the global ScreenCast session and PipeWire capture.
   *
   * Safe to call when no session exists. Never blocks past a short destroy
   * budget; remaining work may finish on a background thread. Prefer going
   * through session_media::prepare_for_stop() so release is not double-run.
   */
  void release_global_capture();
}  // namespace portal

#endif  // __linux__
