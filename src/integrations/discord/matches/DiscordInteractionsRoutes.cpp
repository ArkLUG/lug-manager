#include "integrations/discord/matches/DiscordInteractionsRoutes.hpp"
#include "integrations/discord/DiscordProvider.hpp"
#include <unordered_map>
#include <mutex>
#include <ctime>
#include "utils/web/ClientIp.hpp"
#include "integrations/discord/sync/DiscordLinkPolicy.hpp"
#include "integrations/discord/DiscordSignatureVerifier.hpp"
#include "models/Member.hpp"
#include <crow.h>
#include <sstream>
#include <unordered_set>

namespace {

// Discord interaction types (request) — https://discord.com/developers/docs/interactions/receiving-and-responding
constexpr int TYPE_PING               = 1;
constexpr int TYPE_MESSAGE_COMPONENT  = 3;
constexpr int TYPE_MODAL_SUBMIT       = 5;

// Discord interaction response types
constexpr int RESPONSE_PONG                        = 1;
constexpr int RESPONSE_CHANNEL_MESSAGE_WITH_SOURCE  = 4;
constexpr int RESPONSE_MODAL                        = 9;

// Ephemeral flag — response visible only to the clicking user, not posted publicly.
constexpr int FLAG_EPHEMERAL = 64;

crow::response json_response(int http_code, crow::json::wvalue body) {
    crow::response res;
    res.code = http_code;
    res.add_header("Content-Type", "application/json");
    res.write(body.dump());
    return res;
}

crow::response ephemeral_message(const std::string& content) {
    crow::json::wvalue body;
    body["type"] = RESPONSE_CHANNEL_MESSAGE_WITH_SOURCE;
    body["data"]["content"] = content;
    body["data"]["flags"]   = FLAG_EPHEMERAL;
    return json_response(200, std::move(body));
}

constexpr size_t kMaxInteractionBytes = 16 * 1024;

crow::response plain_status(int code) {
    crow::response res;
    res.code = code;
    return res;
}

// Timestamps within 5 minutes of now (Discord sends unix seconds).
bool fresh_timestamp(const std::string& ts) {
    if (ts.empty() || ts.size() > 12 || ts.find_first_not_of("0123456789") != std::string::npos) return false;
    const long long t = std::stoll(ts), now = static_cast<long long>(std::time(nullptr));
    return t > now - 300 && t < now + 300;
}

// Failed signature checks per client address: more than 20 in a minute and
// the address gets 429 for the rest of that minute.
class FailureLimiter {
public:
    bool blocked(const std::string& ip) {
        std::lock_guard<std::mutex> l(mu_);
        prune();
        auto it = counts_.find(ip);
        return it != counts_.end() && it->second > 20;
    }
    void add(const std::string& ip) {
        std::lock_guard<std::mutex> l(mu_);
        prune();
        ++counts_[ip];
    }
    void reset() { std::lock_guard<std::mutex> l(mu_); counts_.clear(); }
private:
    void prune() {
        const std::time_t minute = std::time(nullptr) / 60;
        if (minute != minute_) { counts_.clear(); minute_ = minute; }
    }
    std::mutex mu_;
    std::time_t minute_ = 0;
    std::unordered_map<std::string, int> counts_;
};
FailureLimiter& failures() { static FailureLimiter f; return f; }

// Bare 401, no body — used for every signature-verification failure so nothing
// about *why* it failed (missing header vs. bad signature vs. bad key) leaks.
crow::response unauthorized() {
    crow::response res;
    res.code = 401;
    return res;
}

// Parses the comma-separated Discord role-ID allowlist from settings into a set.
std::unordered_set<std::string> parse_role_ids(const std::string& csv) {
    std::unordered_set<std::string> out;
    std::istringstream ss(csv);
    std::string rid;
    while (std::getline(ss, rid, ',')) {
        if (!rid.empty()) out.insert(rid);
    }
    return out;
}

// Checks the interaction payload's `member.roles` array (present because Discord
// signed the whole payload — trustworthy without a separate API round-trip)
// against the admin-configured allowlist. Never a role-name match — see plan.
bool is_authorized(const crow::json::rvalue& body, const std::unordered_set<std::string>& allowlist) {
    if (allowlist.empty()) return false; // default-closed: no roles configured = nobody authorized
    if (!body.has("member")) return false;
    const auto& member = body["member"];
    if (!member.has("roles")) return false;
    const auto& roles = member["roles"];
    for (const auto& r : roles) {
        if (allowlist.count(r.s())) return true;
    }
    return false;
}

// Extracts "discord_match_resolve:<id>" / "discord_match_modal:<id>" → id, or -1 on malformed input.
int64_t extract_id(const std::string& custom_id, const std::string& prefix) {
    if (custom_id.rfind(prefix, 0) != 0) return -1;
    try {
        return std::stoll(custom_id.substr(prefix.size()));
    } catch (...) {
        return -1;
    }
}

} // namespace

namespace {
const std::string& env_key() {
    static const std::string k = [] { const char* v = std::getenv("DISCORD_PUBLIC_KEY"); return std::string(v ? v : ""); }();
    return k;
}
}
bool discord_interactions_key_locked() { return !env_key().empty(); }
void reset_discord_interaction_limits() { failures().reset(); }

std::string discord_interactions_key(SettingsRepository& settings) {
    return !env_key().empty() ? env_key() : settings.get("discord_public_key", "");
}

void register_discord_interactions_routes(LugApp& app,
                                           const std::string& discord_public_key,
                                           PendingDiscordMatchRepository& pending_matches,
                                           MemberRepository& member_repo,
                                           SettingsRepository& settings,
                                           AuditService& audit,
                                           std::shared_ptr<ReminderActions> reminder_actions) {

    // POST /discord/interactions — deliberately NOT wrapped in AuthMiddleware /
    // ApiKeyMiddleware checks. Discord is the caller, with no session or API key;
    // the trust boundary is entirely the Ed25519 signature check below, followed
    // by the role-ID allowlist for anything that mutates data. This is the only
    // intentionally ungated mutating route in the app — do not copy this pattern
    // elsewhere without the same signature-verification safeguard.
    CROW_ROUTE(app, "/discord/interactions").methods("POST"_method)(
        [&, reminder_actions](const crow::request& req) {

        // 0. Cheap limits first: Discord's interaction payloads are a few KB;
        //    a client that keeps failing the signature check gets slowed down.
        if (req.body.size() > kMaxInteractionBytes) return plain_status(413);
        // Failures are counted per address (the last X-Forwarded-For entry,
        // i.e. what Traefik saw). Requests that pass the signature check are
        // never refused by this, so a burst of junk can't lock Discord out.
        const std::string ip = client_ip(req);

        // 1. Raw body, captured before any parsing — the signature covers these
        //    exact bytes, so re-serializing (even losslessly) would break verification.
        const std::string& raw_body = req.body;

        // 2. Discord's two required headers.
        std::string signature = req.get_header_value("X-Signature-Ed25519");
        std::string timestamp = req.get_header_value("X-Signature-Timestamp");

        // 3. Verify before anything else touches the body. Fails closed on any
        //    malformed input; never throws.
        if (signature.empty() || timestamp.empty() ||
            !verify_discord_signature(discord_public_key.empty() ? discord_interactions_key(settings) : discord_public_key,
                                      signature, timestamp, raw_body)) {
            failures().add(ip);
            return failures().blocked(ip) ? plain_status(429) : unauthorized();
        }
        // 3b. Replay window: Discord's timestamp (unix seconds) must be within
        //     5 minutes of now. The signature covers it, so it can't be edited.
        if (!fresh_timestamp(timestamp)) {
            failures().add(ip);
            return failures().blocked(ip) ? plain_status(429) : unauthorized();
        }

        // 4. Only now parse.
        crow::json::rvalue body;
        try {
            body = crow::json::load(raw_body);
            if (!body) return unauthorized();
        } catch (...) {
            return unauthorized();
        }

        int type = body.has("type") ? body["type"].i() : 0;

        // --- PING: fastest path, no DB/role access at all ---
        if (type == TYPE_PING) {
            crow::json::wvalue pong;
            pong["type"] = RESPONSE_PONG;
            return json_response(200, std::move(pong));
        }

        auto allowlist = parse_role_ids(settings.get("discord_matches_authorized_role_ids", ""));

        // --- MESSAGE_COMPONENT: the "Resolve Match" button click ---
        if (type == TYPE_MESSAGE_COMPONENT) {
            if (!body.has("data") || !body["data"].has("custom_id")) return unauthorized();
            std::string custom_id = body["data"]["custom_id"].s();

            // Reminder DM buttons: the member acts on their own reminder (no
            // role allowlist; ReminderActions checks the DM was sent to them).
            if (custom_id.rfind("lm:", 0) == 0) {
                if (!reminder_actions) return ephemeral_message("This button doesn't work any more.");
                std::string clicker;
                if (body.has("user") && body["user"].has("id")) clicker = body["user"]["id"].s();
                else if (body.has("member") && body["member"].has("user") && body["member"]["user"].has("id"))
                    clicker = body["member"]["user"]["id"].s();
                auto reply = reminder_actions->handle(clicker, custom_id);
                if (!reply || reply->private_only) return ephemeral_message(reply ? reply->note : "This button doesn't work any more.");
                std::string content;
                if (body.has("message") && body["message"].has("content")) content = body["message"]["content"].s();
                crow::json::wvalue upd;
                upd["type"] = 7;   // UPDATE_MESSAGE: edit the DM in place
                upd["data"]["content"] = (content.empty() ? "" : content + "\n\n") + reply->note;
                upd["data"]["components"] = crow::json::load(chat::DiscordProvider::components(reply->buttons));
                return json_response(200, std::move(upd));
            }
            int64_t pending_id = extract_id(custom_id, "discord_match_resolve:");
            if (pending_id < 0) return ephemeral_message("This button is no longer valid.");

            if (!is_authorized(body, allowlist)) {
                return ephemeral_message("You don't have permission to resolve member matches.");
            }

            auto pending = pending_matches.find_by_id(pending_id);
            if (!pending || !pending->resolved_at.empty()) {
                return ephemeral_message("This match has already been resolved.");
            }

            // Open the modal — this IS the HTTP response to the click, no outbound
            // Discord API call needed.
            crow::json::wvalue modal;
            modal["type"] = RESPONSE_MODAL;
            modal["data"]["custom_id"] = "discord_match_modal:" + std::to_string(pending_id);
            modal["data"]["title"]     = "Resolve Discord Match";
            crow::json::wvalue row;
            row["type"] = 1; // Action Row
            crow::json::wvalue input;
            input["type"]         = 4; // Text Input
            input["custom_id"]    = "member_id_or_new";
            input["label"]        = "Member ID to link (blank = create new)";
            input["style"]        = 1; // short
            input["required"]     = false;
            input["placeholder"]  = "e.g. 42, or leave blank for " + pending->discord_display_name;
            row["components"][0]  = std::move(input);
            modal["data"]["components"][0] = std::move(row);
            return json_response(200, std::move(modal));
        }

        // --- MODAL_SUBMIT: the form submission ---
        if (type == TYPE_MODAL_SUBMIT) {
            if (!body.has("data") || !body["data"].has("custom_id")) return unauthorized();
            std::string custom_id = body["data"]["custom_id"].s();
            int64_t pending_id = extract_id(custom_id, "discord_match_modal:");
            if (pending_id < 0) return ephemeral_message("This form is no longer valid.");

            // Defense in depth — re-check authorization, never trust the earlier click.
            if (!is_authorized(body, allowlist)) {
                return ephemeral_message("You don't have permission to resolve member matches.");
            }

            auto pending = pending_matches.find_by_id(pending_id);
            if (!pending || !pending->resolved_at.empty()) {
                return ephemeral_message("This match has already been resolved.");
            }

            // Discord nests modal field values: data.components[0].components[0].value
            std::string value_str;
            try {
                const auto& comps = body["data"]["components"];
                value_str = comps[0]["components"][0]["value"].s();
            } catch (...) {
                return ephemeral_message("Couldn't read the submitted form.");
            }

            // Trim whitespace
            size_t start = value_str.find_first_not_of(" \t\r\n");
            std::string trimmed = (start == std::string::npos) ? "" : value_str.substr(start);
            size_t end = trimmed.find_last_not_of(" \t\r\n");
            if (end != std::string::npos) trimmed = trimmed.substr(0, end + 1);

            std::string discord_actor = "discord:" + pending->discord_username;

            if (trimmed.empty()) {
                // Blank → create a new member from the Discord identity.
                Member m;
                m.discord_user_id  = pending->discord_user_id;
                m.discord_username = pending->discord_username;
                m.display_name     = pending->discord_display_name;
                m.role             = "member";
                Member created = member_repo.create(m);

                pending_matches.mark_resolved(pending->id, "created_new", created.id);
                audit.log_system("discord_match.create_new", "member", created.id, created.display_name,
                    "Created new member from Discord via interaction, resolved by " + discord_actor);

                return ephemeral_message("Created new member " + created.display_name + ".");
            }

            // Otherwise, must be a numeric member ID to link.
            int64_t member_id = 0;
            try {
                size_t pos = 0;
                member_id = std::stoll(trimmed, &pos);
                if (pos != trimmed.size()) member_id = 0; // trailing garbage → invalid
            } catch (...) {
                member_id = 0;
            }
            if (member_id <= 0) {
                return ephemeral_message("\"" + trimmed + "\" isn't a valid member ID. Leave blank to create a new member instead.");
            }

            auto member = member_repo.find_by_id(member_id);
            if (!member) {
                return ephemeral_message("No member found with ID " + std::to_string(member_id) + ".");
            }

            // Interaction resolvers are never LUG admins as far as we can tell.
            std::string blocked = discord_link_block_reason(*member, /*caller_is_admin=*/false);
            if (!blocked.empty()) return ephemeral_message(blocked);

            member_repo.link_discord_id(member_id, pending->discord_user_id, pending->discord_username);
            pending_matches.mark_resolved(pending->id, "linked", member_id);
            audit.log_system("discord_match.link", "member", member_id, member->display_name,
                "Linked Discord user " + pending->discord_display_name + " via interaction, resolved by " + discord_actor);

            return ephemeral_message("Linked to " + member->display_name + ".");
        }

        // Unknown/unsupported interaction type — acknowledge harmlessly.
        return plain_status(400);   // signed, but not a kind of interaction we handle
    });
}
