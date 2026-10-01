// Regression tests for access-control and injection fixes.
#include "integration_test_base.hpp"

// A chapter lead (or moderator) must not be able to change LUG roles via the
// JSON member endpoint - previously {"role":"admin"} on themselves succeeded.
TEST_F(IntegrationTest, ChapterLeadCannotSelfPromoteViaJsonPut) {
    auto r = PUT("/members/" + std::to_string(chapter_lead_member_id),
                 R"({"role":"admin"})", chapter_lead_token);
    EXPECT_EQ(r.code, 403);
    auto m = member_repo->find_by_id(chapter_lead_member_id);
    ASSERT_TRUE(m);
    EXPECT_EQ(m->role, "chapter_lead");
}

// Form edit by a non-admin must leave the target's role untouched (this used
// to let a chapter lead demote an admin by posting role=member).
TEST_F(IntegrationTest, ChapterLeadCannotChangeRoleViaForm) {
    auto r = POST("/members/" + std::to_string(regular_member_id),
                  "first_name=Reg&last_name=User&role=moderator", chapter_lead_token);
    auto m = member_repo->find_by_id(regular_member_id);
    ASSERT_TRUE(m);
    EXPECT_EQ(m->role, "member");
}

// Permission to remove/toggle an attendance record must come from the entity
// the record belongs to, not from caller-supplied entity_type/entity_id.
TEST_F(IntegrationTest, AttendanceRemoveChecksRecordsOwnEntity) {
    Chapter ch;
    ch.name = "IDOR Chapter";
    ch.shorthand = "ID";
    auto chapter = chapter_repo->create(ch);
    chapter_member_repo->upsert(regular_member_id, chapter.id, "event_manager", admin_member_id);

    Meeting own;
    own.title = "Own Chapter Meeting";
    own.start_time = "2026-06-01T19:00:00";
    own.end_time = "2026-06-01T21:00:00";
    own.scope = "chapter";
    own.chapter_id = chapter.id;
    auto own_mtg = meeting_svc->create(own);

    Meeting other;
    other.title = "Someone Else's Meeting";
    other.start_time = "2026-06-02T19:00:00";
    other.end_time = "2026-06-02T21:00:00";
    other.scope = "lug_wide";
    auto other_mtg = meeting_svc->create(other);

    attendance_svc->check_in(admin_member_id, "meeting", other_mtg.id, "", false);
    auto rows = attendance_repo->find_by_entity("meeting", other_mtg.id);
    ASSERT_EQ(rows.size(), 1u);

    std::string spoofed = "entity_type=meeting&entity_id=" + std::to_string(own_mtg.id);
    auto t = POST("/attendance/admin/" + std::to_string(rows[0].id) + "/toggle-virtual",
                  spoofed + "&current=0", member_token);
    EXPECT_EQ(t.code, 403);
    auto r = POST("/attendance/admin/" + std::to_string(rows[0].id) + "/remove",
                  spoofed, member_token);
    EXPECT_EQ(r.code, 403);

    rows = attendance_repo->find_by_entity("meeting", other_mtg.id);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_FALSE(rows[0].is_virtual);
}

// The overview search used to be spliced into SQL: an apostrophe broke it
// and a crafted value could rewrite the query.
TEST_F(IntegrationTest, AttendanceOverviewSearchIsParameterized) {
    Member m;
    m.first_name = "Zed";
    m.last_name = "O'Brien";
    m.display_name = "Zed O'Brien";
    member_repo->create(m);

    auto r = GET("/attendance/overview?search=Brien", admin_token);
    EXPECT_EQ(r.code, 200);
    expect_contains(r, "Zed");

    auto apostrophe = GET("/attendance/overview?search=O%27Brien", admin_token);
    EXPECT_EQ(apostrophe.code, 200);
    expect_contains(apostrophe, "Zed");

    // Classic tautology injection must match nothing rather than everything.
    auto inj = GET("/attendance/overview?search=nomatch%27%20OR%20%271%27%3D%271", admin_token);
    EXPECT_EQ(inj.code, 200);
    EXPECT_EQ(inj.body.find("Zed"), std::string::npos);
}

// check_in() must report duplicates as "not new" - it used to rely on
// last_insert_rowid(), which SQLite does not reset when INSERT OR IGNORE
// skips a row, so a repeat check-in right after any insert looked new.
TEST_F(IntegrationTest, DuplicateCheckinIsReportedAsNotNew) {
    Meeting m;
    m.title = "Dup Checkin Meeting";
    m.start_time = "2026-06-03T19:00:00";
    m.end_time = "2026-06-03T21:00:00";
    m.scope = "lug_wide";
    auto mtg = meeting_svc->create(m);

    EXPECT_TRUE(attendance_repo->check_in(regular_member_id, "meeting", mtg.id, "", false));
    EXPECT_FALSE(attendance_repo->check_in(regular_member_id, "meeting", mtg.id, "", false));
    EXPECT_EQ(attendance_repo->count_by_entity("meeting", mtg.id), 1);

    EXPECT_FALSE(attendance_repo->set_virtual(999999, true));
}

// Login CSRF: a callback carrying a code must be rejected unless its state
// nonce matches the oauth_state cookie set by /auth/login in this browser.
TEST_F(IntegrationTest, AuthCallbackWithoutStateCookieIsRejected) {
    auto r = GET("/auth/callback?code=attacker-code&state=forged");
    EXPECT_TRUE(r.code == 302 || r.code == 307);
    EXPECT_NE(r.location.find("/login?error=failed"), std::string::npos);
}

TEST_F(IntegrationTest, AuthLoginSetsStateCookieMatchingState) {
    auto r = GET("/auth/login");
    size_t c = r.headers.find("oauth_state=");
    ASSERT_NE(c, std::string::npos);
    std::string nonce = r.headers.substr(c + 12, 64);
    EXPECT_NE(r.location.find("state=" + nonce), std::string::npos);
}

TEST_F(IntegrationTest, LogoutRejectsGet) {
    auto r = GET("/auth/logout", admin_token);
    EXPECT_NE(r.code, 302);
    EXPECT_TRUE(session_store->find(admin_token).has_value());
}

// Role changes must take effect on existing sessions immediately.
TEST_F(IntegrationTest, DemotedAdminLosesAccessWithoutRelogin) {
    EXPECT_EQ(GET("/settings", admin_token).code, 200);
    auto m = member_repo->find_by_id(admin_member_id);
    ASSERT_TRUE(m);
    m->role = "member";
    member_repo->update(*m);
    EXPECT_NE(GET("/settings", admin_token).code, 200);
}

TEST_F(IntegrationTest, MemberCannotViewOthersAttendanceDetail) {
    auto other = GET("/attendance/member/" + std::to_string(admin_member_id) + "/detail", member_token);
    EXPECT_EQ(other.code, 403);
    auto own = GET("/attendance/member/" + std::to_string(regular_member_id) + "/detail", member_token);
    EXPECT_EQ(own.code, 200);
}

TEST_F(IntegrationTest, AttendanceCountRejectsUnknownEntityType) {
    auto r = GET("/attendance/count/%22%3E%3Cscript%3E/1", member_token);
    EXPECT_EQ(r.body.find("<script>"), std::string::npos);
    EXPECT_NE(r.code, 200);
}

// Leads are appointed by admins, so a non-admin lead must not demote/remove one.
TEST_F(IntegrationTest, ChapterLeadCannotDemoteOrRemoveAnotherLead) {
    chapter_member_repo->upsert(regular_member_id, test_chapter_id, "lead", admin_member_id);
    std::string base = "/chapters/" + std::to_string(test_chapter_id) + "/members";
    auto demote = POST(base, "member_id=" + std::to_string(regular_member_id) + "&chapter_role=member",
                       chapter_lead_token);
    EXPECT_EQ(demote.code, 403);
    auto remove = http("DELETE", base + "/" + std::to_string(regular_member_id), "", chapter_lead_token);
    EXPECT_EQ(remove.code, 403);
    auto role = chapter_member_repo->get_chapter_role(regular_member_id, test_chapter_id);
    ASSERT_TRUE(role);
    EXPECT_EQ(*role, "lead");
}

TEST_F(IntegrationTest, SecurityHeadersPresent) {
    auto r = GET("/login");
    EXPECT_NE(r.headers.find("X-Frame-Options: DENY"), std::string::npos);
    EXPECT_NE(r.headers.find("X-Content-Type-Options: nosniff"), std::string::npos);
    EXPECT_NE(r.headers.find("frame-ancestors 'none'"), std::string::npos);
}

// Saving a member's profile calls set_chapter(); it used to delete and
// re-insert the chapter row as plain 'member', demoting chapter leads.
TEST_F(IntegrationTest, SetChapterKeepsExistingChapterRole) {
    member_repo->set_chapter(chapter_lead_member_id, test_chapter_id);
    auto role = chapter_member_repo->get_chapter_role(chapter_lead_member_id, test_chapter_id);
    ASSERT_TRUE(role);
    EXPECT_EQ(*role, "lead");
}

TEST_F(IntegrationTest, TransactionRollsBackUnlessCommitted) {
    db->execute("CREATE TABLE tx_probe (v INTEGER)");
    try {
        Transaction tx(*db);
        db->execute("INSERT INTO tx_probe VALUES (1)");
        {
            Transaction inner(*db); // nested scope joins the outer transaction
            db->execute("INSERT INTO tx_probe VALUES (2)");
            inner.commit();
        }
        throw std::runtime_error("boom");
    } catch (const std::runtime_error&) {}
    auto count = db->prepare("SELECT COUNT(*) FROM tx_probe");
    ASSERT_TRUE(count.step());
    EXPECT_EQ(count.col_int(0), 0);

    {
        Transaction tx(*db);
        db->execute("INSERT INTO tx_probe VALUES (3)");
        tx.commit();
    }
    auto after = db->prepare("SELECT COUNT(*) FROM tx_probe");
    ASSERT_TRUE(after.step());
    EXPECT_EQ(after.col_int(0), 1);
}

// A role an admin sets by hand is recorded as manual so Discord sync can't wipe it.
TEST_F(IntegrationTest, AdminRoleChangeIsMarkedManual) {
    EXPECT_EQ(member_repo->get_role_source(regular_member_id), "discord");
    Member upd;
    upd.role = "moderator";
    member_svc->update(regular_member_id, upd);
    EXPECT_EQ(member_repo->get_role_source(regular_member_id), "manual");
}

// Only SHA-256(token) is persisted, so a copied DB can't replay sessions.
TEST_F(IntegrationTest, SessionTokensAreStoredHashed) {
    auto raw = db->prepare("SELECT COUNT(*) FROM sessions WHERE token=?");
    raw.bind(1, admin_token);
    ASSERT_TRUE(raw.step());
    EXPECT_EQ(raw.col_int(0), 0);
    auto hashed = db->prepare("SELECT COUNT(*) FROM sessions WHERE token=? AND token_is_hash=1");
    hashed.bind(1, sha256_hex(admin_token));
    ASSERT_TRUE(hashed.step());
    EXPECT_EQ(hashed.col_int(0), 1);
}

// Sessions written raw by older versions keep working after upgrade.
TEST_F(IntegrationTest, LegacyRawSessionTokenIsMigrated) {
    db->execute("INSERT INTO sessions (token, member_id, role, expires_at) VALUES "
                "('legacy-raw-token', " + std::to_string(admin_member_id) + ", 'admin', '2099-01-01T00:00:00')");
    SessionStore fresh(*db); // startup conversion
    auto s = fresh.find("legacy-raw-token");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->member_id, admin_member_id);
}

// discord_user_id ends up in bot-authenticated Discord API paths; path
// characters must be rejected so it can't retarget a request.
TEST_F(IntegrationTest, PathLikeDiscordIdRejected) {
    std::string key = make_api_key("write");
    auto r = API_POST("/api/v1/members",
        R"({"discord_user_id":"../../channels/123","first_name":"Evil","last_name":"Id"})", key);
    EXPECT_NE(r.code, 201);
    EXPECT_FALSE(member_repo->find_by_discord_id("../../channels/123").has_value());
}

// A bare CR in a title must not survive into the public iCal feed, where some
// clients treat it as a line break (property injection).
TEST_F(IntegrationTest, IcalEscapesBareCarriageReturn) {
    Meeting m;
    m.title = "Bad\rX-INJECTED:1";
    m.start_time = "2026-07-01T19:00:00";
    m.end_time = "2026-07-01T21:00:00";
    m.scope = "lug_wide";
    meeting_svc->create(m);
    auto r = GET("/calendar.ics");
    EXPECT_EQ(r.code, 200);
    EXPECT_EQ(r.body.find("\rX-INJECTED"), std::string::npos);
    EXPECT_NE(r.body.find("X-INJECTED"), std::string::npos); // still present, escaped
}

// Demoting a chapter lead via the API needs admin scope, same as appointing one.
TEST_F(IntegrationTest, WriteScopeCannotDemoteChapterLead) {
    std::string key = make_api_key("write");
    auto r = API_PUT("/api/v1/chapter-members/" + std::to_string(chapter_lead_member_id) + "/" +
                     std::to_string(test_chapter_id), R"({"chapter_role":"member"})", key);
    EXPECT_EQ(r.code, 403);
    auto p = API_POST("/api/v1/chapter-members",
        R"({"member_id":)" + std::to_string(chapter_lead_member_id) + R"(,"chapter_id":)" +
        std::to_string(test_chapter_id) + R"(,"chapter_role":"event_manager"})", key);
    EXPECT_EQ(p.code, 403);
    auto role = chapter_member_repo->get_chapter_role(chapter_lead_member_id, test_chapter_id);
    ASSERT_TRUE(role);
    EXPECT_EQ(*role, "lead");
}

// Account takeover: a chapter lead must not be able to link a Discord account
// (e.g. their own alt) to an admin's member record.
TEST_F(IntegrationTest, ChapterLeadCannotLinkDiscordToAdmin) {
    auto admin_before = member_repo->find_by_id(admin_member_id);
    ASSERT_TRUE(admin_before);
    PendingDiscordMatch p;
    p.discord_user_id = "999000999000999000";
    p.discord_username = "alt";
    p.discord_display_name = "Alt";
    auto pending = pending_discord_match_repo->create(p);

    auto r = POST("/settings/discord-matches/" + std::to_string(pending.id) + "/link",
                  "member_id=" + std::to_string(admin_member_id), chapter_lead_token);
    EXPECT_EQ(r.code, 403);
    auto admin_after = member_repo->find_by_id(admin_member_id);
    EXPECT_EQ(admin_after->discord_user_id, admin_before->discord_user_id);
}
