/**
 * @file src/platform/linux/display_topology.h
 * @brief Host display topology prepare/restore for stream paths (dongle swap, etc.).
 *
 * Used by headless_dongle (and future headless_evdi) paths. Private labwc/gamescope
 * runtimes must not call these — they own an isolated surface and leave the host layout alone.
 */
#pragma once

#ifdef __linux__

#include <string>
#include <string_view>

namespace display_topology {

  /**
   * @brief Whether privacy swap mode makes the streaming output primary and blanks primary.
   */
  bool swap_makes_headless_primary(std::string_view swap_mode);

  /**
   * @brief True when the configured path should run kscreen-doctor display swap.
   *
   * Requires auto_manage + streaming_output, and must not run for private labwc sessions.
   */
  bool should_manage_host_topology();

  /**
   * @brief Enable streaming output / optional privacy swap before capture.
   */
  void prepare_for_stream();

  /**
   * @brief Restore primary display layout after the stream ends.
   */
  void restore_after_stream();

  /**
   * @brief Whether a kscreen-doctor-visible output with this name exists.
   */
  bool output_present(const std::string &name);

}  // namespace display_topology

#endif
