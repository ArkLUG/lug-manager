#pragma once
#include <optional>
#include "config/Config.hpp"
#include "async/ThreadPool.hpp"
#include "models/Meeting.hpp"
#include "models/LugEvent.hpp"
#include <string>
#include <ctime>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <chrono>
#include <unordered_map>

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
    // Unlike reconfigure(), these can clear the value (Settings > Discord).
    void        set_events_forum_channel_id(const std::string& id) { std::lock_guard<std::mutex> l(cfg_mutex_); events_forum_channel_id_ = id; }
    void        set_lug_channel_id(const std::string& id)           { std::lock_guard<std::mutex> l(cfg_mutex_); lug_channel_id_ = id; }
    void        set_timezone(const std::string& tz) { if (tz.empty()) return; std::lock_guard<std::mutex> l(cfg_mutex_); timezone_ = tz; }
    std::string get_guild_id()                  const { std::lock_guard<std::mutex> l(cfg_mutex_); return guild_id_; }
    std::string get_lug_channel_id()            const { std::lock_guard<std::mutex> l(cfg_mutex_); return lug_channel_id_; }
    std::string get_events_forum_channel_id()   const { std::lock_guard<std::mutex> l(cfg_mutex_); return events_forum_channel_id_; }
    std::string get_event_reports_forum_id()   const { std::lock_guard<std::mutex> l(cfg_mutex_); return event_reports_forum_id_; }
    std::string get_meeting_reports_forum_id() const { std::lock_guard<std::mutex> l(cfg_mutex_); return meeting_reports_forum_id_; }
    void set_event_reports_forum_id(const std::string& id) { std::lock_guard<std::mutex> l(cfg_mutex_); event_reports_forum_id_ = id; }
    void set_meeting_reports_forum_id(const std::string& id) { std::lock_guard<std::mutex> l(cfg_mutex_); meeting_reports_forum_id_ = id; }
    std::string get_announcement_role_id()      const { std::lock_guard<std::mutex> l(cfg_mutex_); return announcement_role_id_; }
    std::string get_non_lug_event_role_id()     const { std::lock_guard<std::mutex> l(cfg_mutex_); return non_lug_event_role_id_; }
    std::string get_timezone()                  const { std::lock_guard<std::mutex> l(cfg_mutex_); return timezone_; }
    bool        get_suppress_pings()            const { return suppress_pings_; }
    void        set_suppress_pings(bool v)            { suppress_pings_ = v; }
    bool        get_suppress_updates()          const { return suppress_updates_; }
    void        set_suppress_updates(bool v)          { suppress_updates_ = v; }

    // Fetch text channels (type 0/5) or forum channels (type 15) from the configured guild
    std::vector<DiscordChannel> fetch_text_channels()  const;
    // Same for an explicit guild (Settings preview of a not-yet-saved guild id).
    std::vector<DiscordChannel> fetch_text_channels(const std::string& guild_id) const;
    std::vector<DiscordChannel> fetch_forum_channels() const;
    std::vector<DiscordChannel> fetch_voice_channels() const;

    // Fetch all roles defined in the guild
    std::vector<DiscordRole> fetch_guild_roles() const;

    // Fetch the Discord role IDs that a guild member currently has
    std::vector<std::string> fetch_member_role_ids(const std::string& discord_user_id) const;

    // Like fetch_member_role_ids, but distinguishes "confirmed guild member
    // (possibly with no roles)" from "not a member / couldn't verify": returns
    // nullopt unless Discord returned a guild member object for this user.
    std::optional<std::vector<std::string>> fetch_guild_member_role_ids(const std::string& discord_user_id) const;

    // Fetch all guild members with pagination (skips bots)
    std::vector<DiscordGuildMember> fetch_guild_members() const;




    // Direct message a user (opens the DM channel first). Fails quietly
    // (returns false) if they don't share a server or have DMs closed.
    bool send_dm(const std::string& discord_user_id, const std::string& content);
    // Guild channel/role/thread lists are cached for 2 minutes (settings and
    // forms used to hit Discord on every view). Call to force a fresh read.
    void clear_cache() const;

    // ── Generic operations used by chat::DiscordProvider (synchronous) ──
    // Every message states exactly which roles/users it may ping
    // (allowed_mentions); nothing else in the text can notify anyone.
    struct Result { bool ok = false; std::string id; std::string error; };
    struct ScheduledEvent { std::string name, description, location, start_local, end_local; };
    Result send_message(const std::string& channel, const std::string& content,
                        const std::vector<std::string>& roles = {}, const std::vector<std::string>& users = {});
    Result edit_message(const std::string& channel, const std::string& message, const std::string& content,
                        const std::vector<std::string>& roles = {}, const std::vector<std::string>& users = {});
    Result remove_message(const std::string& channel, const std::string& message);
    Result start_forum_thread(const std::string& forum, const std::string& name, const std::string& content,
                              const std::vector<std::string>& roles = {}, const std::vector<std::string>& users = {});
    Result start_thread_from_message(const std::string& channel, const std::string& message, const std::string& name);
    Result rename_thread(const std::string& thread, const std::string& name);
    Result remove_channel(const std::string& id);
    Result create_scheduled(const ScheduledEvent& e);
    Result update_scheduled(const std::string& id, const ScheduledEvent& e);
    Result remove_scheduled(const std::string& id);
    Result direct_message(const std::string& user, const std::string& content);
    static long& last_status_ref();   // HTTP status of this thread's last Discord request
    // Runs a job on the integration worker pool (request handlers that mustn't wait on chat services).
    void run_async(std::function<void()> job);

    // ── Maintenance (DiscordTimeRepair): synchronous, read or edit only ──
    // Never posts anything new. Edits don't notify members on Discord.
    std::string sync_get(const std::string& endpoint) const;   // read-only GET, uncached
    std::string sync_patch_scheduled_event_times(const std::string& discord_event_id,
                                                 const std::string& start_utc, const std::string& end_utc);
    std::string sync_edit_message_content(const std::string& channel_id, const std::string& message_id,
                                          const std::string& content);   // pings nobody
    std::string utc_iso(const std::string& local_iso) const { return iso_to_discord_timestamp(local_iso); }
    static std::string tz_abbrev(const std::string& local_iso, const std::string& tz_name);   // "CDT"
    // Interprets an ISO "YYYY-MM-DDTHH:MM[:SS]" in IANA zone tz_name and
    // returns the UTC epoch, or -1 if unparseable.
    static std::time_t local_to_epoch(const std::string& iso, const std::string& tz_name);
    // "Tue 6/2 7:00 PM CDT" for a LUG-local ISO time.
    static std::string friendly_time(const std::string& iso, const std::string& tz_name);

    // Posts a message with a single "Resolve" button to a channel (fire-and-forget).
    // custom_id must encode enough state for the interaction handler to act on the
    // click (e.g. "discord_match_resolve:" + pending_discord_matches row id).
    void post_button_message(const std::string& channel_id, const std::string& content,
                              const std::string& button_label, const std::string& custom_id);

    // Assign or remove a Discord role from a guild member (synchronous)
    void add_member_role(const std::string& discord_user_id, const std::string& role_id);
    void remove_member_role(const std::string& discord_user_id, const std::string& role_id);

    // Set a member's server nickname (synchronous). Returns empty on success, error message on failure.
    std::string set_member_nickname(const std::string& discord_user_id, const std::string& nickname);

    // Remove (kick) a member from the guild (synchronous)
    void kick_member(const std::string& discord_user_id);

    // Fetch active threads in the configured forum channel
    std::vector<DiscordThread> fetch_forum_threads() const;





private:
    const Config& config_;
    ThreadPool&   pool_;
    // Runtime-reconfigurable settings (admin Settings page saves call
    // reconfigure()/setters while request and worker threads are mid-API-call).
    // Always read through the get_*() accessors, which copy under this lock -
    // an unsynchronized std::string read racing a write is undefined behavior.
    mutable std::mutex cfg_mutex_;
    // GET cache for guild lists - see clear_cache().
    mutable std::mutex cache_mutex_;
    mutable std::unordered_map<std::string, std::pair<std::chrono::steady_clock::time_point, std::string>> get_cache_;
    std::string   guild_id_;
    std::string   lug_channel_id_;
    std::string   events_forum_channel_id_;
    std::string   announcement_role_id_;
    std::string   non_lug_event_role_id_;
    std::string   timezone_              = "UTC";
    std::string   event_reports_forum_id_;
    std::string   meeting_reports_forum_id_;
    std::atomic<bool> suppress_pings_{false};
    std::atomic<bool> suppress_updates_{false};

    static size_t write_cb(void* contents, size_t size, size_t nmemb, std::string* s);
    std::string discord_api_request(const std::string& method, const std::string& endpoint,
                                    const std::string& json_body = "") const;
    std::string discord_api_request_uncached(const std::string& method, const std::string& endpoint,
                                             const std::string& json_body) const;
    std::string iso_to_discord_timestamp(const std::string& iso) const;
    std::string scheduled_json(const ScheduledEvent& e) const;
    Result call(const std::string& method, const std::string& endpoint, const std::string& body, bool want_id);
};
