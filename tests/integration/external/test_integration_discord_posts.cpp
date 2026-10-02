// What LUG Manager posts to Discord for events and meetings, against the
// local fake Discord API: create, edit, move between channels, take down and
// delete. Written before the chat-provider refactor so it keeps behaving the
// same with the default message templates.
#include "integration_test_base.hpp"
#include "fake_discord.hpp"
#include "repositories/members/ChapterRepository.hpp"

namespace {
const std::string LUG_CH = "600000000000000001", FORUM_CH = "600000000000000002", CH_CH = "600000000000000003";
const std::string ANN_ROLE = "500000000000000010", NONLUG_ROLE = "500000000000000011", CH_ROLE = "500000000000000012";
}

class DiscordPostsTest : public IntegrationTest {
protected:
    std::unique_ptr<FakeDiscord> fake;
    void SetUp() override {
        IntegrationTest::SetUp();
        fake = std::make_unique<FakeDiscord>();
        discord_client->reconfigure(fake->guild_id, LUG_CH, FORUM_CH, ANN_ROLE, NONLUG_ROLE, "America/Chicago");
        auto st = db->prepare("UPDATE chapters SET discord_announcement_channel_id=?, discord_member_role_id=? WHERE id=?");
        st.bind(1, CH_CH); st.bind(2, CH_ROLE); st.bind(3, test_chapter_id);
        st.step();
    }
    void TearDown() override {
        settle();
        fake.reset();
        IntegrationTest::TearDown();
    }
    // Background Discord calls (edits, deletes) run on a worker pool: wait until quiet.
    void settle() {
        size_t n = fake->requests().size();
        for (int i = 0; i < 40; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            size_t m = fake->requests().size();
            if (m == n && i > 2) return;
            n = m;
        }
    }
    void exec(const std::string& sql) { auto st = db->prepare(sql); st.step(); }
    std::vector<FakeServer::Request> sent(const std::string& re) { settle(); return fake->matching(re); }
    std::string body_of(const std::string& re) {
        auto r = sent(re);
        return r.empty() ? "" : r.back().body;
    }
    LugEvent new_event(const std::string& scope = "lug_wide") {
        LugEvent e;
        e.title = "Brick Fest"; e.start_time = "2099-10-14T09:00:00"; e.end_time = "2099-10-15T17:00:00";
        e.location = "Expo Hall, 1 Main St, Little Rock, AR 72201"; e.scope = scope; e.status = "confirmed";
        e.description = "Two days of LEGO."; e.suppress_calendar = true;
        if (scope == "chapter") e.chapter_id = test_chapter_id;
        return e;
    }
    Meeting new_meeting(const std::string& scope = "lug_wide") {
        Meeting m;
        m.title = "October meeting"; m.start_time = "2099-10-14T19:00:00"; m.end_time = "2099-10-14T21:00:00";
        m.location = "Library"; m.scope = scope; m.description = "Bring a build."; m.suppress_calendar = true;
        if (scope == "chapter") m.chapter_id = test_chapter_id;
        return m;
    }
};

TEST_F(DiscordPostsTest, EventCreateEditAndDelete) {
    auto ev = event_svc->create(new_event());
    settle();
    auto fresh = *event_svc->get(ev.id);
    // A forum thread first, then the announcement linking to it, then the scheduled event
    auto threads = sent("^POST /api/v10/channels/" + FORUM_CH + "/threads$");
    ASSERT_EQ(threads.size(), 1u);
    auto t = nlohmann::json::parse(threads[0].body);
    EXPECT_EQ(t["name"], "Brick Fest | Little Rock, AR | 10/14/99-10/15/99");
    std::string starter = t["message"]["content"];
    EXPECT_NE(starter.find("**Brick Fest**"), std::string::npos);
    EXPECT_NE(starter.find("Dates: 10/14 - 10/15"), std::string::npos);
    EXPECT_NE(starter.find("Two days of LEGO."), std::string::npos);
    ASSERT_FALSE(fresh.discord_thread_id.empty());
    auto ann = nlohmann::json::parse(body_of("^POST /api/v10/channels/" + LUG_CH + "/messages$"));
    std::string a = ann["content"];
    EXPECT_NE(a.find("<@&" + ANN_ROLE + ">"), std::string::npos);
    EXPECT_NE(a.find("**Brick Fest**"), std::string::npos);
    EXPECT_NE(a.find("Dates: 10/14 - 10/15"), std::string::npos);
    EXPECT_NE(a.find("Location: Expo Hall"), std::string::npos);
    EXPECT_NE(a.find("Discussion Thread: https://discord.com/channels/" + fake->guild_id + "/" + fresh.discord_thread_id), std::string::npos);
    EXPECT_EQ(a.find("@everyone"), std::string::npos);
    auto se = nlohmann::json::parse(body_of("^POST /api/v10/guilds/" + fake->guild_id + "/scheduled-events$"));
    EXPECT_EQ(se["name"], "Brick Fest");
    EXPECT_EQ(se["scheduled_start_time"], "2099-10-14T14:00:00Z");     // 9 AM CDT
    EXPECT_EQ(se["entity_type"], 3);
    EXPECT_FALSE(fresh.discord_event_id.empty());
    EXPECT_FALSE(fresh.discord_lug_message_id.empty());
    EXPECT_TRUE(fresh.discord_chapter_message_id.empty());

    // Edit: scheduled event, thread name, starter, announcement edited in place + an "updated" note
    fake->clear();
    LugEvent upd = fresh;
    upd.title = "Brick Fest 2099";
    event_svc->update(ev.id, upd, true, true);
    EXPECT_EQ(sent("^PATCH /api/v10/guilds/[0-9]+/scheduled-events/" + fresh.discord_event_id + "$").size(), 1u);
    EXPECT_NE(body_of("^PATCH /api/v10/channels/" + fresh.discord_thread_id + "$").find("Brick Fest 2099 | Little Rock"), std::string::npos);
    EXPECT_NE(body_of("^PATCH /api/v10/channels/" + LUG_CH + "/messages/" + fresh.discord_lug_message_id + "$").find("**Brick Fest 2099**"),
              std::string::npos);
    EXPECT_NE(body_of("^POST /api/v10/channels/" + fresh.discord_thread_id + "/messages$").find("**Event Updated** — Brick Fest 2099 has been updated."),
              std::string::npos);
    EXPECT_TRUE(sent("^POST /api/v10/channels/" + LUG_CH).empty());          // nothing new in the channel

    // Bulk re-sync (notify=false) and the "no update notes" setting: no note
    fake->clear();
    event_svc->update(ev.id, upd, true, false);
    EXPECT_TRUE(sent("^POST ").empty());
    discord_client->set_suppress_updates(true);
    event_svc->update(ev.id, upd, true, true);
    EXPECT_TRUE(sent("^POST ").empty());
    discord_client->set_suppress_updates(false);

    // Moved to a chapter: LUG announcement deleted, chapter announcement posted with the chapter role
    fake->clear();
    upd.scope = "chapter"; upd.chapter_id = test_chapter_id;
    event_svc->update(ev.id, upd, true, false);
    EXPECT_EQ(sent("^DELETE /api/v10/channels/" + LUG_CH + "/messages/" + fresh.discord_lug_message_id + "$").size(), 1u);
    EXPECT_NE(body_of("^POST /api/v10/channels/" + CH_CH + "/messages$").find("<@&" + CH_ROLE + ">"), std::string::npos);
    auto moved = *event_svc->get(ev.id);
    EXPECT_FALSE(moved.discord_chapter_message_id.empty());

    // Taken off Discord: everything the app posted is removed
    fake->clear();
    LugEvent off = moved;
    off.suppress_discord = true;
    event_svc->update(ev.id, off, true, false);
    EXPECT_EQ(sent("^DELETE /api/v10/guilds/[0-9]+/scheduled-events/" + moved.discord_event_id + "$").size(), 1u);
    EXPECT_EQ(sent("^DELETE /api/v10/channels/" + moved.discord_thread_id + "$").size(), 1u);
    EXPECT_EQ(sent("^DELETE /api/v10/channels/" + CH_CH + "/messages/" + moved.discord_chapter_message_id + "$").size(), 1u);
    auto gone = *event_svc->get(ev.id);
    EXPECT_TRUE(gone.discord_event_id.empty() && gone.discord_thread_id.empty() && gone.discord_chapter_message_id.empty());

    // Back on: published again
    fake->clear();
    LugEvent on = gone;
    on.suppress_discord = false;
    event_svc->update(ev.id, on, true, false);
    EXPECT_EQ(sent("^POST /api/v10/channels/" + FORUM_CH + "/threads$").size(), 1u);
    EXPECT_EQ(sent("^POST /api/v10/guilds/[0-9]+/scheduled-events$").size(), 1u);
    auto back = *event_svc->get(ev.id);

    // Deleted: removed from Discord too
    fake->clear();
    event_svc->cancel(ev.id);
    EXPECT_EQ(sent("^DELETE /api/v10/guilds/[0-9]+/scheduled-events/" + back.discord_event_id + "$").size(), 1u);
    EXPECT_EQ(sent("^DELETE /api/v10/channels/" + back.discord_thread_id + "$").size(), 1u);
}

TEST_F(DiscordPostsTest, EventVariants) {
    // Non-LUG events: their own role and a prefix
    auto nl = event_svc->create(new_event("non_lug"));
    auto c = nlohmann::json::parse(body_of("^POST /api/v10/channels/" + LUG_CH + "/messages$"));
    EXPECT_NE(c["content"].get<std::string>().find("<@&" + NONLUG_ROLE + ">"), std::string::npos);
    EXPECT_NE(c["content"].get<std::string>().find("[Non-LUG] **Brick Fest**"), std::string::npos);

    // A user-picked thread is used, not created, and never deleted
    fake->clear();
    LugEvent picked = new_event();
    picked.discord_thread_id = "700000000000000999";
    auto pe = event_svc->create(picked);
    EXPECT_TRUE(sent("^POST /api/v10/channels/" + FORUM_CH + "/threads$").empty());
    EXPECT_NE(body_of("^POST /api/v10/channels/" + LUG_CH + "/messages$").find("/700000000000000999"), std::string::npos);
    fake->clear();
    event_svc->cancel(pe.id);
    EXPECT_TRUE(sent("^DELETE /api/v10/channels/700000000000000999$").empty());

    // Pings off: no role mentions anywhere
    fake->clear();
    discord_client->set_suppress_pings(true);
    event_svc->create(new_event());
    for (const auto& r : sent("^POST "))
        EXPECT_EQ(r.body.find("<@&"), std::string::npos) << r.path;
    discord_client->set_suppress_pings(false);

    (void)nl;

    // Chapter events: LUG announcement plus the chapter's own
    fake->clear();
    auto ch = event_svc->create(new_event("chapter"));
    EXPECT_EQ(sent("^POST /api/v10/channels/" + LUG_CH + "/messages$").size(), 1u);
    EXPECT_NE(body_of("^POST /api/v10/channels/" + CH_CH + "/messages$").find("<@&" + CH_ROLE + ">"), std::string::npos);
    EXPECT_FALSE(event_svc->get(ch.id)->discord_chapter_message_id.empty());

    // "Don't post to Discord" from the start: nothing at all
    fake->clear();
    LugEvent quiet = new_event();
    quiet.suppress_discord = true;
    event_svc->create(quiet);
    EXPECT_TRUE(sent(".").empty());
}

TEST_F(DiscordPostsTest, MeetingCreateEditMoveAndDelete) {
    auto m = meeting_svc->create(new_meeting());
    auto mf = *meeting_svc->get(m.id);
    auto se = nlohmann::json::parse(body_of("^POST /api/v10/guilds/[0-9]+/scheduled-events$"));
    EXPECT_EQ(se["name"], "October meeting");
    EXPECT_EQ(se["scheduled_start_time"], "2099-10-15T00:00:00Z");    // 7 PM CDT
    std::string a = nlohmann::json::parse(body_of("^POST /api/v10/channels/" + LUG_CH + "/messages$"))["content"];
    EXPECT_NE(a.find("<@&" + ANN_ROLE + ">"), std::string::npos);
    EXPECT_NE(a.find("**October meeting**"), std::string::npos);
    // Discord timestamps: each reader sees their own time zone (7-9 PM CDT here).
    EXPECT_NE(a.find("When: <t:4095705600:F> – <t:4095712800:t>"), std::string::npos) << a;
    EXPECT_NE(a.find("Where: Library"), std::string::npos);
    EXPECT_NE(a.find("Bring a build."), std::string::npos);
    EXPECT_FALSE(mf.discord_event_id.empty());
    EXPECT_FALSE(mf.discord_lug_message_id.empty());

    // Same channel: edited in place
    fake->clear();
    Meeting upd = mf;
    upd.location = "Community Center";
    meeting_svc->update(m.id, upd);
    EXPECT_EQ(sent("^PATCH /api/v10/guilds/[0-9]+/scheduled-events/" + mf.discord_event_id + "$").size(), 1u);
    EXPECT_NE(body_of("^PATCH /api/v10/channels/" + LUG_CH + "/messages/" + mf.discord_lug_message_id + "$").find("Where: Community Center"),
              std::string::npos);
    EXPECT_TRUE(sent("^POST ").empty());

    // Moved to a chapter: deleted from the LUG channel, posted in the chapter's
    fake->clear();
    upd.scope = "chapter"; upd.chapter_id = test_chapter_id;
    meeting_svc->update(m.id, upd);
    EXPECT_EQ(sent("^DELETE /api/v10/channels/" + LUG_CH + "/messages/" + mf.discord_lug_message_id + "$").size(), 1u);
    EXPECT_NE(body_of("^POST /api/v10/channels/" + CH_CH + "/messages$").find("<@&" + CH_ROLE + ">"), std::string::npos);
    auto moved = *meeting_svc->get(m.id);
    EXPECT_TRUE(moved.discord_lug_message_id.empty());
    EXPECT_FALSE(moved.discord_chapter_message_id.empty());

    // Taken off Discord, then deleted
    fake->clear();
    Meeting off = moved;
    off.suppress_discord = true;
    meeting_svc->update(m.id, off);
    EXPECT_EQ(sent("^DELETE /api/v10/guilds/[0-9]+/scheduled-events/" + moved.discord_event_id + "$").size(), 1u);
    EXPECT_EQ(sent("^DELETE /api/v10/channels/" + CH_CH + "/messages/" + moved.discord_chapter_message_id + "$").size(), 1u);
    fake->clear();
    Meeting on = *meeting_svc->get(m.id);
    on.suppress_discord = false;
    meeting_svc->update(m.id, on);
    EXPECT_EQ(sent("^POST /api/v10/guilds/[0-9]+/scheduled-events$").size(), 1u);
    auto again = *meeting_svc->get(m.id);
    fake->clear();
    meeting_svc->cancel(m.id);
    EXPECT_EQ(sent("^DELETE /api/v10/guilds/[0-9]+/scheduled-events/" + again.discord_event_id + "$").size(), 1u);
    EXPECT_EQ(sent("^DELETE /api/v10/channels/" + CH_CH + "/messages/" + again.discord_chapter_message_id + "$").size(), 1u);

    // Non-LUG meetings ping the non-LUG role
    fake->clear();
    meeting_svc->create(new_meeting("non_lug"));
    EXPECT_NE(body_of("^POST /api/v10/channels/" + LUG_CH + "/messages$").find("<@&" + NONLUG_ROLE + ">"), std::string::npos);
}

TEST_F(DiscordPostsTest, ChallengeWinnerAndTestMessage) {
    // Test announcement from Settings > Discord
    EXPECT_EQ(POST("/api/discord/test-announcement", "channel_id=" + LUG_CH, admin_token).code, 200);
    EXPECT_NE(body_of("^POST /api/v10/channels/" + LUG_CH + "/messages$").find("LUG Manager test announcement"), std::string::npos);

    // Challenge winner, posted to the LUG channel
    fake->clear();
    exec("INSERT INTO challenges (title, starts_on, ends_on, created_by) "
         "VALUES ('Space Week', '2020-01-01', '2020-01-08', " + std::to_string(admin_member_id) + ")");
    int64_t cid = db->last_insert_rowid();
    exec("INSERT INTO challenge_entries (challenge_id, member_id, title, file) VALUES (" + std::to_string(cid) + "," +
         std::to_string(regular_member_id) + ",'Moon Base','x.png')");
    int64_t eid = db->last_insert_rowid();
    exec("INSERT INTO challenge_votes (challenge_id, entry_id, member_id) VALUES (" + std::to_string(cid) + "," +
         std::to_string(eid) + "," + std::to_string(admin_member_id) + ")");
    POST("/challenges/" + std::to_string(cid) + "/announce", "", admin_token);
    std::string w = nlohmann::json::parse(body_of("^POST /api/v10/channels/" + LUG_CH + "/messages$"))["content"];
    EXPECT_NE(w.find("**Space Week** winner:"), std::string::npos);
    EXPECT_NE(w.find("\"Moon Base\" (1 votes)"), std::string::npos);
}

// Times in Discord messages use Discord's timestamps (shown in each reader's
// own time zone); reminders count down, and DMs get the same.
TEST_F(DiscordPostsTest, TimesAreDiscordTimestamps) {
    auto m = meeting_svc->create(new_meeting());
    settle();
    fake->clear();
    ASSERT_EQ(chat_hub->remind_meeting(*meeting_svc->get(m.id)), 1);
    std::string r = nlohmann::json::parse(body_of("^POST /api/v10/channels/" + LUG_CH + "/messages$"))["content"];
    EXPECT_NE(r.find("<t:4095705600:F> (<t:4095705600:R>)"), std::string::npos) << r;

    fake->clear();
    ASSERT_TRUE(chat_hub->direct_message(regular_member_id, "dm.event_reminder",
        {{"title", "Brick Fest"}, {"when", "Wed 10/14 7:00 PM CDT"}, {"when_at", "2099-10-14T19:00:00"}, {"location", "Expo"}}));
    auto dms = sent("^POST /api/v10/channels/dm[^/]+/messages$");
    ASSERT_FALSE(dms.empty());
    std::string dm = nlohmann::json::parse(dms.back().body)["content"];
    EXPECT_NE(dm.find("<t:4095705600:F> (<t:4095705600:R>)"), std::string::npos) << dm;
    EXPECT_EQ(dm.find("CDT"), std::string::npos) << dm;
}
