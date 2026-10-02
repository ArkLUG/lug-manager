// Chat integration controls, against the fake Discord: per-provider switches,
// per-item "leave out" options, quiet mode, Discord off, message templates
// (wording, editor, test send), the activity log and retry, nickname options.
#include "integration_test_base.hpp"
#include "fake_discord.hpp"

namespace {
const std::string LUG_CH = "600000000000000001", FORUM_CH = "600000000000000002", CH_CH = "600000000000000003";
const std::string ANN_ROLE = "500000000000000010";
}

class ChatTest : public IntegrationTest {
protected:
    std::unique_ptr<FakeDiscord> fake;
    void SetUp() override {
        IntegrationTest::SetUp();
        fake = std::make_unique<FakeDiscord>();
        discord_client->reconfigure(fake->guild_id, LUG_CH, FORUM_CH, ANN_ROLE, "", "America/Chicago");
        auto st = db->prepare("UPDATE chapters SET discord_announcement_channel_id=? WHERE id=?");
        st.bind(1, CH_CH); st.bind(2, test_chapter_id);
        st.step();
    }
    void TearDown() override { settle(); fake.reset(); IntegrationTest::TearDown(); }
    void settle() {
        size_t n = fake->requests().size();
        for (int i = 0; i < 40; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            size_t m = fake->requests().size();
            if (m == n && i > 2) return;
            n = m;
        }
    }
    size_t count(const std::string& re) { settle(); return fake->matching(re).size(); }
    std::string last_body(const std::string& re) { settle(); auto r = fake->matching(re); return r.empty() ? "" : r.back().body; }
    LugEvent event(const std::string& scope = "lug_wide") {
        LugEvent e;
        e.title = "Brick Fest"; e.start_time = "2099-10-14T09:00:00"; e.end_time = "2099-10-14T17:00:00";
        e.location = "Expo Hall"; e.scope = scope; e.status = "confirmed"; e.suppress_calendar = true;
        if (scope == "chapter") e.chapter_id = test_chapter_id;
        return e;
    }
    Meeting meeting() {
        Meeting m;
        m.title = "October meeting"; m.start_time = "2099-10-14T19:00:00"; m.end_time = "2099-10-14T21:00:00";
        m.location = "Library"; m.scope = "lug_wide"; m.suppress_calendar = true;
        return m;
    }
    int64_t activity(const std::string& where) {
        auto st = db->prepare("SELECT COUNT(*) FROM chat_activity WHERE " + where);
        return st.step() ? st.col_int(0) : -1;
    }
};

TEST_F(ChatTest, SwitchesTurnPartsOff) {
    chat_hub->set_switch("discord", "event_thread", false);
    chat_hub->set_switch("discord", "event_scheduled", false);
    chat_hub->set_switch("discord", "meeting_announce", false);
    event_svc->create(event());
    EXPECT_EQ(count("^POST /api/v10/channels/" + FORUM_CH + "/threads$"), 0u);
    EXPECT_EQ(count("^POST /api/v10/guilds/[0-9]+/scheduled-events$"), 0u);
    EXPECT_EQ(count("^POST /api/v10/channels/" + LUG_CH + "/messages$"), 1u);     // the announcement still goes
    EXPECT_EQ(last_body("^POST /api/v10/channels/" + LUG_CH + "/messages$").find("Discussion Thread"), std::string::npos);
    fake->clear();
    meeting_svc->create(meeting());
    EXPECT_EQ(count("^POST /api/v10/channels/"), 0u);
    EXPECT_EQ(count("^POST /api/v10/guilds/[0-9]+/scheduled-events$"), 1u);
    // Saved from the settings page (unticked = off)
    POST("/settings/discord", "discord_guild_id=" + fake->guild_id + "&discord_announcements_channel_id=" + LUG_CH +
         "&chat_event_thread=1&chat_event_announce=1&pings_on=1&chat_dms=1&nicknames=full", admin_token);
    EXPECT_EQ(settings_repo->get("chat.discord.event_thread", ""), "1");
    EXPECT_EQ(settings_repo->get("chat.discord.meeting_scheduled", ""), "0");
    EXPECT_EQ(settings_repo->get("discord_suppress_pings", ""), "0");
    EXPECT_EQ(settings_repo->get("discord_suppress_updates", ""), "1");
    EXPECT_EQ(settings_repo->get("chat.discord.nicknames", ""), "full");
    EXPECT_EQ(settings_repo->get("auth_discord_enabled", ""), "0");
}

TEST_F(ChatTest, PerItemOptionsFromTheForm) {
    // The form's "leave out" boxes
    auto r = POST("/events", "title=Swap+Meet&start_time=2099-11-01T10:00&end_time=2099-11-01T14:00&scope=lug_wide&status=confirmed"
                  "&chat_opts=1&skip_scheduled=1&skip_thread=1&skip_bogus=1", admin_token);
    ASSERT_LT(r.code, 400);
    auto st = db->prepare("SELECT id FROM lug_events WHERE title='Swap Meet'");
    ASSERT_TRUE(st.step());
    int64_t id = st.col_int(0);
    EXPECT_EQ(chat_hub->skipped("event", id), (std::set<std::string>{"scheduled", "thread"}));
    EXPECT_EQ(count("^POST /api/v10/guilds/[0-9]+/scheduled-events$"), 0u);
    EXPECT_EQ(count("^POST /api/v10/channels/" + FORUM_CH + "/threads$"), 0u);
    EXPECT_EQ(count("^POST /api/v10/channels/" + LUG_CH + "/messages$"), 1u);
    // The edit form shows them ticked
    auto form = GET("/events/" + std::to_string(id) + "/edit", admin_token);
    expect_contains(form, "name=\"skip_scheduled\" value=\"1\" checked");
    // An edit without the options block (API) leaves them alone
    LugEvent upd = *event_svc->get(id);
    event_svc->update(id, upd);
    EXPECT_EQ(chat_hub->skipped("event", id).size(), 2u);
    // No update notes for this one
    chat_hub->set_skipped("event", id, "update_note");
    fake->clear();
    event_svc->update(id, upd, true, true);
    EXPECT_EQ(count("^POST "), 0u);
    // Meetings
    chat_hub->set_skipped("meeting", 1, "announce,scheduled,bogus");
    EXPECT_EQ(chat_hub->skipped("meeting", 1), (std::set<std::string>{"announce", "scheduled"}));
}

TEST_F(ChatTest, QuietModeEditsButPostsNothingNew) {
    auto ev = event_svc->create(event());
    auto e = *event_svc->get(ev.id);
    settings_repo->set("chat.quiet", "1");
    fake->clear();
    LugEvent upd = e;
    upd.title = "Brick Fest 2";
    event_svc->update(ev.id, upd, true, true);
    EXPECT_EQ(count("^PATCH /api/v10/channels/" + LUG_CH + "/messages/" + e.discord_lug_message_id + "$"), 1u);   // edited
    EXPECT_EQ(count("^POST "), 0u);                                                                              // no "updated" note
    event_svc->create(event());
    meeting_svc->create(meeting());
    EXPECT_EQ(count("^POST "), 0u);
    EXPECT_GE(activity("action='skip'"), 3);
    // DMs go by email instead (if any) - here: nothing sent on Discord
    Member m; m.first_name = "Dee"; m.display_name = "Dee M."; m.discord_user_id = "300000000000000005"; m.role = "member";
    int64_t mid = member_repo->create(m).id;
    EXPECT_FALSE(notifier_with_chat()->notify(mid, "waitlist", "dm.waitlist", {{"title", "X"}, {"when", "soon"}}));
    EXPECT_EQ(count("users/@me/channels"), 0u);
}

TEST_F(ChatTest, DiscordSwitchedOffSendsNothing) {
    Features::set("discord", false);
    event_svc->create(event());
    meeting_svc->create(meeting());
    EXPECT_TRUE(fake->requests().empty());
    // Sign-in with Discord is gone; its API pages too
    expect_not_contains(GET("/login"), "Sign in with Discord");
    EXPECT_NE(GET("/auth/login").location.find("/login"), std::string::npos);
    EXPECT_EQ(GET("/settings/roles", admin_token).code, 404);
    auto s = GET("/settings", admin_token);
    expect_contains(s, "Discord is switched off");
    // Members with Discord get email instead of a DM
    Member m; m.first_name = "Eve"; m.display_name = "Eve M."; m.discord_user_id = "300000000000000006";
    m.email = "eve@example.org"; m.role = "member";
    int64_t mid = member_repo->create(m).id;
    EXPECT_TRUE(notifier_with_chat()->notify(mid, "waitlist", "dm.waitlist", {{"title", "Show"}, {"when", "Sat"}}));
    ASSERT_EQ(mailer->outbox().size(), 1u);
    EXPECT_EQ(mailer->outbox()[0].subject, "You're in: Show");
    EXPECT_TRUE(fake->requests().empty());
    Features::set("discord", true);
}

TEST_F(ChatTest, CustomWordingIsUsedAndMentionsAreInert) {
    chat::TemplateStore(*db).save("event.announcement", "", "📣 {title} on {dates}[[ - {location}]]\n{link}");
    LugEvent e = event();
    e.title = "@everyone <@&123> party";
    event_svc->create(e);
    std::string body = last_body("^POST /api/v10/channels/" + LUG_CH + "/messages$");
    auto j = nlohmann::json::parse(body);
    std::string text = j["content"];
    EXPECT_EQ(text.find("📣 @​everyone <​@&123> party on 10/14 - Expo Hall"), 0u) << text;
    EXPECT_EQ(j["allowed_mentions"]["parse"].size(), 0u);
    // A DM with custom wording, rendered for the member
    chat::TemplateStore(*db).save("dm.waitlist", "Spot for {name}", "Good news {name}: {title}!");
    Member m; m.first_name = "Fay"; m.display_name = "Fay M."; m.discord_user_id = "300000000000000007"; m.role = "member";
    int64_t mid = member_repo->create(m).id;
    EXPECT_TRUE(notifier_with_chat()->notify(mid, "waitlist", "dm.waitlist", {{"title", "Show"}, {"when", "Sat"}}));
    EXPECT_NE(last_body("^POST /api/v10/channels/dm300000000000000007/messages$").find("Good news Fay M.: Show!"), std::string::npos);
}

TEST_F(ChatTest, MessageEditorPages) {
    EXPECT_EQ(GET("/settings/messages", member_token).code, 403);
    auto list = GET("/settings/messages", admin_token);
    expect_contains(list, "Event announcement");
    expect_contains(list, "Sign-in emails");
    auto ed = GET("/settings/messages/reminder.event", admin_token);
    expect_contains(ed, "⏰ **Reminder:** {title} starts {when}[[ ({when_relative})]][[ at {location}]]");
    expect_contains(ed, "data-text=\"{location}\"");
    EXPECT_EQ(GET("/settings/messages/nope", admin_token).code, 404);
    // Preview
    auto pv = POST("/settings/messages/reminder.event/preview", "body=Hi+{title}+{bogus}", admin_token);
    expect_contains(pv, "Hi Brick Fest");
    expect_contains(pv, "Unknown placeholder: {bogus}");
    // Saving checks: required placeholders, unknown ones, empty
    EXPECT_EQ(POST("/settings/messages/email.sign_in_link", "subject=Hi&body=No+link+here", admin_token).code, 400);
    EXPECT_EQ(POST("/settings/messages/reminder.event", "body=%7Bbogus%7D", admin_token).code, 400);
    EXPECT_EQ(POST("/settings/messages/reminder.event", "body=+", admin_token).code, 400);
    EXPECT_EQ(POST("/settings/messages/dm.waitlist", "subject=&body=x+{title}", admin_token).code, 400);
    auto ok = POST("/settings/messages/reminder.event", "body=Soon%3A+{title}%0D%0Aat+{when}", admin_token);
    EXPECT_EQ(ok.code, 200);
    EXPECT_EQ(chat::TemplateStore(*db).body("reminder.event"), "Soon: {title}\nat {when}");     // CRLF -> LF
    expect_contains(GET("/settings/messages", admin_token), "Customized");
    // Reset
    POST("/settings/messages/reminder.event/reset", "", admin_token);
    EXPECT_FALSE(chat::TemplateStore(*db).customized("reminder.event"));
    // Test send: the admin has Discord in the fixture -> a DM
    std::string admin_discord;
    { auto st = db->prepare("SELECT COALESCE(discord_user_id,'') FROM members WHERE id=?"); st.bind(1, admin_member_id); st.step(); admin_discord = st.col_text(0); }
    auto t = POST("/settings/messages/dm.waitlist/test", "", admin_token);
    if (!admin_discord.empty()) {
        expect_contains(t, "Sent you a direct message");
        EXPECT_EQ(count("^POST /api/v10/channels/dm" + admin_discord + "/messages$"), 1u);
    }
}

TEST_F(ChatTest, ActivityLogAndRetry) {
    meeting_svc->create(meeting());
    EXPECT_GE(activity("ok=1 AND action='post' AND what='meeting.announcement'"), 1);
    // A failing standalone post is logged with its text and can be retried
    fake->rate_limit_next = 20;
    chat::Provider* p = chat_hub->provider("discord");
    ASSERT_NE(p, nullptr);
    auto r = chat_hub->post_in(*p, LUG_CH, "test.message", {});
    EXPECT_FALSE(r.ok);
    fake->rate_limit_next = 0;
    int64_t failed_id = 0;
    { auto st = db->prepare("SELECT id FROM chat_activity WHERE ok=0 AND payload<>'' ORDER BY id DESC"); ASSERT_TRUE(st.step()); failed_id = st.col_int(0); }
    expect_contains(GET("/settings/chat-activity?failed=1", admin_token), "Retry");
    expect_contains(GET("/settings", admin_token), "failed</span>");
    EXPECT_EQ(POST("/settings/chat-activity/" + std::to_string(failed_id) + "/retry", "", member_token).code, 403);
    fake->clear();
    expect_contains(POST("/settings/chat-activity/" + std::to_string(failed_id) + "/retry", "", admin_token), "Sent.");
    EXPECT_NE(last_body("^POST /api/v10/channels/" + LUG_CH + "/messages$").find("LUG Manager test announcement"), std::string::npos);
    EXPECT_FALSE(chat_hub->retry(failed_id));                 // only once
    // The test-message button reports failures honestly now
    fake->rate_limit_next = 20;
    expect_contains(POST("/api/discord/test-announcement", "", admin_token), "send it:");
    fake->rate_limit_next = 0;
}

TEST_F(ChatTest, NoForumMeansAThreadFromTheAnnouncement) {
    discord_client->set_events_forum_channel_id("");
    auto ev = event_svc->create(event());
    auto e = *event_svc->get(ev.id);
    ASSERT_FALSE(e.discord_lug_message_id.empty());
    EXPECT_EQ(count("^POST /api/v10/channels/" + LUG_CH + "/messages/" + e.discord_lug_message_id + "/threads$"), 1u);
    EXPECT_NE(last_body("^PATCH /api/v10/channels/" + LUG_CH + "/messages/" + e.discord_lug_message_id + "$").find("Discussion Thread: "),
              std::string::npos);
    EXPECT_FALSE(e.discord_thread_id.empty());
    // Settings can clear the forum channel now (it used to come back until a restart)
    discord_client->set_events_forum_channel_id(FORUM_CH);
    POST("/settings/discord", "discord_guild_id=" + fake->guild_id + "&discord_announcements_channel_id=" + LUG_CH +
         "&discord_events_forum_channel_id=", admin_token);
    EXPECT_EQ(discord_client->get_events_forum_channel_id(), "");
}

TEST_F(ChatTest, NicknameOptions) {
    Member m; m.first_name = "Gus"; m.last_name = "Builder"; m.discord_user_id = "300000000000000008"; m.role = "member";
    int64_t id = member_svc->create(m).id;
    Member u = *member_repo->find_by_id(id);
    EXPECT_EQ(member_svc->nickname_for(u), u.display_name);
    settings_repo->set("chat.discord.nicknames", "full");
    EXPECT_EQ(member_svc->nickname_for(u), "Gus Builder");
    settings_repo->set("chat.discord.nicknames", "off");
    EXPECT_EQ(member_svc->nickname_for(u), "");
    fake->clear();
    u.last_name = "Brickman";
    member_svc->update(id, u);
    EXPECT_EQ(count("members/300000000000000008"), 0u);        // left alone
}
