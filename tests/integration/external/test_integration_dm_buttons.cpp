// Buttons on reminder DMs: what's sent (fake Discord) and what a click does
// (signed /discord/interactions requests, as Discord sends them).
#include "integration_test_base.hpp"
#include "fake_discord.hpp"
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <vector>

namespace {
std::string hexs(const unsigned char* d, size_t n) {
    static const char* h = "0123456789abcdef";
    std::string o;
    for (size_t i = 0; i < n; ++i) { o += h[d[i] >> 4]; o += h[d[i] & 15]; }
    return o;
}
std::string local_in(std::time_t secs) {   // LUG-local ISO, secs from now
    return local_iso(std::time(nullptr) + secs);
}
}

class DmButtonsTest : public IntegrationTest {
protected:
    EVP_PKEY* key = nullptr;
    std::unique_ptr<FakeDiscord> fake;
    std::shared_ptr<Notifier> n;
    void SetUp() override {
        EVP_PKEY_CTX* c = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
        EVP_PKEY_keygen_init(c); EVP_PKEY_keygen(c, &key); EVP_PKEY_CTX_free(c);
        unsigned char pub[32]; size_t len = 32;
        EVP_PKEY_get_raw_public_key(key, pub, &len);
        config.discord_public_key = hexs(pub, len);
        IntegrationTest::SetUp();
        fake = std::make_unique<FakeDiscord>();
        discord_client->reconfigure(fake->guild_id, "600000000000000001");
        n = notifier_with_chat();
        n->set_actions_available([] { return true; });
    }
    void TearDown() override { fake.reset(); IntegrationTest::TearDown(); if (key) EVP_PKEY_free(key); }

    Response click(const std::string& user, const std::string& custom_id, const std::string& content = "Reminder text") {
        nlohmann::json b = {{"type", 3}, {"data", {{"custom_id", custom_id}, {"component_type", 2}}},
                            {"user", {{"id", user}}}, {"message", {{"content", content}}}};
        std::string body = b.dump(), ts = "1700000000";
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key);
        std::string msg = ts + body;
        size_t sl = 0;
        EVP_DigestSign(ctx, nullptr, &sl, reinterpret_cast<const unsigned char*>(msg.data()), msg.size());
        std::vector<unsigned char> sig(sl);
        EVP_DigestSign(ctx, sig.data(), &sl, reinterpret_cast<const unsigned char*>(msg.data()), msg.size());
        EVP_MD_CTX_free(ctx);
        return http("POST", "/discord/interactions", body, "", false, "", true,
                    {"X-Signature-Ed25519: " + hexs(sig.data(), sl), "X-Signature-Timestamp: " + ts});
    }
    // The components of the last DM to a Discord user
    nlohmann::json last_dm(const std::string& user) {
        auto m = fake->matching("POST /api/v10/channels/dm" + user + "/messages");
        if (m.empty()) return nlohmann::json();
        return nlohmann::json::parse(m.back().body, nullptr, false);
    }
    std::string action_id(const nlohmann::json& dm, const std::string& prefix) {
        for (const auto& row : dm.value("components", nlohmann::json::array()))
            for (const auto& b : row["components"])
                if (b.contains("custom_id") && b["custom_id"].get<std::string>().rfind(prefix, 0) == 0) return b["custom_id"];
        return "";
    }
    int64_t make_event(const std::string& title, std::time_t in_secs, int max = 0) {
        LugEvent e;
        e.title = title; e.start_time = local_in(in_secs).substr(0, 19); e.end_time = local_in(in_secs + 6 * 3600).substr(0, 19);
        e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.suppress_calendar = true; e.max_attendees = max;
        return event_svc->create(e).id;
    }
};

TEST_F(DmButtonsTest, EventReminderButtonsAndCantMakeIt) {
    const int64_t ev = make_event("Button Show", 2 * 86400, 1);
    POST("/events/" + std::to_string(ev) + "/rsvp", "", member_token);        // going
    POST("/events/" + std::to_string(ev) + "/rsvp", "", chapter_lead_token);  // waitlist
    ASSERT_TRUE(n->notify(regular_member_id, "event_reminder", "dm.event_reminder",
                          {{"title", "Button Show"}, {"when", "soon"}, {"when_at", local_in(2 * 86400).substr(0, 19)}, {"location", "Hall"}},
                          false, "", Notifier::about_for("event_reminder", std::to_string(ev))));
    auto dm = last_dm("member-test-001");
    ASSERT_TRUE(dm.is_object());
    const std::string s = dm.dump();
    EXPECT_NE(s.find("http://lug.test/events/" + std::to_string(ev)), std::string::npos);   // View event link
    EXPECT_NE(s.find("Can't make it"), std::string::npos);
    EXPECT_NE(s.find("Remind me later"), std::string::npos);
    EXPECT_NE(s.find("Don't remind me"), std::string::npos);
    const std::string off = action_id(dm, "lm:rsvp_off:");
    ASSERT_FALSE(off.empty());

    // Someone else can't use it; junk ids are refused politely
    expect_contains(click("lead-test-001", off), "for someone else");
    expect_contains(click("member-test-001", "lm:rsvp_off:999999"), "doesn't work any more");
    expect_contains(click("member-test-001", "lm:bogus"), "doesn't work any more");

    // Can't make it: RSVP cancelled, the DM is edited, the waitlist moves up (and is told)
    fake->clear();
    auto r = click("member-test-001", off, "**Reminder:** Button Show");
    EXPECT_EQ(r.code, 200);
    auto j = nlohmann::json::parse(r.body);
    EXPECT_EQ(j["type"], 7);
    const std::string content = j["data"]["content"];
    EXPECT_NE(content.find("**Reminder:** Button Show"), std::string::npos);
    EXPECT_NE(content.find("Your RSVP for Button Show is cancelled"), std::string::npos);
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM event_rsvps WHERE event_id=? AND member_id=?", ev, regular_member_id), 0);
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM event_rsvps WHERE event_id=? AND member_id=? AND status='going'", ev, chapter_lead_member_id), 1);
    EXPECT_TRUE(fake->wait_for("POST /api/v10/channels/dmlead-test-001/messages"));
    // Clicking again: already off the list
    expect_contains(click("member-test-001", off), "not on the list");
}

TEST_F(DmButtonsTest, RemindMeLaterAndDontRemindMe) {
    const int64_t ev = make_event("Snooze Show", 2 * 86400);
    const std::string when_at = local_in(2 * 86400).substr(0, 19);
    ASSERT_TRUE(n->notify(regular_member_id, "event_reminder", "dm.event_reminder",
                          {{"title", "Snooze Show"}, {"when", "soon"}, {"when_at", when_at}, {"location", ""}},
                          false, "", Notifier::about_for("event_reminder", std::to_string(ev))));
    auto dm = last_dm("member-test-001");
    const std::string snooze = action_id(dm, "lm:snooze:"), mute = action_id(dm, "lm:mute:");
    ASSERT_FALSE(snooze.empty());
    auto r = nlohmann::json::parse(click("member-test-001", snooze).body);
    EXPECT_NE(r["data"]["content"].get<std::string>().find("I'll remind you again <t:"), std::string::npos);
    const int64_t until = query_int(*db, "SELECT snooze_until FROM reminder_dms WHERE id=?", static_cast<int64_t>(std::stoll(snooze.substr(10))));
    const std::time_t start = DiscordClient::local_to_epoch(when_at, discord_client->get_timezone());
    EXPECT_EQ(until, start - 2 * 3600);                                   // two hours before it starts
    EXPECT_EQ(r["data"]["components"][0]["components"][0]["style"], 5);   // the link stays

    // Sent again when due (once)
    fake->clear();
    EXPECT_EQ(n->send_snoozed(until - 60), 0);
    EXPECT_EQ(n->send_snoozed(until + 1), 1);
    EXPECT_FALSE(fake->matching("POST /api/v10/channels/dmmember-test-001/messages").empty());
    EXPECT_EQ(n->send_snoozed(until + 2), 0);

    // Don't remind me: turned off, with Undo
    auto m = nlohmann::json::parse(click("member-test-001", mute).body);
    EXPECT_NE(m["data"]["content"].get<std::string>().find("Turned off: Event reminders"), std::string::npos);
    EXPECT_FALSE(NotificationPrefs(*db).wants(regular_member_id, "event_reminder"));
    EXPECT_NE(m.dump().find("lm:unmute:"), std::string::npos);
    click("member-test-001", "lm:unmute:" + mute.substr(8));
    EXPECT_TRUE(NotificationPrefs(*db).wants(regular_member_id, "event_reminder"));
    EXPECT_GE(query_int(*db, "SELECT COUNT(*) FROM audit_log WHERE action IN ('member.notifications_off','member.notifications_on','member.reminder_snoozed')"), 3);

    // Dues: a later day; the digest has no snooze
    ASSERT_TRUE(n->notify(regular_member_id, "dues_reminder", "dm.dues_reminder", {{"paid_until", "2030-01-01"}}, false, "",
                          Notifier::about_for("dues_reminder", "2030-01-01")));
    auto dues = last_dm("member-test-001");
    EXPECT_NE(dues.dump().find("http://lug.test/account"), std::string::npos);
    const std::string ds = action_id(dues, "lm:snooze:");
    const std::time_t before = std::time(nullptr);
    click("member-test-001", ds);
    EXPECT_GE(query_int(*db, "SELECT snooze_until FROM reminder_dms WHERE id=?", static_cast<int64_t>(std::stoll(ds.substr(10)))), before + 3 * 86400);
    ASSERT_TRUE(n->notify(regular_member_id, "digest", "dm.digest", {{"items", "x"}}, false, "", Notifier::about_for("digest", "")));
    EXPECT_TRUE(action_id(last_dm("member-test-001"), "lm:snooze:").empty());
    EXPECT_FALSE(action_id(last_dm("member-test-001"), "lm:mute:").empty());
}

TEST_F(DmButtonsTest, CantMakeMyShiftAndLinksOnlyWithoutInteractions) {
    const int64_t ev = make_event("Shift Show", 3 * 86400);
    int64_t shift = 0, signup = 0;
    {
        auto s = db->prepare("INSERT INTO event_shifts (event_id, title, starts_at, ends_at, slots) VALUES (?, 'Teardown', ?, ?, 2) RETURNING id");
        s.bind(1, ev); s.bind(2, local_in(3 * 86400).substr(0, 16)); s.bind(3, local_in(3 * 86400 + 7200).substr(0, 16));
        ASSERT_TRUE(s.step()); shift = s.col_int(0);
    }
    {
        auto u = db->prepare("INSERT INTO event_shift_signups (shift_id, member_id) VALUES (?, ?) RETURNING id");
        u.bind(1, shift); u.bind(2, regular_member_id);
        ASSERT_TRUE(u.step()); signup = u.col_int(0);
    }
    ASSERT_TRUE(n->notify(regular_member_id, "shift_reminder", "dm.shift_reminder",
                          {{"shift", "Teardown"}, {"event", "Shift Show"}, {"when", "soon"}, {"when_at", local_in(3 * 86400).substr(0, 16)}},
                          false, "", Notifier::about_for("shift_reminder", std::to_string(signup) + ":" + std::to_string(ev))));
    auto dm = last_dm("member-test-001");
    EXPECT_NE(dm.dump().find("http://lug.test/events/" + std::to_string(ev)), std::string::npos);
    const std::string off = action_id(dm, "lm:shift_off:");
    ASSERT_FALSE(off.empty());
    auto r = nlohmann::json::parse(click("member-test-001", off).body);
    EXPECT_NE(r["data"]["content"].get<std::string>().find("You're off the Teardown shift"), std::string::npos);
    EXPECT_EQ(query_int(*db, "SELECT COUNT(*) FROM event_shift_signups WHERE id=?", signup), 0);

    // Without Discord interactions set up: link buttons only
    n->set_actions_available([] { return false; });
    ASSERT_TRUE(n->notify(regular_member_id, "dues_reminder", "dm.dues_reminder", {{"paid_until", "2030-01-01"}}, false, "",
                          Notifier::about_for("dues_reminder", "2030-01-01")));
    auto links = last_dm("member-test-001").dump();
    EXPECT_NE(links.find("http://lug.test/account"), std::string::npos);
    EXPECT_EQ(links.find("lm:"), std::string::npos);
}
