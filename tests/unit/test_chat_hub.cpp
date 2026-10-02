// chat::ChatHub with a recording provider: what gets sent for events,
// meetings and DMs, with the default templates and the switches.
// (Replaces the old DiscordClient content-builder tests.)
#include "integrations/discord/DiscordClient.hpp"
#include "test_helper.hpp"
#include "chat/ChatHub.hpp"

namespace {

// Records every call; ids are "rec-1", "rec-2", ...
class Recorder : public chat::Provider {
public:
    struct Call { std::string op, where, text; std::vector<std::string> roles, users; };
    std::vector<Call> calls;
    std::map<std::string, std::string> switches;   // own_switch answers
    std::string forum = "forum", announce = "ann", chapter_ch = "", chapter_role_id = "";
    bool dm_ok = true;
    std::string account = "acct-1";

    std::string id() const override { return "rec"; }
    std::string name() const override { return "Recorder"; }
    bool ready() const override { return true; }
    chat::Caps caps() const override { return chat::Caps{true, true, true, true, false}; }
    std::optional<bool> own_switch(const std::string& n) const override {
        auto it = switches.find(n);
        if (it == switches.end()) return std::nullopt;
        return it->second == "1";
    }
    std::string place(chat::Place p) const override {
        if (p == chat::Place::Announcements) return announce;
        if (p == chat::Place::EventsForum) return forum;
        return "";
    }
    std::string chapter_channel(int64_t) const override { return chapter_ch; }
    std::string chapter_role(int64_t) const override { return chapter_role_id; }
    std::string announcement_role(bool non_lug) const override { return non_lug ? "nonlug_role" : "ann_role"; }
    std::string role_mention(const std::string& r) const override { return "<@&" + r + ">"; }
    std::string user_mention(const std::string& u) const override { return "<@" + u + ">"; }
    std::string thread_url(const std::string& t) const override { return t.empty() ? "" : "https://chat.example/" + t; }
    std::string inert(const std::string& t) const override { return t; }

    chat::Result rec(const std::string& op, const std::string& where, const chat::Message& m) {
        calls.push_back({op, where, m.text, m.roles, m.users});
        return chat::Result{true, "rec-" + std::to_string(calls.size()), ""};
    }
    chat::Result post(const std::string& c, const chat::Message& m) override { return rec("post", c, m); }
    chat::Result edit(const std::string& c, const std::string& id, const chat::Message& m) override { return rec("edit", c + "/" + id, m); }
    chat::Result remove(const std::string& c, const std::string& id) override { return rec("remove", c + "/" + id, {}); }
    chat::Result start_forum_thread(const std::string& f, const std::string& title, const chat::Message& m) override {
        auto r = rec("forum_thread", f, m);
        calls.back().where = f + "|" + title;
        return r;
    }
    chat::Result start_thread(const std::string& c, const std::string& msg, const std::string& title) override { return rec("thread", c + "/" + msg + "|" + title, {}); }
    chat::Result rename_thread(const std::string& t, const std::string& title) override { return rec("rename", t + "|" + title, {}); }
    chat::Result edit_thread_starter(const std::string& t, const chat::Message& m) override { return rec("edit_starter", t, m); }
    chat::Result remove_thread(const std::string& t) override { return rec("remove_thread", t, {}); }
    chat::Result create_event(const chat::ScheduledEvent& e) override { return rec("event", e.start_local, chat::Message{e.name, {}, {}}); }
    chat::Result update_event(const std::string& id, const chat::ScheduledEvent& e) override { return rec("update_event", id, chat::Message{e.name, {}, {}}); }
    chat::Result remove_event(const std::string& id) override { return rec("remove_event", id, {}); }
    std::string member_account(int64_t) const override { return account; }
    chat::Result direct_message(const std::string& a, const chat::Message& m) override {
        if (!dm_ok) return chat::Result{false, "", "closed DMs"};
        return rec("dm", a, m);
    }

    const Call* first(const std::string& op) const {
        for (const auto& c : calls) if (c.op == op) return &c;
        return nullptr;
    }
};

LugEvent show(const std::string& scope = "lug_wide") {
    LugEvent e;
    e.id = 7; e.title = "Spring Showcase"; e.start_time = "2026-04-15T10:00:00"; e.end_time = "2026-04-17T16:00:00";
    e.location = "Convention Center, 1 Main St, Little Rock, AR 72201"; e.scope = scope; e.description = "A great event";
    e.max_attendees = 50; e.signup_deadline = "2026-04-10"; e.discord_ping_role_ids = "role_a,role_b";
    e.event_lead_id = 9; e.event_lead_name = "Lee D.";
    return e;
}

} // namespace

class ChatHubTest : public DbFixture {
protected:
    std::shared_ptr<Recorder> rec = std::make_shared<Recorder>();
    std::unique_ptr<chat::ChatHub> hub;
    void SetUp() override {
        DbFixture::SetUp();
        hub = std::make_unique<chat::ChatHub>(*db, "https://lug.example");
        hub->add(rec);
        hub->set_timezone_source([] { return std::string("America/Chicago"); });
    }
};

TEST_F(ChatHubTest, EventAnnouncementThreadAndScheduledEvent) {
    hub->event_published(show());
    auto* thread = rec->first("forum_thread");
    ASSERT_NE(thread, nullptr);
    EXPECT_EQ(thread->where, "forum|Spring Showcase | Little Rock, AR | 4/15/26-4/17/26");
    EXPECT_NE(thread->text.find("<@&role_a> <@&role_b>"), std::string::npos);      // only the extra roles in the thread
    EXPECT_EQ(thread->text.find("ann_role"), std::string::npos);
    EXPECT_NE(thread->text.find("Lead: <@acct-1>"), std::string::npos);
    EXPECT_NE(thread->text.find("Capacity: 50"), std::string::npos);
    EXPECT_NE(thread->text.find("Signup Deadline: 4/10"), std::string::npos);
    EXPECT_NE(thread->text.find("\n\nA great event"), std::string::npos);
    EXPECT_EQ(thread->users, std::vector<std::string>{"acct-1"});

    auto* post = rec->first("post");
    ASSERT_NE(post, nullptr);
    EXPECT_EQ(post->where, "ann");
    EXPECT_EQ(post->text, "<@&ann_role> <@&role_a> <@&role_b>\n**Spring Showcase**\nDates: 4/15 - 4/17\n"
                          "Location: Convention Center, 1 Main St, Little Rock, AR 72201\nDiscussion Thread: https://chat.example/rec-1");
    EXPECT_EQ(post->roles, (std::vector<std::string>{"ann_role", "role_a", "role_b"}));
    auto* ev = rec->first("event");
    ASSERT_NE(ev, nullptr);
    EXPECT_EQ(ev->text, "Spring Showcase");
}

TEST_F(ChatHubTest, PingsOffNonLugAndChapterCopy) {
    rec->switches["pings"] = "0";
    rec->chapter_ch = "ch-chan";
    LugEvent e = show("non_lug");
    e.chapter_id = 3;
    hub->event_published(e);
    for (const auto& c : rec->calls) {
        EXPECT_EQ(c.text.find("<@&"), std::string::npos) << c.op;
        EXPECT_EQ(c.text.find("<@acct"), std::string::npos) << c.op;
        EXPECT_TRUE(c.roles.empty()) << c.op;
    }
    EXPECT_NE(rec->first("forum_thread")->text.find("Lead: Lee D."), std::string::npos);
    EXPECT_NE(rec->first("post")->text.find("[External] **Spring Showcase**"), std::string::npos);
    int posts = 0;
    for (const auto& c : rec->calls) if (c.op == "post") ++posts;
    EXPECT_EQ(posts, 2);                                   // LUG channel + the chapter's
}

TEST_F(ChatHubTest, MeetingAnnouncementAndReminder) {
    Meeting m;
    m.id = 4; m.title = "Monthly Meeting"; m.description = "Agenda items"; m.location = "Library Room 3";
    m.start_time = "2026-04-15T19:00:00"; m.end_time = "2026-04-15T21:00:00"; m.scope = "lug_wide";
    hub->meeting_published(m);
    auto* post = rec->first("post");
    ASSERT_NE(post, nullptr);
    EXPECT_EQ(post->text, "<@&ann_role>\n**Monthly Meeting**\nWhen: 4/15 7:00 PM CDT – 4/15 9:00 PM CDT\nWhere: Library Room 3\n\nAgenda items");
    rec->calls.clear();
    EXPECT_EQ(hub->remind_meeting(m), 1);
    EXPECT_EQ(rec->calls[0].text, "⏰ **Reminder:** Monthly Meeting - Wed 4/15 7:00 PM CDT at Library Room 3");
    // Pings off
    rec->calls.clear();
    rec->switches["pings"] = "0";
    hub->meeting_published(m);
    EXPECT_EQ(rec->first("post")->text.find("<@&"), std::string::npos);
}

TEST_F(ChatHubTest, DirectMessagesAndQuietMode) {
    EXPECT_TRUE(hub->direct_message(1, "dm.waitlist", {{"title", "Show"}, {"when", "Sat"}}));
    EXPECT_EQ(rec->calls.back().text, "🎉 A spot opened up for **Show** (Sat) - you're off the waitlist and now going. "
                                      "If you can't make it, please cancel your RSVP so the next person gets the spot.");
    rec->dm_ok = false;
    EXPECT_FALSE(hub->direct_message(1, "dm.waitlist", {{"title", "Show"}, {"when", "Sat"}}));
    rec->dm_ok = true;
    hub->set_switch("rec", "dms", false);
    EXPECT_FALSE(hub->can_dm(1));
    hub->set_switch("rec", "dms", true);
    EXPECT_TRUE(hub->can_dm(1));
    auto st = db->prepare("INSERT OR REPLACE INTO lug_settings (key, value) VALUES ('chat.quiet', '1')");
    st.step();
    EXPECT_FALSE(hub->can_dm(1));
    size_t before = rec->calls.size();
    hub->event_published(show());
    EXPECT_EQ(rec->calls.size(), before);                 // nothing new while quiet
}

// Discord timestamps are exact moments, so daylight saving must be applied
// when turning the LUG's local times into them (US Central: CDT from
// 2026-03-08 2 AM to 2026-11-01 2 AM).
TEST(ChatTimes, DaylightSavingIsApplied) {
    const char* tz = "America/Chicago";
    auto utc = [](int y, int mo, int d, int h, int mi) {
        std::tm t{}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi;
        return timegm(&t);
    };
    EXPECT_EQ(DiscordClient::local_to_epoch("2026-03-07T19:00:00", tz), utc(2026, 3, 8, 1, 0));    // CST, UTC-6
    EXPECT_EQ(DiscordClient::local_to_epoch("2026-03-08T19:00:00", tz), utc(2026, 3, 9, 0, 0));    // CDT, UTC-5 (same day it starts)
    EXPECT_EQ(DiscordClient::local_to_epoch("2026-08-22T14:00:00", tz), utc(2026, 8, 22, 19, 0));  // CDT
    EXPECT_EQ(DiscordClient::local_to_epoch("2026-10-31T19:00:00", tz), utc(2026, 11, 1, 0, 0));   // last CDT evening
    EXPECT_EQ(DiscordClient::local_to_epoch("2026-11-01T19:00:00", tz), utc(2026, 11, 2, 1, 0));   // CST again
    EXPECT_EQ(DiscordClient::local_to_epoch("2026-12-12T10:00:00", tz), utc(2026, 12, 12, 16, 0)); // CST
    // A meeting spanning the change keeps its real length: 11 PM -> 3 AM is 5 hours that night.
    EXPECT_EQ(DiscordClient::local_to_epoch("2026-11-01T03:00:00", tz) - DiscordClient::local_to_epoch("2026-10-31T23:00:00", tz), 5 * 3600);
}
