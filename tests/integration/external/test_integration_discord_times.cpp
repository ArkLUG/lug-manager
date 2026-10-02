// Repairing Discord copies of times sent by images without time-zone data
// (services/DiscordTimeRepair.hpp), against the local fake Discord API.
// The repair must only read, then edit in place: no new posts, no DMs.
#include "integration_test_base.hpp"
#include "fake_discord.hpp"

namespace {
const std::string LUG_CH = "600000000000000001", FORUM_CH = "600000000000000002", CH_CH = "600000000000000003";

std::string sql_text(SqliteDatabase& db, const std::string& sql) {
    auto st = db.prepare(sql);
    return st.step() ? st.col_text(0) : "";
}
void exec(SqliteDatabase& db, const std::string& sql) {
    auto st = db.prepare(sql);
    st.step();
}
// "YYYY-MM-DDTHH:MM:00" for today + days, local wall-clock time
std::string local_at(SqliteDatabase& db, int days, const std::string& hhmm) {
    return sql_text(db, "SELECT date('now','" + std::string(days < 0 ? "" : "+") + std::to_string(days) + " days')") + "T" + hhmm + ":00";
}
// What an image without zoneinfo sent: the local wall-clock time labelled UTC
std::string as_if_utc(const std::string& local) { return local.substr(0, 19) + "+00:00"; }
}

class DiscordTimesTest : public IntegrationTest {
protected:
    std::unique_ptr<FakeDiscord> fake;
    void SetUp() override {
        IntegrationTest::SetUp();
        fake = std::make_unique<FakeDiscord>();
        discord_client->reconfigure(fake->guild_id, LUG_CH, FORUM_CH, "", "", "America/Chicago");
    }
    void TearDown() override {
        fake.reset();
        IntegrationTest::TearDown();
    }
    int64_t add_meeting(const std::string& title, const std::string& start, const std::string& end, const std::string& se,
                        const std::string& lug_msg = "", const std::string& ch_msg = "") {
        auto st = db->prepare("INSERT INTO meetings (title, start_time, end_time, ical_uid, discord_event_id, discord_lug_message_id, "
                              "discord_chapter_message_id, chapter_id, scope) VALUES (?,?,?,?,?,?,?,?, 'chapter')");
        st.bind(1, title); st.bind(2, start); st.bind(3, end); st.bind(4, "dt-" + title);
        st.bind(5, se); st.bind(6, lug_msg); st.bind(7, ch_msg); st.bind(8, test_chapter_id);
        st.step();
        return db->last_insert_rowid();
    }
    int64_t add_event(const std::string& title, const std::string& start, const std::string& end, const std::string& se,
                      const std::string& status = "confirmed", int suppress = 0) {
        auto st = db->prepare("INSERT INTO lug_events (title, start_time, end_time, status, ical_uid, discord_event_id, suppress_discord) "
                              "VALUES (?,?,?,?,?,?,?)");
        st.bind(1, title); st.bind(2, start); st.bind(3, end); st.bind(4, status); st.bind(5, "dt-" + title);
        st.bind(6, se); st.bind(7, static_cast<int64_t>(suppress));
        st.step();
        return db->last_insert_rowid();
    }
    void discord_event(const std::string& id, const std::string& start, const std::string& end, int status = 1) {
        fake->scheduled_events[id] = {{"id", id}, {"status", status}, {"scheduled_start_time", start}, {"scheduled_end_time", end}};
    }
    void discord_message(const std::string& channel, const std::string& id, const std::string& content) {
        fake->messages[channel + "/" + id] = {{"id", id}, {"channel_id", channel}, {"content", content}};
    }
};

TEST_F(DiscordTimesTest, ChecksThenFixesSilently) {
    exec(*db, "UPDATE chapters SET discord_announcement_channel_id='" + CH_CH + "' WHERE id=" + std::to_string(test_chapter_id));
    // An upcoming show, sent as if 10 AM were UTC
    std::string s1 = local_at(*db, 30, "10:00"), e1 = local_at(*db, 30, "16:00");
    add_event("Brick Fest", s1, e1, "se1");
    discord_event("se1", as_if_utc(s1), as_if_utc(e1));
    // An upcoming meeting: wrong scheduled event, "America" in both announcements
    std::string s2 = local_at(*db, 10, "19:00"), e2 = local_at(*db, 10, "21:00");
    add_meeting("October meeting", s2, e2, "se2", "msg2", "chmsg2");
    discord_event("se2", as_if_utc(s2), as_if_utc(e2));
    std::string day = s2.substr(5, 2) + "/" + s2.substr(8, 2);
    std::string bad = "<@&555> \n**October meeting**\n📅 " + day + " 7:00 PM America → " + day + " 9:00 PM America\n📍 Library";
    discord_message(LUG_CH, "msg2", bad);
    discord_message(CH_CH, "chmsg2", bad);
    // Already right
    std::string s3 = local_at(*db, 12, "19:00"), e3 = local_at(*db, 12, "21:00");
    add_meeting("Build night", s3, e3, "se3", "msg3");
    discord_event("se3", discord_client->utc_iso(s3), discord_client->utc_iso(e3));
    discord_message(LUG_CH, "msg3", "**Build night** 7:00 PM " + DiscordClient::tz_abbrev(s3, "America/Chicago"));
    // Past: can't move the scheduled event; its announcement only with include_past
    std::string s4 = local_at(*db, -10, "19:00"), e4 = local_at(*db, -10, "21:00");
    add_meeting("Old meeting", s4, e4, "se4", "msg4");
    discord_event("se4", as_if_utc(s4), as_if_utc(e4));
    discord_message(LUG_CH, "msg4", "**Old meeting** 7:00 PM America");
    // Deleted on Discord; cancelled and Discord-suppressed ones aren't looked at
    std::string s6 = local_at(*db, 20, "19:00");
    add_meeting("Gone from Discord", s6, local_at(*db, 20, "21:00"), "se6");
    add_event("Cancelled show", s1, e1, "se7", "cancelled");
    add_event("Private show", s1, e1, "se8", "confirmed", 1);

    // Admins only
    EXPECT_EQ(GET("/settings/discord-times", member_token).code, 403);
    EXPECT_EQ(POST("/settings/discord-times/fix", "", member_token).code, 403);
    expect_contains(GET("/settings/discord-times", admin_token), "Repair times on Discord");

    // Check: reads only
    fake->clear();
    auto check = POST("/settings/discord-times/check", "", admin_token);
    EXPECT_EQ(check.code, 200);
    expect_contains(check, "4 wrong");   // 2 scheduled events + 2 announcements
    expect_contains(check, "Discord has it 5 hours early");   // October: CDT, UTC-5
    expect_contains(check, "Fix 4 on Discord (silently)");
    expect_not_contains(check, "Cancelled show");
    expect_not_contains(check, "Private show");
    EXPECT_TRUE(fake->matching("^(POST|PATCH|PUT|DELETE) ").empty());
    EXPECT_TRUE(fake->matching("se7|se8").empty());
    EXPECT_TRUE(fake->matching("se4").empty());           // past: not even read
    EXPECT_TRUE(fake->matching("msg4").empty());

    // API (admin key) gives the same answer as JSON; other scopes can't
    std::string key = make_api_key("admin"), read_key = make_api_key("read", "ro");
    EXPECT_EQ(API_GET("/api/v1/maintenance/discord-times", read_key).code, 403);
    EXPECT_EQ(API_GET("/api/v1/maintenance/discord-times", "").code, 401);
    auto j = crow::json::load(API_GET("/api/v1/maintenance/discord-times", key).body);
    ASSERT_TRUE(j);
    EXPECT_EQ(j["counts"]["wrong"].i(), 4);
    EXPECT_EQ(j["counts"]["ok"].i(), 2);
    EXPECT_EQ(j["counts"]["past"].i(), 1);
    EXPECT_EQ(j["counts"]["missing"].i(), 1);
    EXPECT_FALSE(j["applied"].b());
    EXPECT_TRUE(fake->matching("^(POST|PATCH|PUT|DELETE) ").empty());

    // Fix: edits in place only
    fake->clear();
    auto fix = POST("/settings/discord-times/fix", "", admin_token);
    EXPECT_EQ(fix.code, 200);
    expect_contains(fix, "Done: 4 fixed");
    EXPECT_TRUE(fake->matching("^(POST|PUT|DELETE) ").empty());            // nothing new posted, no DMs
    auto patches = fake->matching("^PATCH ");
    ASSERT_EQ(patches.size(), 4u);   // se1, se2, msg2, chmsg2
    for (const auto& p : patches) {
        auto b = crow::json::load(p.body);
        ASSERT_TRUE(b);
        if (p.path.find("/scheduled-events/") != std::string::npos) {
            EXPECT_EQ(b.size(), 2u) << p.body;                               // only the two times
            EXPECT_TRUE(b.has("scheduled_start_time") && b.has("scheduled_end_time"));
        } else {
            EXPECT_EQ(b["allowed_mentions"]["parse"].size(), 0u) << p.body;  // pings nobody
        }
    }
    EXPECT_EQ(fake->scheduled_events["se1"]["scheduled_start_time"], discord_client->utc_iso(s1));
    EXPECT_EQ(fake->scheduled_events["se2"]["scheduled_end_time"], discord_client->utc_iso(e2));
    // The times become Discord timestamps (each reader's own time zone); nothing else changes.
    std::string good = "<@&555> \n**October meeting**\n📅 <t:" + std::to_string(DiscordClient::local_to_epoch(s2, "America/Chicago")) +
                       ":F> → <t:" + std::to_string(DiscordClient::local_to_epoch(e2, "America/Chicago")) + ":t>\n📍 Library";
    EXPECT_EQ(fake->messages[LUG_CH + "/msg2"]["content"], good);
    EXPECT_EQ(fake->messages[CH_CH + "/chmsg2"]["content"], good);
    EXPECT_EQ(fake->messages[LUG_CH + "/msg4"]["content"], "**Old meeting** 7:00 PM America");
    EXPECT_EQ(sql_text(*db, "SELECT details FROM audit_log WHERE action='discord.repair_times'"), "4 fixed, 2 ok, 1 past, 1 missing");

    // Running it again changes nothing
    fake->clear();
    expect_contains(POST("/settings/discord-times/fix", "", admin_token), "Done: 6 ok, 1 past, 1 missing");
    EXPECT_TRUE(fake->matching("^(POST|PATCH|PUT|DELETE) ").empty());

    // Past announcements on request (text only; the past scheduled event stays put)
    auto past = API_POST("/api/v1/maintenance/discord-times?include_past=1", "", key);
    auto pj = crow::json::load(past.body);
    ASSERT_TRUE(pj);
    EXPECT_EQ(pj["counts"]["fixed"].i(), 1);
    EXPECT_EQ(fake->messages[LUG_CH + "/msg4"]["content"], "**Old meeting** 7:00 PM " + DiscordClient::tz_abbrev(s4, "America/Chicago"));
    EXPECT_EQ(fake->scheduled_events["se4"]["scheduled_start_time"], as_if_utc(s4));
    EXPECT_TRUE(fake->matching("^(POST|PUT|DELETE) /api/v10/(channels|guilds)/[^ ]*(messages|scheduled-events)$").empty());
}

TEST_F(DiscordTimesTest, StartedEventsAndRefusalsAreReported) {
    std::string s = local_at(*db, 5, "18:00"), e = local_at(*db, 5, "20:00");
    add_event("Live now on Discord", s, e, "sa");
    discord_event("sa", as_if_utc(s), as_if_utc(e), /*status: active*/ 2);
    std::string s2 = local_at(*db, 6, "18:00"), e2 = local_at(*db, 6, "20:00");
    add_event("Will be refused", s2, e2, "sr");
    discord_event("sr", as_if_utc(s2), as_if_utc(e2));

    auto check = POST("/settings/discord-times/check", "", admin_token);
    expect_contains(check, "Discord shows it as started or finished");
    expect_contains(check, "1 wrong");
    fake->refuse_edits = true;
    auto fix = POST("/settings/discord-times/fix", "", admin_token);
    expect_contains(fix, "1 error");
    expect_contains(fix, "Discord refused the change");
    expect_not_contains(fix, "fixed");
    EXPECT_EQ(fake->scheduled_events["sr"]["scheduled_start_time"], as_if_utc(s2));
    EXPECT_EQ(fake->scheduled_events["sa"]["scheduled_start_time"], as_if_utc(s));   // started: left alone

}
