#include <gtest/gtest.h>
#include "integrations/discord/DiscordClient.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// Length limits
// ═══════════════════════════════════════════════════════════════════════════
#include "utils/text/Utf8.hpp"

TEST(Utf8Truncate, ShortStringsUnchanged) {
    EXPECT_EQ(utf8_truncate("hello", 10), "hello");
    EXPECT_EQ(utf8_truncate("h\xC3\xA9llo", 5), "h\xC3\xA9llo"); // 5 code points
}

TEST(Utf8Truncate, NeverSplitsMultibyteAndFitsLimit) {
    std::string s;
    for (int i = 0; i < 50; ++i) s += "\xF0\x9F\xA7\xB1"; // 50 x 4-byte emoji
    auto t = utf8_truncate(s, 10);
    // 9 emoji + ellipsis = 10 code points, valid UTF-8 (whole sequences only)
    EXPECT_EQ(t.size(), 9 * 4 + 3u);
    EXPECT_EQ(t.substr(t.size() - 3), "\xE2\x80\xA6");
}
