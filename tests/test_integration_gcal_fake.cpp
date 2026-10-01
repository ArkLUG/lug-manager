// Google Calendar publishing against a local fake (tests/fake_google.hpp).
#include "integration_test_base.hpp"
#include "fake_google.hpp"

class GoogleFakeTest : public IntegrationTest {
protected:
    std::unique_ptr<FakeGoogle> fake;
    void SetUp() override {
        IntegrationTest::SetUp();
        fake = std::make_unique<FakeGoogle>(data_dir);
        gcal_client->reconfigure(fake->service_account_path, "lug-test-calendar@group.calendar.google.com", "America/Chicago");
        ASSERT_TRUE(gcal_client->is_configured());
    }
    void TearDown() override {
        fake.reset();
        IntegrationTest::TearDown();
    }
    LugEvent show(const std::string& title, bool priv = false) {
        LugEvent e;
        e.title = title; e.start_time = "2099-05-02T10:00:00"; e.end_time = "2099-05-02T16:00:00";
        e.location = "Library"; e.description = "Secret plans";
        e.scope = "lug_wide"; e.status = "confirmed"; e.suppress_discord = true; e.is_private = priv;
        return e;
    }
};

TEST_F(GoogleFakeTest, CreateUpdateDeleteEvent) {
    auto ev = *event_svc->get(event_svc->create(show("Expo Booth")).id);   // id is stored after publishing
    ASSERT_FALSE(ev.google_calendar_event_id.empty());
    EXPECT_FALSE(fake->matching("^POST /token").empty());          // service-account JWT exchanged
    auto created = fake->matching("^POST /calendar/v3/calendars/[^/]+/events");
    ASSERT_EQ(created.size(), 1u);
    EXPECT_NE(created[0].body.find("Expo Booth"), std::string::npos);

    auto upd = ev; upd.title = "Expo Booth (moved)";
    event_svc->update(ev.id, upd, true);
    auto puts = fake->matching("^PUT /calendar/v3/calendars/[^/]+/events/" + ev.google_calendar_event_id);
    ASSERT_EQ(puts.size(), 1u);
    EXPECT_NE(puts[0].body.find("moved"), std::string::npos);

    event_svc->cancel(ev.id);
    EXPECT_EQ(fake->matching("^DELETE /calendar/v3/calendars/[^/]+/events/" + ev.google_calendar_event_id).size(), 1u);
}

TEST_F(GoogleFakeTest, PrivateEventsAreRedactedAndSuppressedOnesSkipped) {
    event_svc->create(show("Members Only Party", true));
    auto created = fake->matching("^POST /calendar/v3/");
    ASSERT_EQ(created.size(), 1u);
    EXPECT_EQ(created[0].body.find("Members Only Party"), std::string::npos);
    EXPECT_EQ(created[0].body.find("Secret plans"), std::string::npos);
    EXPECT_EQ(created[0].body.find("Library"), std::string::npos);

    fake->clear();
    auto quiet = show("Not On The Calendar");
    quiet.suppress_calendar = true;
    auto ev = *event_svc->get(event_svc->create(quiet).id);
    EXPECT_TRUE(ev.google_calendar_event_id.empty());
    EXPECT_TRUE(fake->matching("^POST /calendar/v3/").empty());
}

TEST_F(GoogleFakeTest, ImportReadsUpcomingEvents) {
    event_svc->create(show("Import Me"));
    auto items = gcal_client->fetch_upcoming_events();
    ASSERT_FALSE(items.empty());
    EXPECT_NE(items[0].title.find("Import Me"), std::string::npos);   // "[LUG Wide] Import Me"
}

TEST_F(GoogleFakeTest, OfflineBlocksTheRealGoogle) {
    unsetenv("LUG_GOOGLE_BASE");
    GoogleCalendarClient real;
    // The fake's service-account file says token_uri = fake, so point a copy at Google's real endpoint.
    auto sa = nlohmann::json::parse(std::ifstream(fake->service_account_path));
    sa["token_uri"] = "https://oauth2.googleapis.com/token";
    std::string p = data_dir + "/real-sa.json";
    std::ofstream(p) << sa.dump();
    real.reconfigure(p, "x@group.calendar.google.com");
    fake->clear();
    EXPECT_THROW(real.create_event(show("Never Sent")), std::exception);
    EXPECT_TRUE(fake->requests().empty());
}
