// Guardian / consent tracking for young members.
#include "integration_test_base.hpp"

TEST_F(IntegrationTest, GuardianConsentSavedAndBadgedAtCheckin) {
    Member kid;
    kid.first_name = "Kiddo";
    kid.last_name = "Builder";
    kid.display_name = "Kiddo B.";
    kid.fol_status = "kfol";
    auto k = member_repo->create(kid);

    // Edit form shows the guardian block open for a KFOL
    auto form = GET("/members/" + std::to_string(k.id), chapter_lead_token);
    expect_contains(form, "Guardian &amp; consent");

    Meeting m;
    m.title = "Kids Meeting";
    m.start_time = "2099-02-02T19:00:00";
    m.end_time = "2099-02-02T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);
    attendance_repo->check_in(k.id, "meeting", mtg.id);
    auto list = GET("/attendance/list/meeting/" + std::to_string(mtg.id), admin_token);
    expect_contains(list, "no photos");
    expect_contains(list, "consent missing");

    auto r = POST("/members/" + std::to_string(k.id),
                  "first_name=Kiddo&last_name=Builder&fol_status=kfol&guardian_form=1&guardian_name=Pat+Parent"
                  "&guardian_phone=555-0100&consent_on_file=1&consent_date=2099-01-01&photo_release=1",
                  chapter_lead_token);
    auto g = member_repo->get_guardian(k.id);
    EXPECT_EQ(g.name, "Pat Parent");
    EXPECT_TRUE(g.consent_on_file);
    EXPECT_TRUE(g.photo_release);
    auto list2 = GET("/attendance/list/meeting/" + std::to_string(mtg.id), admin_token);
    EXPECT_EQ(list2.body.find("no photos"), std::string::npos);
    EXPECT_EQ(list2.body.find("consent missing"), std::string::npos);

    // Adults never get the badges
    attendance_repo->check_in(regular_member_id, "meeting", mtg.id);
    auto list3 = GET("/attendance/list/meeting/" + std::to_string(mtg.id), admin_token);
    EXPECT_EQ(list3.body.find("no photos"), std::string::npos);
}

TEST_F(IntegrationTest, GuardianDetailsOnlyForLeadsOrSelf) {
    Member kid;
    kid.first_name = "Teeny";
    kid.last_name = "Tim";
    kid.display_name = "Teeny T.";
    kid.fol_status = "tfol";
    auto k = member_repo->create(kid);
    MemberRepository::Guardian g;
    g.name = "Secret Guardian";
    member_repo->set_guardian(k.id, g);
    expect_contains(GET("/members/" + std::to_string(k.id) + "/view", chapter_lead_token), "Secret Guardian");
    EXPECT_EQ(GET("/members/" + std::to_string(k.id) + "/view", member_token).body.find("Secret Guardian"), std::string::npos);
}
