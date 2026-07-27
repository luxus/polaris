/**
 * @file src/platform/linux/display_topology.cpp
 * @brief kscreen-doctor based host display topology for dongle/headless swap paths.
 */

#include "display_topology.h"

#ifdef __linux__

  #include "src/config.h"
  #include "src/logging.h"
  #include "stream_display_policy.h"

  #include <cctype>
  #include <chrono>
  #include <cstdio>
  #include <string>
  #include <thread>

using namespace std::literals;

namespace display_topology {
  namespace {

    std::string exec_capture(const std::string &cmd) {
      FILE *pipe = popen(cmd.c_str(), "r");
      if (!pipe) {
        return {};
      }
      char buf[512];
      std::string result;
      while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
      }
      pclose(pipe);
      return result;
    }

  }  // namespace

  bool swap_makes_headless_primary(std::string_view swap_mode) {
    return swap_mode.empty() || swap_mode == "privacy";
  }

  bool should_manage_host_topology() {
    const auto &cfg = config::video.linux_display;
    if (!cfg.auto_manage_displays || cfg.streaming_output.empty()) {
      return false;
    }
    // Private labwc must never dim/rearrange the host desktop.
    if (cfg.use_cage_compositor) {
      return false;
    }
    const auto policy = stream_display_policy::resolve_current();
    if (policy.use_cage_runtime || policy.use_private_runtime) {
      return false;
    }
    // Dongle / EVDI-style paths (swap topology or explicit headless_dongle mode).
    if (cfg.stream_mode == "headless_dongle" ||
        cfg.stream_mode == "headless_evdi" ||
        policy.selection == "headless_dongle" ||
        policy.selection == "headless_evdi") {
      return true;
    }
    // Legacy: auto_manage alone still runs extended (secondary) enable for compatibility.
    return true;
  }

  bool output_present(const std::string &name) {
    if (name.empty()) {
      return false;
    }
    const std::string out = exec_capture("kscreen-doctor -o 2>/dev/null");
    auto is_tok = [](unsigned char c) {
      return std::isalnum(c) || c == '-' || c == '_';
    };
    for (size_t pos = 0; (pos = out.find(name, pos)) != std::string::npos; pos += name.size()) {
      const char before = pos > 0 ? out[pos - 1] : ' ';
      const size_t after_idx = pos + name.size();
      const char after = after_idx < out.size() ? out[after_idx] : ' ';
      if (!is_tok(static_cast<unsigned char>(before)) && !is_tok(static_cast<unsigned char>(after))) {
        return true;
      }
    }
    return false;
  }

  void prepare_for_stream() {
    if (!should_manage_host_topology()) {
      return;
    }
    const auto &cfg = config::video.linux_display;
    const bool distinct = !cfg.primary_output.empty() && cfg.streaming_output != cfg.primary_output;
    const bool privacy = swap_makes_headless_primary(cfg.headless_swap_mode);

    if (privacy && !distinct && !cfg.primary_output.empty()) {
      BOOST_LOG(warning) << "display_topology: privacy swap needs distinct streaming_output and primary_output; enabling streaming output only"sv;
    }

    if (!output_present(cfg.streaming_output)) {
      BOOST_LOG(warning) << "display_topology: streaming output ["sv << cfg.streaming_output
                         << "] not listed by kscreen-doctor yet; attempting enable anyway"sv;
    }

    std::string cmd = "kscreen-doctor output." + cfg.streaming_output + ".enable";
    if (privacy && distinct) {
      // Headless dongle as primary; blank physical panel for the session.
      cmd += " output." + cfg.streaming_output + ".priority.1";
      cmd += " output." + cfg.primary_output + ".disable";
    }
    else if (distinct) {
      // Extended: keep physical primary, dongle as secondary.
      cmd += " output." + cfg.primary_output + ".priority.1";
      cmd += " output." + cfg.streaming_output + ".priority.2";
    }

    BOOST_LOG(info) << "display_topology: prepare ["sv << cmd << "]"sv;
    const int ret = std::system(cmd.c_str());
    if (ret != 0) {
      BOOST_LOG(error) << "display_topology: prepare failed code="sv << ret;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  }

  void restore_after_stream() {
    if (!should_manage_host_topology()) {
      return;
    }
    const auto &cfg = config::video.linux_display;
    const bool distinct = !cfg.primary_output.empty() && cfg.streaming_output != cfg.primary_output;
    const bool privacy = swap_makes_headless_primary(cfg.headless_swap_mode);

    if (privacy && !distinct && !cfg.primary_output.empty()) {
      return;
    }

    std::string cmd = "kscreen-doctor";
    if (privacy && distinct) {
      cmd += " output." + cfg.primary_output + ".enable";
      cmd += " output." + cfg.primary_output + ".priority.1";
      cmd += " output." + cfg.streaming_output + ".disable";
    }
    else {
      if (distinct) {
        cmd += " output." + cfg.primary_output + ".priority.1";
      }
      cmd += " output." + cfg.streaming_output + ".priority.2";
      cmd += " output." + cfg.streaming_output + ".disable";
    }

    BOOST_LOG(info) << "display_topology: restore ["sv << cmd << "]"sv;
    const int ret = std::system(cmd.c_str());
    if (ret != 0) {
      BOOST_LOG(error) << "display_topology: restore failed code="sv << ret;
    }
  }

}  // namespace display_topology

#endif
