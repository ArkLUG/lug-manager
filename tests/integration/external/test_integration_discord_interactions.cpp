// Integration tests for POST /discord/interactions — the inbound Discord webhook
// used to resolve pending member matches via an in-Discord button + modal.
//
// A fresh Ed25519 keypair is generated per-process and injected as `config.discord_public_key`
// before the fixture boots the app, so every request here can be genuinely signed exactly the
// way Discord signs real interactions (timestamp + raw body, Ed25519 over the concatenation).
#include "integration_test_base.hpp"
#include <openssl/evp.h>
#include <vector>

namespace {

std::string to_hex(const unsigned char* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += digits[(data[i] >> 4) & 0xF];
        out += digits[data[i] & 0xF];
    }
    return out;
}

} // namespace

class DiscordInteractionsTest : public IntegrationTest {
protected:
    EVP_PKEY* signing_key = nullptr;
    std::string public_key_hex;

    // Runs BEFORE IntegrationTest::SetUp() finishes building Services (it calls
    // config.discord_public_key before app boot), by generating the keypair and
    // stashing the public key into `config` ahead of the base fixture's own SetUp.
    void SetUp() override {
        EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
        EVP_PKEY_keygen_init(pctx);
        EVP_PKEY_keygen(pctx, &signing_key);
        EVP_PKEY_CTX_free(pctx);

        unsigned char pub[32];
        size_t pub_len = sizeof(pub);
        EVP_PKEY_get_raw_public_key(signing_key, pub, &pub_len);
        public_key_hex = to_hex(pub, pub_len);

        // config.discord_public_key must be set before IntegrationTest::SetUp() builds
        // Services{...} with it — override config assignment happens first thing here,
        // then delegate to the base fixture.
        config.discord_public_key = public_key_hex;
        IntegrationTest::SetUp();
    }

    void TearDown() override {
        IntegrationTest::TearDown();
        if (signing_key) EVP_PKEY_free(signing_key);
    }

    std::string sign_hex(const std::string& message) {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, signing_key);
        size_t sig_len = 0;
        EVP_DigestSign(ctx, nullptr, &sig_len,
                        reinterpret_cast<const unsigned char*>(message.data()), message.size());
        std::vector<unsigned char> sig(sig_len);
        EVP_DigestSign(ctx, sig.data(), &sig_len,
                        reinterpret_cast<const unsigned char*>(message.data()), message.size());
        EVP_MD_CTX_free(ctx);
        return to_hex(sig.data(), sig_len);
    }

    // Posts a signed interaction body to /discord/interactions.
    Response post_interaction(const std::string& body, const std::string& timestamp = "1700000000") {
        std::string sig = sign_hex(timestamp + body);
        return http("POST", "/discord/interactions", body, "", false, "", true, {
            "X-Signature-Ed25519: " + sig,
            "X-Signature-Timestamp: " + timestamp
        });
    }

    // Posts an interaction body signed by a DIFFERENT (wrong) keypair.
    Response post_interaction_wrong_key(const std::string& body, const std::string& timestamp = "1700000000") {
        EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
        EVP_PKEY_keygen_init(pctx);
        EVP_PKEY* wrong_key = nullptr;
        EVP_PKEY_keygen(pctx, &wrong_key);
        EVP_PKEY_CTX_free(pctx);

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, wrong_key);
        std::string message = timestamp + body;
        size_t sig_len = 0;
        EVP_DigestSign(ctx, nullptr, &sig_len,
                        reinterpret_cast<const unsigned char*>(message.data()), message.size());
        std::vector<unsigned char> sig(sig_len);
        EVP_DigestSign(ctx, sig.data(), &sig_len,
                        reinterpret_cast<const unsigned char*>(message.data()), message.size());
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(wrong_key);

        return http("POST", "/discord/interactions", body, "", false, "", true, {
            "X-Signature-Ed25519: " + to_hex(sig.data(), sig.size()),
            "X-Signature-Timestamp: " + timestamp
        });
    }

    // Seeds a discord-less member + unresolved pending match, returns the pending row id.
    int64_t seed_pending_match(const std::string& discord_user_id, const std::string& discord_username,
                                const std::string& display_name) {
        Member m;
        m.first_name = display_name;
        m.last_name = "Seed";
        m.display_name = display_name;
        m.role = "member";
        auto created = member_repo->create(m);

        PendingDiscordMatch p;
        p.discord_user_id      = discord_user_id;
        p.discord_username     = discord_username;
        p.discord_display_name = display_name;
        p.suggested_member_id  = created.id;
        auto row = pending_discord_match_repo->create(p);
        return row.id;
    }

    void set_authorized_role(const std::string& role_id) {
        settings_repo->set("discord_matches_authorized_role_ids", role_id);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// PING / signature verification
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(DiscordInteractionsTest, PingRespondsWithPong) {
    auto r = post_interaction(R"({"type":1})");
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.body.find("\"type\":1"), std::string::npos);
}

TEST_F(DiscordInteractionsTest, InvalidSignatureRejected) {
    auto r = post_interaction_wrong_key(R"({"type":1})");
    EXPECT_EQ(r.code, 401);
}

TEST_F(DiscordInteractionsTest, MissingSignatureHeadersRejected) {
    auto r = http("POST", "/discord/interactions", R"({"type":1})", "", false, "", true, {});
    EXPECT_EQ(r.code, 401);
}

TEST_F(DiscordInteractionsTest, TamperedBodyRejected) {
    std::string real_body = R"({"type":1})";
    std::string timestamp = "1700000000";
    std::string sig = sign_hex(timestamp + real_body);
    // Send a different body than what was signed.
    auto r = http("POST", "/discord/interactions", R"({"type":2})", "", false, "", true, {
        "X-Signature-Ed25519: " + sig,
        "X-Signature-Timestamp: " + timestamp
    });
    EXPECT_EQ(r.code, 401);
}

// ═══════════════════════════════════════════════════════════════════════════
// MESSAGE_COMPONENT (button click)
// ═══════════════════════════════════════════════════════════════════════════

TEST_F(DiscordInteractionsTest, UnauthorizedButtonClickDenied) {
    int64_t id = seed_pending_match("discord-btn-1", "btnuser", "Button Person");
    set_authorized_role("role-allowed-999"); // clicker won't have this role

    std::string body = R"({"type":3,"member":{"roles":["role-other"]},)"
                        R"("data":{"custom_id":"discord_match_resolve:)" + std::to_string(id) + R"("}})";
    auto r = post_interaction(body);
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.body.find("permission"), std::string::npos);
    EXPECT_NE(r.body.find("\"flags\":64"), std::string::npos);

    // Row must still be unresolved.
    auto pending = pending_discord_match_repo->find_by_id(id);
    ASSERT_TRUE(pending.has_value());
    EXPECT_TRUE(pending->resolved_at.empty());
}

TEST_F(DiscordInteractionsTest, AuthorizedButtonClickOpensModal) {
    int64_t id = seed_pending_match("discord-btn-2", "btnuser2", "Modal Person");
    set_authorized_role("role-allowed-999");

    std::string body = R"({"type":3,"member":{"roles":["role-allowed-999"]},)"
                        R"("data":{"custom_id":"discord_match_resolve:)" + std::to_string(id) + R"("}})";
    auto r = post_interaction(body);
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.body.find("\"type\":9"), std::string::npos); // MODAL response type
    EXPECT_NE(r.body.find("discord_match_modal:" + std::to_string(id)), std::string::npos);
}

TEST_F(DiscordInteractionsTest, ClickOnAlreadyResolvedRowDenied) {
    int64_t id = seed_pending_match("discord-btn-3", "btnuser3", "Resolved Person");
    set_authorized_role("role-allowed-999");
    pending_discord_match_repo->mark_resolved(id, "linked", admin_member_id);

    std::string body = R"({"type":3,"member":{"roles":["role-allowed-999"]},)"
                        R"("data":{"custom_id":"discord_match_resolve:)" + std::to_string(id) + R"("}})";
    auto r = post_interaction(body);
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.body.find("already"), std::string::npos);
    EXPECT_EQ(r.body.find("\"type\":9"), std::string::npos); // no modal reopened
}

// ═══════════════════════════════════════════════════════════════════════════
// MODAL_SUBMIT
// ═══════════════════════════════════════════════════════════════════════════

static std::string modal_submit_body(int64_t pending_id, const std::string& role_id, const std::string& value) {
    return R"({"type":5,"member":{"roles":[")" + role_id + R"("]},)"
           R"("data":{"custom_id":"discord_match_modal:)" + std::to_string(pending_id) + R"(",)"
           R"("components":[{"type":1,"components":[{"type":4,"custom_id":"member_id_or_new","value":")" + value + R"("}]}]}})";
}

TEST_F(DiscordInteractionsTest, ModalSubmitLinksExistingMemberById) {
    int64_t pending_id = seed_pending_match("discord-modal-1", "modaluser1", "Link Person");
    set_authorized_role("role-allowed-999");

    // A separate existing member to link to (distinct from the auto-seeded suggestion).
    Member target;
    target.first_name = "Target";
    target.last_name = "Existing";
    target.display_name = "Target Existing";
    target.role = "member";
    auto created_target = member_repo->create(target);

    auto r = post_interaction(modal_submit_body(pending_id, "role-allowed-999", std::to_string(created_target.id)));
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.body.find("Linked"), std::string::npos);

    auto pending = pending_discord_match_repo->find_by_id(pending_id);
    ASSERT_TRUE(pending.has_value());
    EXPECT_FALSE(pending->resolved_at.empty());
    EXPECT_EQ(pending->resolved_action, "linked");
    EXPECT_EQ(pending->resolved_member_id, created_target.id);

    auto linked = member_repo->find_by_id(created_target.id);
    ASSERT_TRUE(linked.has_value());
    EXPECT_EQ(linked->discord_user_id, "discord-modal-1");
}

TEST_F(DiscordInteractionsTest, ModalSubmitBlankCreatesNewMember) {
    int64_t pending_id = seed_pending_match("discord-modal-2", "modaluser2", "New Person");
    set_authorized_role("role-allowed-999");

    auto r = post_interaction(modal_submit_body(pending_id, "role-allowed-999", ""));
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.body.find("Created"), std::string::npos);

    auto pending = pending_discord_match_repo->find_by_id(pending_id);
    ASSERT_TRUE(pending.has_value());
    EXPECT_FALSE(pending->resolved_at.empty());
    EXPECT_EQ(pending->resolved_action, "created_new");

    auto created = member_repo->find_by_id(pending->resolved_member_id);
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created->discord_user_id, "discord-modal-2");
    EXPECT_EQ(created->display_name, "New Person");
}

TEST_F(DiscordInteractionsTest, ModalSubmitUnauthorizedDenied) {
    int64_t pending_id = seed_pending_match("discord-modal-3", "modaluser3", "Denied Person");
    set_authorized_role("role-allowed-999");

    auto r = post_interaction(modal_submit_body(pending_id, "role-other", ""));
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.body.find("permission"), std::string::npos);

    auto pending = pending_discord_match_repo->find_by_id(pending_id);
    ASSERT_TRUE(pending.has_value());
    EXPECT_TRUE(pending->resolved_at.empty());
}

TEST_F(DiscordInteractionsTest, ModalSubmitInvalidMemberIdDenied) {
    int64_t pending_id = seed_pending_match("discord-modal-4", "modaluser4", "Bad Id Person");
    set_authorized_role("role-allowed-999");

    auto r = post_interaction(modal_submit_body(pending_id, "role-allowed-999", "999999"));
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.body.find("No member found"), std::string::npos);

    auto pending = pending_discord_match_repo->find_by_id(pending_id);
    ASSERT_TRUE(pending.has_value());
    EXPECT_TRUE(pending->resolved_at.empty()); // still unresolved
}

TEST_F(DiscordInteractionsTest, ModalSubmitNonNumericValueRejected) {
    int64_t pending_id = seed_pending_match("discord-modal-5", "modaluser5", "Garbage Person");
    set_authorized_role("role-allowed-999");

    auto r = post_interaction(modal_submit_body(pending_id, "role-allowed-999", "not-a-number"));
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.body.find("isn't a valid member ID"), std::string::npos);

    auto pending = pending_discord_match_repo->find_by_id(pending_id);
    ASSERT_TRUE(pending.has_value());
    EXPECT_TRUE(pending->resolved_at.empty());
}

TEST_F(DiscordInteractionsTest, NoAuthorizedRolesConfiguredMeansNobodyAuthorized) {
    int64_t id = seed_pending_match("discord-btn-4", "btnuser4", "No Config Person");
    // Deliberately do not call set_authorized_role — setting stays empty/default-closed.

    std::string body = R"({"type":3,"member":{"roles":["any-role-at-all"]},)"
                        R"("data":{"custom_id":"discord_match_resolve:)" + std::to_string(id) + R"("}})";
    auto r = post_interaction(body);
    EXPECT_EQ(r.code, 200);
    EXPECT_NE(r.body.find("permission"), std::string::npos);
}
