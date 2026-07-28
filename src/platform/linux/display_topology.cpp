/**
 * @file src/platform/linux/display_topology.cpp
 * @brief Host display topology for dongle swap + connector discovery.
 */

#include "display_topology.h"

#ifdef __linux__

  #include "src/config.h"
  #include "src/logging.h"
  #include "stream_display_policy.h"

  #include <cctype>
  #include <chrono>
  #include <cstdio>
  #include <filesystem>
  #include <fstream>
  #include <string>
  #include <thread>

using namespace std::literals;

namespace display_topology {
  namespace {

    namespace fs = std::filesystem;

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

    std::string read_sysfs_file(const fs::path &path) {
      std::ifstream in(path);
      if (!in) {
        return {};
      }
      std::string value;
      std::getline(in, value);
      while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
      }
      return value;
    }

    /**
     * @brief card1-HDMI-A-2 → HDMI-A-2 (kscreen-doctor / xrandr style).
     */
    std::string connector_name_from_drm(const std::string &drm_name) {
      // drm_name like "card0-DP-3" or "card1-HDMI-A-2"
      const auto dash = drm_name.find('-');
      if (dash == std::string::npos || dash + 1 >= drm_name.size()) {
        return drm_name;
      }
      return drm_name.substr(dash + 1);
    }

    bool name_looks_like_panel(const std::string &name) {
      // Internal laptop panels / eDP are almost never dongles.
      const auto lower = name;
      return lower.find("eDP") != std::string::npos ||
             lower.find("LVDS") != std::string::npos ||
             lower.find("DSI") != std::string::npos;
    }

  }  // namespace

  std::vector<output_info_t> list_outputs() {
    std::vector<output_info_t> outputs;
    std::error_code ec;
    const fs::path drm_root {"/sys/class/drm"};
    if (!fs::exists(drm_root, ec)) {
      return outputs;
    }

    for (const auto &entry : fs::directory_iterator(drm_root, ec)) {
      if (ec) {
        break;
      }
      const auto name = entry.path().filename().string();
      // Skip card nodes and writeback.
      if (name.find("card") != 0 || name.find('-') == std::string::npos) {
        continue;
      }
      if (name.find("Writeback") != std::string::npos) {
        continue;
      }
      const auto status_path = entry.path() / "status";
      if (!fs::exists(status_path)) {
        continue;
      }

      output_info_t info;
      info.drm_path = name;
      info.name = connector_name_from_drm(name);
      const auto status = read_sysfs_file(status_path);
      info.connected = (status == "connected");
      const auto enabled = read_sysfs_file(entry.path() / "enabled");
      info.enabled = (enabled == "enabled");
      // Dongle heuristic: connected external HDMI/DP that is not an internal panel.
      // Dummy plugs usually show as connected HDMI-A-* or DP-*.
      info.likely_dongle =
        info.connected &&
        !name_looks_like_panel(info.name) &&
        (info.name.find("HDMI") != std::string::npos ||
         info.name.find("DP-") != std::string::npos ||
         info.name.find("DisplayPort") != std::string::npos);
      outputs.push_back(std::move(info));
    }

    // Suggest streaming = first likely dongle (or first connected external).
    // Suggest primary = first enabled connected non-dongle, else first connected non-streaming.
    std::string suggested_stream;
    std::string suggested_primary;
    for (const auto &o : outputs) {
      if (o.likely_dongle && suggested_stream.empty()) {
        suggested_stream = o.name;
      }
    }
    if (suggested_stream.empty()) {
      for (const auto &o : outputs) {
        if (o.connected && !name_looks_like_panel(o.name)) {
          suggested_stream = o.name;
          break;
        }
      }
    }
    for (const auto &o : outputs) {
      if (o.connected && o.name != suggested_stream && suggested_primary.empty()) {
        // Prefer enabled panel-like first.
        if (o.enabled || name_looks_like_panel(o.name)) {
          suggested_primary = o.name;
        }
      }
    }
    if (suggested_primary.empty()) {
      for (const auto &o : outputs) {
        if (o.connected && o.name != suggested_stream) {
          suggested_primary = o.name;
          break;
        }
      }
    }

    for (auto &o : outputs) {
      o.suggested_streaming = (!suggested_stream.empty() && o.name == suggested_stream);
      o.suggested_primary = (!suggested_primary.empty() && o.name == suggested_primary);
    }

    return outputs;
  }

  bool ensure_dongle_outputs_configured() {
    auto &cfg = config::video.linux_display;
    if (!cfg.streaming_output.empty() && !cfg.primary_output.empty() &&
        cfg.streaming_output != cfg.primary_output) {
      return true;
    }

    const auto outputs = list_outputs();
    std::string stream;
    std::string primary;
    for (const auto &o : outputs) {
      if (o.suggested_streaming) {
        stream = o.name;
      }
      if (o.suggested_primary) {
        primary = o.name;
      }
    }

    if (cfg.streaming_output.empty() && !stream.empty()) {
      cfg.streaming_output = stream;
      BOOST_LOG(info) << "display_topology: auto-selected streaming_output=["sv << stream << "]"sv;
    }
    if (cfg.primary_output.empty() && !primary.empty()) {
      cfg.primary_output = primary;
      BOOST_LOG(info) << "display_topology: auto-selected primary_output=["sv << primary << "]"sv;
    }
    if (config::video.output_name.empty() && !cfg.streaming_output.empty()) {
      config::video.output_name = cfg.streaming_output;
    }

    return !cfg.streaming_output.empty() && !cfg.primary_output.empty() &&
           cfg.streaming_output != cfg.primary_output;
  }

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
    if (policy.use_cage_runtime ||
        (policy.use_private_runtime && policy.selection != "headless_dongle")) {
      // gamescope private runtime also leaves host topology alone.
      if (policy.selection == "gamescope_stream" || policy.selection == "headless_stream" ||
          policy.selection == "windowed_stream") {
        return false;
      }
    }
    if (cfg.stream_mode == "headless_dongle" ||
        cfg.stream_mode == "headless_evdi" ||
        policy.selection == "headless_dongle" ||
        policy.selection == "headless_evdi") {
      return true;
    }
    // Legacy auto_manage without stream_mode still runs swap helpers.
    return true;
  }

  bool output_present(const std::string &name) {
    if (name.empty()) {
      return false;
    }
    for (const auto &o : list_outputs()) {
      if (o.name == name) {
        return true;
      }
    }
    // Bounded kscreen probe (may hang on some hosts — keep timeout).
    const std::string out = exec_capture("timeout 2 kscreen-doctor -o 2>/dev/null");
    if (out.empty()) {
      return false;
    }
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
    if (config::video.linux_display.stream_mode == "headless_dongle" ||
        config::video.linux_display.stream_mode.empty()) {
      ensure_dongle_outputs_configured();
    }
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
                         << "] not found in sysfs; attempting kscreen enable anyway"sv;
    }

    // timeout guards against kscreen-doctor hangs observed on some hosts.
    std::string cmd = "timeout 8 kscreen-doctor output." + cfg.streaming_output + ".enable";
    if (privacy && distinct) {
      cmd += " output." + cfg.streaming_output + ".priority.1";
      cmd += " output." + cfg.primary_output + ".disable";
    }
    else if (distinct) {
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

    std::string cmd = "timeout 8 kscreen-doctor";
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
