/**
 * @file tests/unit/platform/test_portal_grab_policy.cpp
 * @brief Test XDG Desktop Portal source selection policy.
 */

#include "../../tests_common.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace portal {
  std::uint32_t capture_type_for_stream_display_for_tests(bool headless_mode, bool use_cage_compositor);
  std::uint32_t portal_pick_cursor_mode_for_tests(std::uint32_t available);
}

TEST(PortalGrabPolicyTests, DesktopDisplayRequestsMonitorSource) {
  EXPECT_EQ(portal::capture_type_for_stream_display_for_tests(false, false), 1u);
}

TEST(PortalGrabPolicyTests, PrivateAndWindowedCagePathsRequestWindowSource) {
  EXPECT_EQ(portal::capture_type_for_stream_display_for_tests(true, true), 2u);
  EXPECT_EQ(portal::capture_type_for_stream_display_for_tests(false, true), 2u);
}

// XDG ScreenCast AvailableCursorModes bits: 1=Hidden, 2=Embedded, 4=Metadata.
TEST(PortalGrabPolicyTests, CursorModePrefersEmbeddedThenMetadataThenHidden) {
  EXPECT_EQ(portal::portal_pick_cursor_mode_for_tests(0), 0u);
  EXPECT_EQ(portal::portal_pick_cursor_mode_for_tests(1), 1u);  // Hidden only
  EXPECT_EQ(portal::portal_pick_cursor_mode_for_tests(2), 2u);  // Embedded
  EXPECT_EQ(portal::portal_pick_cursor_mode_for_tests(4), 4u);  // Metadata
  EXPECT_EQ(portal::portal_pick_cursor_mode_for_tests(7), 2u);  // all → Embedded
  EXPECT_EQ(portal::portal_pick_cursor_mode_for_tests(5), 4u);  // Hidden|Metadata → Metadata
  EXPECT_EQ(portal::portal_pick_cursor_mode_for_tests(3), 2u);  // Hidden|Embedded → Embedded
}

TEST(PortalGrabPolicyTests, SelectSourcesInvalidatesRestoreTokenOnFailure) {
  // Source-level contract: failed SelectSources must clear portal_restore_token
  // and retry once without restore_token (never permanently disable tokens).
  const auto path = std::filesystem::path(POLARIS_SOURCE_DIR) / "src/platform/linux/portal_grab.cpp";
  std::ifstream in(path);
  ASSERT_TRUE(in.good());
  std::ostringstream out;
  out << in.rdbuf();
  const auto body = out.str();
  EXPECT_NE(body.find("clear_restore_token()"), std::string::npos);
  EXPECT_NE(body.find("retry once without restore_token"), std::string::npos);
  EXPECT_NE(body.find("save_restore_token("), std::string::npos);
  EXPECT_NE(body.find("portal_wait_cursor_modes("), std::string::npos);
  // Do not permanently disable restore tokens as a "fix".
  EXPECT_EQ(body.find("restore_token_disabled"), std::string::npos);
}
