#pragma once
// A chat service LUG Manager can post to (Discord today; Slack, Matrix and
// others can be added by implementing this). ChatHub decides *what* to send
// and when - from the per-provider switches and the message templates - and
// calls these operations; a provider only knows *how* to talk to its service.
//
// To add one: implement this interface (see DiscordProvider.hpp), register it
// with the ChatHub in main.cpp, and give it a Features entry and a settings
// page for its channels. Operations a service doesn't have (scheduled events,
// forum threads) report caps() false and ChatHub skips them.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chat {

// Where a kind of post goes, in the provider's own settings.
enum class Place { Announcements, EventsForum, EventReports, MeetingReports, Reviews };

struct Caps {
    bool threads = false;           // threads under messages
    bool forums = false;            // forum channels (a thread per post)
    bool scheduled_events = false;  // the service's own event listing
    bool direct_messages = false;
    bool buttons = false;
};

// A message ready to send: text in chat markdown, plus exactly which roles
// and users it may ping (everything else in the text is inert).
struct Message {
    std::string text;
    std::vector<std::string> roles;
    std::vector<std::string> users;
};

struct ScheduledEvent { std::string name, description, location, start_local, end_local; };

struct Result {
    bool ok = false;
    std::string id;      // id of what was created
    std::string error;   // why not, in the service's words
};

class Provider {
public:
    virtual ~Provider() = default;

    virtual std::string id() const = 0;        // "discord"
    virtual std::string name() const = 0;      // "Discord"
    virtual bool ready() const = 0;            // switched on and configured (server/workspace chosen)
    virtual Caps caps() const = 0;
    // Switches the provider keeps in its own settings (Discord: "pings",
    // "update_notes"); nullopt = use the generic chat.<id>.<name> setting.
    virtual std::optional<bool> own_switch(const std::string&) const { return std::nullopt; }

    // ── Where things go ──
    virtual std::string place(Place p) const = 0;                    // "" = not set
    virtual std::string chapter_channel(int64_t chapter_id) const = 0;
    virtual std::string chapter_role(int64_t chapter_id) const = 0;
    virtual std::string announcement_role(bool non_lug) const = 0;

    // ── Text ──
    virtual std::string role_mention(const std::string& role) const = 0;
    virtual std::string user_mention(const std::string& user) const = 0;
    virtual std::string thread_url(const std::string& thread) const = 0;
    // Makes member-supplied text (titles, names, locations) unable to ping or
    // link anyone; called on every placeholder value.
    virtual std::string inert(const std::string& text) const = 0;

    // ── Messages ──
    virtual Result post(const std::string& channel, const Message& m) = 0;
    virtual Result edit(const std::string& channel, const std::string& message, const Message& m) = 0;
    virtual Result remove(const std::string& channel, const std::string& message) = 0;

    // ── Threads ──
    virtual Result start_forum_thread(const std::string& forum, const std::string& title, const Message& first) = 0;
    virtual Result start_thread(const std::string& channel, const std::string& message, const std::string& title) = 0;
    virtual Result rename_thread(const std::string& thread, const std::string& title) = 0;
    virtual Result edit_thread_starter(const std::string& thread, const Message& m) = 0;
    virtual Result remove_thread(const std::string& thread) = 0;

    // ── Scheduled events ──
    virtual Result create_event(const ScheduledEvent& e) = 0;
    virtual Result update_event(const std::string& id, const ScheduledEvent& e) = 0;
    virtual Result remove_event(const std::string& id) = 0;

    // ── Direct messages ──
    // The member's account on this service ("" if they haven't linked one).
    virtual std::string member_account(int64_t member_id) const = 0;
    virtual Result direct_message(const std::string& account, const Message& m) = 0;
};

} // namespace chat
