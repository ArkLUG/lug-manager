#include "integration_test_base.hpp"







// ═══════════════════════════════════════════════════════════════════════════
// Dashboard
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, MemberCanViewDashboard) {
    auto r = GET("/dashboard", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCanViewMeetings) {
    auto r = GET("/meetings", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCanViewEvents) {
    auto r = GET("/events", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCanViewCalendar) {
    auto r = GET("/calendar.ics");
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCanViewAttendance) {
    auto r = GET("/attendance", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "My Attendance");
}

TEST_F(IntegrationTest, MemberCanViewMembersPage) {
    auto r = GET("/members", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCanViewChaptersPage) {
    auto r = GET("/chapters", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCannotCreateEvent) {
    auto r = POST("/events",
        "title=Hacked&start_time=2026-06-15&end_time=2026-06-17&scope=lug_wide",
        member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, MemberCannotCancelMeeting) {
    Meeting m;
    m.title = "Cancel Guard";
    m.start_time = "2026-05-01T19:00:00";
    m.end_time = "2026-05-01T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    auto r = POST("/meetings/" + std::to_string(mtg.id) + "/cancel", "", member_token);
    EXPECT_NE(r.code, 200);
    // Meeting should still exist
    EXPECT_TRUE(meeting_repo->find_by_id(mtg.id).has_value());
}

TEST_F(IntegrationTest, MemberCannotCancelEvent) {
    LugEvent e;
    e.title = "Cancel Guard Event";
    e.start_time = "2026-06-01T00:00:00";
    e.end_time = "2026-06-02T00:00:00";
    e.status = "confirmed";
    e.scope = "lug_wide";
    auto ev = event_svc->create(e);

    auto r = POST("/events/" + std::to_string(ev.id) + "/cancel", "", member_token);
    EXPECT_NE(r.code, 200);
    EXPECT_TRUE(event_repo->find_by_id(ev.id).has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// 404 / not found
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(IntegrationTest, PIIHiddenForMembers) {
    // Set email on admin member
    auto admin = member_repo->find_by_id(admin_member_id);
    ASSERT_TRUE(admin.has_value());
    admin->email = "secret@example.com";
    member_repo->update(*admin);

    // Member can't see emails in datatable
    auto r = POST("/api/members/datatable", "draw=1&start=0&length=25&search=", member_token);
    EXPECT_EQ(r.code, 200);
    expect_not_contains(r, "secret@example.com");
}

TEST_F(IntegrationTest, PIIVisibleForAdmin) {
    auto admin = member_repo->find_by_id(admin_member_id);
    ASSERT_TRUE(admin.has_value());
    admin->email = "visible@example.com";
    member_repo->update(*admin);

    auto r = POST("/api/members/datatable", "draw=1&start=0&length=25&search=", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "visible@example.com");
}

TEST_F(IntegrationTest, LeadCannotPromoteLead) {
    // Create a chapter and make the regular member a lead
    Chapter ch;
    ch.name = "Lead Test Chapter";
    auto created_ch = chapter_repo->create(ch);

    chapter_member_repo->upsert(regular_member_id, created_ch.id, "lead", admin_member_id);

    // Create another member to promote
    Member m3;
    m3.discord_user_id = "promote-target";
    m3.discord_username = "target";
    m3.display_name = "Target U.";
    auto target = member_repo->create(m3);

    // Lead tries to add another lead — should be forbidden (admin-only)
    auto r = POST("/chapters/" + std::to_string(created_ch.id) + "/lead",
        "member_id=" + std::to_string(target.id), member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, LeadCannotAssignLeadRole) {
    // Create a chapter and make the regular member a lead
    Chapter ch;
    ch.name = "Role Guard Chapter";
    auto created_ch = chapter_repo->create(ch);
    chapter_member_repo->upsert(regular_member_id, created_ch.id, "lead", admin_member_id);

    Member m3;
    m3.discord_user_id = "role-guard-target";
    m3.discord_username = "role_guard";
    m3.display_name = "Guard U.";
    auto target = member_repo->create(m3);

    // Lead tries to set chapter_role=lead via members endpoint — should be forbidden
    auto r = POST("/chapters/" + std::to_string(created_ch.id) + "/members",
        "member_id=" + std::to_string(target.id) + "&chapter_role=lead", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, EventManagerCanEditChapterEvent) {
    // Create a chapter and make the regular member an event_manager
    Chapter ch;
    ch.name = "EM Edit Chapter";
    auto created_ch = chapter_repo->create(ch);
    chapter_member_repo->upsert(regular_member_id, created_ch.id, "event_manager", admin_member_id);

    // Create an event in that chapter
    LugEvent e;
    e.title = "EM Editable Event";
    e.start_time = "2026-08-01T00:00:00";
    e.end_time = "2026-08-02T00:00:00";
    e.scope = "chapter";
    e.chapter_id = created_ch.id;
    auto created_ev = event_svc->create(e);

    // Event manager can access the edit form
    auto r = GET("/events/" + std::to_string(created_ev.id) + "/edit", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "EM Editable Event");
}

TEST_F(IntegrationTest, EventManagerCanCancelChapterEvent) {
    Chapter ch;
    ch.name = "EM Cancel Chapter";
    auto created_ch = chapter_repo->create(ch);
    chapter_member_repo->upsert(regular_member_id, created_ch.id, "event_manager", admin_member_id);

    LugEvent e;
    e.title = "EM Cancelable Event";
    e.start_time = "2026-09-01T00:00:00";
    e.end_time = "2026-09-02T00:00:00";
    e.scope = "chapter";
    e.chapter_id = created_ch.id;
    auto created_ev = event_svc->create(e);

    auto r = POST("/events/" + std::to_string(created_ev.id) + "/cancel", "", member_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, MemberCannotEditOtherChapterEvent) {
    // Create a chapter where member has NO role
    Chapter ch;
    ch.name = "No Access Chapter";
    auto created_ch = chapter_repo->create(ch);

    LugEvent e;
    e.title = "Forbidden Event";
    e.start_time = "2026-10-01T00:00:00";
    e.end_time = "2026-10-02T00:00:00";
    e.scope = "chapter";
    e.chapter_id = created_ch.id;
    auto created_ev = event_svc->create(e);

    auto r = GET("/events/" + std::to_string(created_ev.id) + "/edit", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, EventManagerCanEditChapterMeeting) {
    Chapter ch;
    ch.name = "EM Meeting Chapter";
    auto created_ch = chapter_repo->create(ch);
    chapter_member_repo->upsert(regular_member_id, created_ch.id, "event_manager", admin_member_id);

    Meeting m;
    m.title = "EM Editable Meeting";
    m.start_time = "2026-08-15T19:00:00";
    m.end_time = "2026-08-15T21:00:00";
    m.scope = "chapter";
    m.chapter_id = created_ch.id;
    auto created_m = meeting_svc->create(m);

    auto r = GET("/meetings/" + std::to_string(created_m.id) + "/edit", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "EM Editable Meeting");
}

TEST_F(IntegrationTest, SuppressFlagsInEventCreate) {
    // Create event with suppress flags via form POST
    auto r = POST("/events",
        "title=Suppressed+Event&start_time=2025-01-15&end_time=2025-01-16"
        "&scope=lug_wide&suppress_discord=on&suppress_calendar=on",
        admin_token);
    EXPECT_EQ(r.code, 200);

    // Verify the flags were saved
    auto all = event_repo->find_all();
    auto it = std::find_if(all.begin(), all.end(),
        [](const LugEvent& e) { return e.title == "Suppressed Event"; });
    ASSERT_NE(it, all.end());
    EXPECT_TRUE(it->suppress_discord);
    EXPECT_TRUE(it->suppress_calendar);
}

TEST_F(IntegrationTest, NotesInEventCreate) {
    auto r = POST("/events",
        "title=Notes+Event&start_time=2026-11-15&end_time=2026-11-16"
        "&scope=lug_wide&notes=Test+notes+here",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto all = event_repo->find_all();
    auto it = std::find_if(all.begin(), all.end(),
        [](const LugEvent& e) { return e.title == "Notes Event"; });
    ASSERT_NE(it, all.end());
    EXPECT_EQ(it->notes, "Test notes here");
}

TEST_F(IntegrationTest, PerkLevelsAdminOnly) {
    auto r = GET("/settings/perks", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, PerkLevelsAdminCanAccess) {
    auto r = GET("/settings/perks", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Perk Levels");
}

TEST_F(IntegrationTest, PerkLevelCreateAndList) {
    auto r = POST("/settings/perks",
        "name=Bronze&meeting_attendance_required=3&event_attendance_required=1&sort_order=1"
        "&min_fol_status=tfol",
        admin_token);
    EXPECT_TRUE(r.code == 200 || r.code == 302 || r.code == 307);

    auto levels = perk_level_repo->find_all();
    ASSERT_GE(levels.size(), 1u);
    auto it = std::find_if(levels.begin(), levels.end(),
        [](const PerkLevel& p) { return p.name == "Bronze"; });
    ASSERT_NE(it, levels.end());
    EXPECT_EQ(it->meeting_attendance_required, 3);
    EXPECT_EQ(it->event_attendance_required, 1);
    EXPECT_EQ(it->min_fol_status, "tfol");
}

TEST_F(IntegrationTest, PerkLevelEditForm) {
    // Create a perk level first
    PerkLevel p;
    p.name = "Editable Tier";
    p.sort_order = 1;
    p.min_fol_status = "kfol";
    auto created = perk_level_repo->create(p);

    // GET edit form
    auto r = GET("/settings/perks/" + std::to_string(created.id) + "/edit", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Edit Perk Level");
    expect_contains(r, "Editable Tier");
}

TEST_F(IntegrationTest, PerkLevelEditFormNonAdmin) {
    PerkLevel p;
    p.name = "Forbidden Tier";
    p.sort_order = 1;
    auto created = perk_level_repo->create(p);

    auto r = GET("/settings/perks/" + std::to_string(created.id) + "/edit", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, PerkLevelUpdate) {
    PerkLevel p;
    p.name = "Update Me";
    p.meeting_attendance_required = 1;
    p.min_fol_status = "kfol";
    p.sort_order = 1;
    auto created = perk_level_repo->create(p);

    auto r = PUT("/settings/perks/" + std::to_string(created.id),
        "name=Updated+Tier&meeting_attendance_required=5&event_attendance_required=2"
        "&min_fol_status=afol&sort_order=3",
        admin_token);
    EXPECT_TRUE(r.code == 200 || r.code == 302 || r.code == 307);

    auto found = perk_level_repo->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Updated Tier");
    EXPECT_EQ(found->meeting_attendance_required, 5);
    EXPECT_EQ(found->event_attendance_required, 2);
    EXPECT_EQ(found->min_fol_status, "afol");
    EXPECT_EQ(found->sort_order, 3);
}

TEST_F(IntegrationTest, AttendanceOverviewYearFilter) {
    auto r = GET("/attendance/overview?year=2026", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "2026");
    expect_contains(r, "Attendance Overview");
}

TEST_F(IntegrationTest, AttendanceOverviewSearch) {
    auto r = GET("/attendance/overview?year=2026&search=Admin", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Admin U.");
}

TEST_F(IntegrationTest, AttendanceOverviewHideInactive) {
    auto r = GET("/attendance/overview?year=2026&hide_inactive=1", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Attendance Overview");
}

TEST_F(IntegrationTest, AttendanceOverviewSort) {
    auto r = GET("/attendance/overview?year=2026&sort=total&dir=desc", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Attendance Overview");
}

TEST_F(IntegrationTest, AttendanceOverviewPagination) {
    auto r = GET("/attendance/overview?year=2026&page=1", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Page 1");
}

// ═══════════════════════════════════════════════════════════════════════════
// Comprehensive Permission Tests
// ═══════════════════════════════════════════════════════════════════════════

// --- PII visibility ---

TEST_F(IntegrationTest, ChapterLeadCanSeePII) {
    auto admin = member_repo->find_by_id(admin_member_id);
    admin->email = "pii_test@example.com";
    member_repo->update(*admin);

    auto r = POST("/api/members/datatable", "draw=1&start=0&length=25&search=", chapter_lead_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "pii_test@example.com");
}

TEST_F(IntegrationTest, MemberCannotSeePIIWhenNotPublic) {
    auto admin = member_repo->find_by_id(admin_member_id);
    admin->email = "hidden_pii@example.com";
    admin->pii_sharing = "none";
    member_repo->update(*admin);

    auto r = POST("/api/members/datatable", "draw=1&start=0&length=25&search=", member_token);
    EXPECT_EQ(r.code, 200);
    expect_not_contains(r, "hidden_pii@example.com");
}

TEST_F(IntegrationTest, MemberCanSeePIIWhenPublic) {
    // Set admin member's PII to all
    auto admin = member_repo->find_by_id(admin_member_id);
    admin->email = "public_pii@example.com";
    admin->pii_sharing = "all";
    member_repo->update(*admin);

    // Regular member should now see the email
    auto r = POST("/api/members/datatable", "draw=1&start=0&length=25&search=", member_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "public_pii@example.com");

    // Clean up
    admin->pii_sharing = "none";
    member_repo->update(*admin);
}

TEST_F(IntegrationTest, MemberCreateWithContactFields) {
    auto r = POST("/members",
        "first_name=Contact&last_name=Test&discord_user_id=contact-int-test"
        "&discord_username=contacttest&role=member"
        "&phone=(555)+999-0000&address_line1=456+Oak+St&city=Conway&state=AR&zip=72032"
        "&pii_sharing=all",
        admin_token);
    EXPECT_EQ(r.code, 200);

    auto all = member_repo->find_all();
    auto it = std::find_if(all.begin(), all.end(),
        [](const Member& m) { return m.discord_user_id == "contact-int-test"; });
    ASSERT_NE(it, all.end());
    EXPECT_EQ(it->phone, "(555) 999-0000");
    EXPECT_EQ(it->address_line1, "456 Oak St");
    EXPECT_EQ(it->city, "Conway");
    EXPECT_EQ(it->state, "AR");
    EXPECT_EQ(it->zip, "72032");
    EXPECT_EQ(it->pii_sharing, "all");
}

TEST_F(IntegrationTest, MemberEditFormAdminOnly) {
    auto r = GET("/members/" + std::to_string(regular_member_id), member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, AdminCanAccessMemberEditForm) {
    auto r = GET("/members/" + std::to_string(regular_member_id), admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Edit Member");
}

// --- Chapter lead restrictions ---

TEST_F(IntegrationTest, ChapterLeadCanManageChapterMembers) {
    auto r = GET("/chapters/" + std::to_string(test_chapter_id) + "/members", chapter_lead_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, ChapterLeadCanAddEventManager) {
    Member new_m;
    new_m.discord_user_id = "new-em-perm-test";
    new_m.discord_username = "new_em";
    new_m.display_name = "NewEM U.";
    auto created = member_repo->create(new_m);

    auto r = POST("/chapters/" + std::to_string(test_chapter_id) + "/members",
        "member_id=" + std::to_string(created.id) + "&chapter_role=event_manager",
        chapter_lead_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, ChapterLeadCannotAddLead) {
    Member new_m;
    new_m.discord_user_id = "lead-deny-test";
    new_m.discord_username = "lead_deny";
    new_m.display_name = "LeadDeny U.";
    auto created = member_repo->create(new_m);

    auto r = POST("/chapters/" + std::to_string(test_chapter_id) + "/lead",
        "member_id=" + std::to_string(created.id), chapter_lead_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, ChapterLeadCannotSetLeadRoleViaMembersEndpoint) {
    Member new_m;
    new_m.discord_user_id = "lead-role-deny";
    new_m.discord_username = "role_deny";
    new_m.display_name = "RoleDeny U.";
    auto created = member_repo->create(new_m);

    auto r = POST("/chapters/" + std::to_string(test_chapter_id) + "/members",
        "member_id=" + std::to_string(created.id) + "&chapter_role=lead",
        chapter_lead_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, ChapterLeadCannotEditChapterSettings) {
    auto r = PUT("/chapters/" + std::to_string(test_chapter_id),
        "name=Hacked+Chapter", chapter_lead_token);
    EXPECT_EQ(r.code, 403);
}

// --- Event manager permissions ---

TEST_F(IntegrationTest, EventManagerCanCreateChapterEvent) {
    auto r = POST("/events",
        "title=EM+Perm+Event&start_time=2026-09-01&end_time=2026-09-02"
        "&scope=chapter&chapter_id=" + std::to_string(test_chapter_id) +
        "&suppress_discord=on",
        event_manager_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, EventManagerCanEditChapterEvent2) {
    LugEvent e;
    e.title = "EM Edit Test";
    e.start_time = "2026-09-10T00:00:00";
    e.end_time = "2026-09-11T00:00:00";
    e.scope = "chapter";
    e.chapter_id = test_chapter_id;
    e.suppress_discord = true;
    auto created = event_svc->create(e);

    auto r = GET("/events/" + std::to_string(created.id) + "/edit", event_manager_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, EventManagerCannotEditOtherChapter) {
    Chapter other;
    other.name = "Other Chapter";
    other.discord_announcement_channel_id = "";
    auto other_ch = chapter_repo->create(other);

    LugEvent e;
    e.title = "Other Chapter Event";
    e.start_time = "2026-09-15T00:00:00";
    e.end_time = "2026-09-16T00:00:00";
    e.scope = "chapter";
    e.chapter_id = other_ch.id;
    e.suppress_discord = true;
    auto created = event_svc->create(e);

    auto r = GET("/events/" + std::to_string(created.id) + "/edit", event_manager_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, EventManagerCanCreateChapterMeeting) {
    auto r = POST("/meetings",
        "title=EM+Perm+Meeting&start_time=2026-09-20T19:00:00&end_time=2026-09-20T21:00:00"
        "&scope=chapter&chapter_id=" + std::to_string(test_chapter_id) +
        "&suppress_discord=on",
        event_manager_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, EventManagerCanCancelChapterMeeting) {
    Meeting m;
    m.title = "EM Cancel Meeting";
    m.start_time = "2026-09-25T19:00:00";
    m.end_time = "2026-09-25T21:00:00";
    m.scope = "chapter";
    m.chapter_id = test_chapter_id;
    m.suppress_discord = true;
    auto created = meeting_svc->create(m);

    auto r = POST("/meetings/" + std::to_string(created.id) + "/cancel", "", event_manager_token);
    EXPECT_EQ(r.code, 200);
}

TEST_F(IntegrationTest, RegularMemberCannotCreateEvent) {
    auto r = POST("/events",
        "title=Blocked&start_time=2026-10-01&end_time=2026-10-02&scope=lug_wide",
        member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, RegularMemberCannotEditEvent) {
    LugEvent e;
    e.title = "Blocked Edit";
    e.start_time = "2026-10-05T00:00:00";
    e.end_time = "2026-10-06T00:00:00";
    e.scope = "chapter";
    e.chapter_id = test_chapter_id;
    e.suppress_discord = true;
    auto created = event_svc->create(e);

    auto r = GET("/events/" + std::to_string(created.id) + "/edit", member_token);
    EXPECT_EQ(r.code, 403);
}

// --- Admin-only routes ---

TEST_F(IntegrationTest, MemberCannotAccessAttendanceOverview2) {
    auto r = GET("/attendance/overview", member_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, ChapterLeadCannotAccessSettings) {
    auto r = GET("/settings", chapter_lead_token);
    EXPECT_TRUE(r.code == 302 || r.code == 307);
}

TEST_F(IntegrationTest, ChapterLeadCannotAccessPerks) {
    auto r = GET("/settings/perks", chapter_lead_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, EventManagerCannotAccessAdmin) {
    auto r1 = GET("/settings", event_manager_token);
    EXPECT_TRUE(r1.code == 302 || r1.code == 307);

    auto r2 = GET("/attendance/overview", event_manager_token);
    EXPECT_EQ(r2.code, 403);

    auto r3 = GET("/settings/perks", event_manager_token);
    EXPECT_EQ(r3.code, 403);
}

// --- View-only access for all authenticated ---

TEST_F(IntegrationTest, AllUsersCanViewDashboard) {
    EXPECT_EQ(GET("/dashboard", member_token).code, 200);
    EXPECT_EQ(GET("/dashboard", chapter_lead_token).code, 200);
    EXPECT_EQ(GET("/dashboard", event_manager_token).code, 200);
}

TEST_F(IntegrationTest, AllUsersCanViewEvents) {
    EXPECT_EQ(GET("/events", member_token).code, 200);
    EXPECT_EQ(GET("/events", chapter_lead_token).code, 200);
    EXPECT_EQ(GET("/events", event_manager_token).code, 200);
}

TEST_F(IntegrationTest, AllUsersCanViewMeetings) {
    EXPECT_EQ(GET("/meetings", member_token).code, 200);
    EXPECT_EQ(GET("/meetings", chapter_lead_token).code, 200);
    EXPECT_EQ(GET("/meetings", event_manager_token).code, 200);
}

TEST_F(IntegrationTest, AllUsersCanViewChapters) {
    EXPECT_EQ(GET("/chapters", member_token).code, 200);
    EXPECT_EQ(GET("/chapters", chapter_lead_token).code, 200);
    EXPECT_EQ(GET("/chapters", event_manager_token).code, 200);
}

TEST_F(IntegrationTest, AllUsersCanViewMembers) {
    EXPECT_EQ(GET("/members", member_token).code, 200);
    EXPECT_EQ(GET("/members", chapter_lead_token).code, 200);
    EXPECT_EQ(GET("/members", event_manager_token).code, 200);
}

TEST_F(IntegrationTest, AllUsersCanViewPersonalAttendance) {
    EXPECT_EQ(GET("/attendance", member_token).code, 200);
    EXPECT_EQ(GET("/attendance", chapter_lead_token).code, 200);
    EXPECT_EQ(GET("/attendance", event_manager_token).code, 200);
}

// --- Convert-to-meeting admin-only ---

TEST_F(IntegrationTest, EventManagerCannotConvertToMeeting) {
    LugEvent e;
    e.title = "No Convert";
    e.start_time = "2026-11-01T00:00:00";
    e.end_time = "2026-11-02T00:00:00";
    e.scope = "chapter";
    e.chapter_id = test_chapter_id;
    e.suppress_discord = true;
    auto created = event_svc->create(e);

    auto r = POST("/events/" + std::to_string(created.id) + "/convert-to-meeting", "", event_manager_token);
    EXPECT_EQ(r.code, 403);
}

TEST_F(IntegrationTest, MemberCreateWithoutDiscord) {
    auto r = POST("/members",
        "first_name=Kid&last_name=Member&fol_status=kfol&role=member",
        admin_token);
    EXPECT_EQ(r.code, 200);

    // Verify member was created without discord ID
    auto all = member_repo->find_all();
    auto it = std::find_if(all.begin(), all.end(),
        [](const Member& m) { return m.first_name == "Kid" && m.last_name == "Member"; });
    ASSERT_NE(it, all.end());
    EXPECT_TRUE(it->discord_user_id.empty());
    EXPECT_EQ(it->fol_status, "kfol");
}

