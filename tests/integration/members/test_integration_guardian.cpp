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
                  "first_name=Kiddo&last_name=Builder&fol_status=kfol&guardian_form=1&guardian_member_id=" + std::to_string(regular_member_id) +
                  "&consent_on_file=1&consent_date=2099-01-01&photo_release=1",
                  chapter_lead_token);
    auto g = member_repo->get_guardian(k.id);
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(g.member_id, regular_member_id);
    EXPECT_FALSE(g.member_name.empty());
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

// The guardian is another adult member of the club; guardian and consent are
// for KFOL/TFOL members only.
TEST_F(IntegrationTest, GuardianMustBeAnotherAdultMember) {
    Member kid; kid.first_name = "Kid"; kid.last_name = "One"; kid.display_name = "Kid O."; kid.fol_status = "kfol";
    auto k = member_repo->create(kid);
    Member teen; teen.first_name = "Teen"; teen.last_name = "Two"; teen.display_name = "Teen T."; teen.fol_status = "tfol";
    auto t = member_repo->create(teen);
    const std::string url = "/members/" + std::to_string(k.id);
    const std::string base = "first_name=Kid&last_name=One&fol_status=kfol&guardian_form=1&guardian_member_id=";

    auto self = POST(url, base + std::to_string(k.id), admin_token);
    EXPECT_EQ(self.code, 400);
    expect_contains(self, "own guardian");
    auto minor = POST(url, base + std::to_string(t.id), admin_token);
    EXPECT_EQ(minor.code, 400);
    expect_contains(minor, "adult member");
    EXPECT_EQ(POST(url, base + "999999", admin_token).code, 400);
    EXPECT_EQ(member_repo->get_guardian(k.id).member_id, 0);

    // The picker lists adults only, not the member themselves
    auto form = GET(url, admin_token);
    expect_contains(form, "name=\"guardian_member_id\"");
    expect_not_contains(form, ">Teen T.</option>");
    expect_not_contains(form, ">Kid O.</option>");
    expect_not_contains(form, "name=\"guardian_name\"");

    // Picking an adult member works and replaces typed-in details from before
    MemberRepository::Guardian old; old.name = "Typed Parent"; old.phone = "555";
    member_repo->set_guardian(k.id, old);
    expect_contains(GET(url, admin_token), "Typed Parent");
    EXPECT_EQ(POST(url, base + std::to_string(regular_member_id) + "&consent_on_file=1", admin_token).code, 200);
    auto g = member_repo->get_guardian(k.id);
    EXPECT_EQ(g.member_id, regular_member_id);
    EXPECT_EQ(g.name, "");
    expect_contains(GET(url + "/view", admin_token), "/members/" + std::to_string(regular_member_id) + "/view");

    // Now an adult: guardian and consent are cleared
    EXPECT_EQ(POST(url, "first_name=Kid&last_name=One&fol_status=afol&guardian_form=1&guardian_member_id=" +
                   std::to_string(regular_member_id) + "&consent_on_file=1&photo_release=1", admin_token).code, 200);
    g = member_repo->get_guardian(k.id);
    EXPECT_EQ(g.member_id, 0);
    EXPECT_FALSE(g.consent_on_file);
    EXPECT_FALSE(g.photo_release);
    expect_not_contains(GET(url + "/view", admin_token), ">Guardian</dt>");

    // Deleting the guardian member unlinks them
    MemberRepository::Guardian link; link.member_id = t.id;   // (teen as stand-in record to delete)
    member_repo->set_guardian(k.id, link);
    db->execute("DELETE FROM members WHERE id=" + std::to_string(t.id));
    EXPECT_EQ(member_repo->get_guardian(k.id).member_id, 0);
}
