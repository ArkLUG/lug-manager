#pragma once
#include "config/Config.hpp"
#include "async/ThreadPool.hpp"
#include "models/Meeting.hpp"
#include "models/LugEvent.hpp"
#include <string>
#include <vector>
#include <functional>

struct DiscordChannel {
    std::string id;
    std::string name;
    int         type = 0; // 0 = GUILD_TEXT
};

struct DiscordThread {
    std::string id;
    std::string name;
};

struct DiscordRole {
    std::string id;
    std::string name;
    uint32_t    color = 0;
};

struct DiscordGuildMember {
    std::string              discord_user_id;
    std::string              username;
    std::string              global_name;   // empty when Discord API returns null
    std::string              nick;          // guild-specific nickname, empty if not set
    std::vector<std::string> role_ids;
};

class DiscordClient {
public:
    DiscordClient(const Config& config, ThreadPool& pool);

    // Update guild/channel config at runtime (called after settings are loaded/saved)
    void        reconfigure(const std::string& guild_id,
                            const std::string& lug_channel_id,
                            const std::string& events_forum_channel_id = "",
                            const std::string& announcement_role_id = "",
                            const std::string& non_lug_event_role_id = "",
                            const std::string& timezone = "");
    void        set_timezone(const std::string& tz) { if (!tz.empty()) timezone_ = tz; }
    std::string get_guild_id()                  const { return guild_id_; }
    std::string get_lug_channel_id()            const { return lug_channel_id_; }
    std::string get_events_forum_channel_id()   const { return events_forum_channel_id_; }
    std::string get_announcement_role_id()      const { return announcement_role_id_; }
    std::string get_non_lug_event_role_id()     const { return non_lug_event_role_id_; }
    std::string get_timezone()                  const { return timezone_; }
    bool        get_suppress_pings()            const { return suppress_pings_; }
    void        set_suppress_pings(bool v)            { suppress_pings_ = v; }
    bool        get_suppress_updates()          const { return suppress_updates_; }
    void        set_suppress_updates(bool v)          { suppress_updates_ = v; }

    // Fetch text channels (type 0/5) or forum channels (type 15) from the configured guild
    std::vector<DiscordChannel> fetch_text_channels()  const;
    std::vector<DiscordChannel> fetch_forum_channels() const;

    // Fetch all roles defined in the guild
    std::vector<DiscordRole> fetch_guild_roles() const;

    // Fetch the Discord role IDs that a guild member currently has
    std::vector<std::string> fetch_member_role_ids(const std::string& discord_user_id) const;

    // Fetch all guild members with pagination (skips bots)
    std::vector<DiscordGuildMember> fetch_guild_members() const;

    // Creates/updates/cancels/deletes Discord scheduled event for a meeting
    void create_scheduled_event(const Meeting& m);
    void update_scheduled_event(const Meeting& m);
    void cancel_scheduled_event(const std::string& discord_event_id);
    void delete_scheduled_event(const std::string& discord_event_id);
    void delete_channel(const std::string& channel_or_thread_id); // also deletes threads

    // For LugEvents: creates announcement thread + scheduled event
    void create_event_thread(LugEvent& e);          // Posts message + creates thread, fills e.discord_thread_id
    void create_event_scheduled_event(LugEvent& e); // Creates Discord scheduled event, fills e.discord_event_id
    void update_event(const LugEvent& e);

    // Creates forum thread for a LugEvent, picking the right role based on scope
    std::string sync_create_forum_thread_for_event(const std::string& title, const LugEvent& e);
    // Creates text channel thread for a LugEvent, picking the right role based on scope
    std::string sync_create_text_thread_for_event(const std::string& title, const LugEvent& e);
    // Posts an event announcement message to a channel; returns the message ID (sync)
    std::string sync_post_event_announcement(const std::string& channel_id, const LugEvent& e,
                                              const std::string& role_id,
                                              const std::string& thread_url = "");
    // Creates a public thread from an existing message; returns thread ID (sync)
    std::string sync_create_thread_from_message(const std::string& channel_id,
                                                 const std::string& message_id,
                                                 const std::string& thread_name);
    // Edits an existing message in a channel (async)
    void update_channel_message(const std::string& channel_id, const std::string& message_id,
                                 const std::string& content);
    // Deletes a message in a channel (sync, errors logged not thrown)
    void delete_channel_message(const std::string& channel_id, const std::string& message_id);

    // Post a plain message to a channel
    void post_message(const std::string& channel_id, const std::string& content);

    // Assign or remove a Discord role from a guild member (synchronous)
    void add_member_role(const std::string& discord_user_id, const std::string& role_id);
    void remove_member_role(const std::string& discord_user_id, const std::string& role_id);

    // Set a member's server nickname (synchronous). Returns empty on success, error message on failure.
    std::string set_member_nickname(const std::string& discord_user_id, const std::string& nickname);

    // Remove (kick) a member from the guild (synchronous)
    void kick_member(const std::string& discord_user_id);

    // Fetch active threads in the configured forum channel
    std::vector<DiscordThread> fetch_forum_threads() const;

    // Sync variants for when we need the discord_event_id back
    std::string sync_create_scheduled_event_meeting(const Meeting& m);
    std::string sync_create_scheduled_event_event(const LugEvent& e);
    std::string sync_create_event_thread(const std::string& channel_id, const std::string& title, const std::string& description);
    // Creates a thread post in a forum channel (type 15) and returns the thread id
    std::string sync_create_forum_thread(const std::string& title, const std::string& description);

    // Brief announcement: title, dates, location, thread link + role pings
    static std::string build_event_announcement_content(const LugEvent& e,
                                                         const std::string& role_id,
                                                         const std::string& thread_url = "",
                                                         bool suppress_pings = false);
    // Full thread starter: all details + pings the event lead by Discord mention
    static std::string build_thread_starter_content(const LugEvent& e,
                                                     const std::string& role_id,
                                                     bool suppress_pings = false);

    // Meeting announcement: title, date/time, location + role ping
    static std::string build_meeting_announcement_content(const Meeting& m,
                                                           const std::string& role_id,
                                                           const std::string& tz_name = "UTC",
                                                           bool suppress_pings = false);
    // Posts a meeting announcement to a channel; returns the message ID (sync)
    std::string sync_post_meeting_announcement(const std::string& channel_id, const Meeting& m,
                                               const std::string& role_id);

    // Publish a report to a forum channel. Creates new thread or edits existing.
    // Returns the thread ID (for storing as notes_discord_post_id).
    std::string publish_report_to_forum(const std::string& forum_channel_id,
                                         const std::string& existing_thread_id,
                                         const std::string& title,
                                         const std::string& content);

private:
    const Config& config_;
    ThreadPool&   pool_;
    std::string   guild_id_;
    std::string   lug_channel_id_;
    std::string   events_forum_channel_id_;
    std::string   announcement_role_id_;
    std::string   non_lug_event_role_id_;
    std::string   timezone_              = "UTC";
    bool          suppress_pings_        = false;
    bool          suppress_updates_      = false;

    static size_t write_cb(void* contents, size_t size, size_t nmemb, std::string* s);
    std::string discord_api_request(const std::string& method, const std::string& endpoint,
                                    const std::string& json_body = "") const;
    std::string build_meeting_event_json(const Meeting& m) const;
    std::string build_lug_event_json(const LugEvent& e) const;
    std::string iso_to_discord_timestamp(const std::string& iso) const;
};
