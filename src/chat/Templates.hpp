#pragma once
// Every message LUG Manager sends - chat posts, direct messages/emails and
// sign-in emails - comes from a template an admin can edit (Settings >
// Messages). Overrides are stored in lug_settings as "tmpl.<key>" (and
// "tmpl.<key>.subject" for the subject line); the defaults below are the
// built-in wording.
//
// Template syntax, kept simple for non-programmers:
//   {name}        a placeholder, replaced by its value
//   [[ ... ]]     an optional part: left out when any placeholder in it is empty
//                 (may span lines)
//   a line whose placeholders are all empty is left out entirely
//   {{ and }}     a literal { or }
// Text is written in chat markdown (**bold**, *italic*); emails get it
// stripped to plain text.
#include "db/SqliteDatabase.hpp"
#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace chat {

using Values = std::map<std::string, std::string>;

struct Placeholder { const char* name; const char* help; const char* sample; };

struct TemplateDef {
    const char* key;
    const char* group;            // shown as a heading in the editor
    const char* label;
    const char* help;             // when it's sent
    const char* default_subject;  // "" = no subject line (chat posts)
    const char* default_body;
    std::vector<Placeholder> placeholders;
    std::vector<const char*> required;   // placeholders the body must keep (e.g. {link})
    size_t max_len;                      // longest sensible result (0 = no limit)
};

inline const char* kGroupChannel = "Chat posts";
inline const char* kGroupDirect = "Messages to members (chat DM or email)";
inline const char* kGroupSignIn = "Sign-in emails";

// Shared placeholder sets
#define LUGM_EVENT_PH \
    {"title", "Event name", "Brick Fest"}, \
    {"dates", "Dates, like 10/14 - 10/15", "10/14 - 10/15"}, \
    {"location", "Full location", "Expo Hall, 1 Main St, Little Rock, AR"}, \
    {"location_short", "City, state", "Little Rock, AR"}, \
    {"description", "Event description", "Two days of LEGO for everyone."}, \
    {"non_lug", "\"[External] \" for events run by others, else empty", ""}, \
    {"link", "Link to the event in LUG Manager (needs the public URL set)", "https://lug.example.org/events/12"}, \
    {"fee", "Entrance fee", "$5"}

inline const std::vector<TemplateDef>& all_templates() {
    static const std::vector<TemplateDef> defs = {
        // ── Chat posts ──
        {"event.announcement", kGroupChannel, "Event announcement",
         "Posted in the announcements channel (and a chapter's channel) when an event is published; edited when it changes.",
         "", "{pings}\n{non_lug}**{title}**\nDates: {dates}\nLocation: {location}\nDiscussion Thread: {thread_link}",
         {LUGM_EVENT_PH, {"pings", "Role mentions (empty when pings are off)", "@LUG Members"},
          {"thread_link", "Link to the event's discussion thread", "https://discord.com/channels/1/2"}}, {}, 2000},
        {"event.thread_title", kGroupChannel, "Event thread title",
         "Name of the forum thread (or thread) made for each event. Up to 100 characters.",
         "", "{title}[[ | {location_short}]][[ | {dates_year}]]",
         {LUGM_EVENT_PH, {"dates_year", "Dates with the year, like 10/14/26-10/15/26", "10/14/26-10/15/26"}}, {"title"}, 100},
        {"event.thread_starter", kGroupChannel, "Event thread first post",
         "The first post in an event's thread.",
         "", "{pings}\n{non_lug}**{title}**\nDates: {dates}\nLocation: {location}\nLead: {lead}\nSignup Deadline: {signup_deadline}\n"
             "Capacity: {capacity}[[\n\n{description}]]",
         {LUGM_EVENT_PH, {"pings", "The event's extra ping roles (empty when pings are off)", "@Train Fans"},
          {"lead", "Event lead (a mention when pings are on)", "Ann B."},
          {"signup_deadline", "Signup deadline, like 10/1", "10/1"}, {"capacity", "Maximum attendees", "40"}}, {}, 2000},
        {"event.updated", kGroupChannel, "Event updated note",
         "Posted in the event's thread when the event is edited (unless update notes are off).",
         "", "**Event Updated** — {title} has been updated.", {LUGM_EVENT_PH}, {}, 2000},
        {"meeting.announcement", kGroupChannel, "Meeting announcement",
         "Posted in the announcements channel (group-wide) or the chapter's channel when a meeting is published; edited when it changes.",
         "", "{pings}\n**{title}**\nWhen: {when_range}\nWhere: {location}[[\n\n{description}]]",
         {{"title", "Meeting name", "October meeting"}, {"when_range", "Start and end, like 10/14 7:00 PM CDT – 10/14 9:00 PM CDT (on Discord, in each reader's own time zone)",
           "10/14 7:00 PM CDT – 10/14 9:00 PM CDT"},
          {"when", "Start, like Tue 10/14 7:00 PM CDT (on Discord, in each reader's own time zone)", "Tue 10/14 7:00 PM CDT"},
          {"when_relative", "How long until it starts, like \"in 3 days\" (Discord only; empty elsewhere)", "in 3 days"},
          {"location", "Where", "Brickton Library"}, {"description", "Meeting description", "Bring a build to show."},
          {"pings", "Role mention (empty when pings are off)", "@LUG Members"},
          {"chapter", "Chapter name (empty for group-wide)", "North Chapter"},
          {"link", "Link to the meeting in LUG Manager", "https://lug.example.org/meetings/7"}}, {}, 2000},
        {"reminder.meeting", kGroupChannel, "Meeting reminder",
         "Posted before a meeting when Discord reminders are on.",
         "", "⏰ **Reminder:** {title} - {when}[[ ({when_relative})]][[ at {location}]]",
         {{"title", "Meeting name", "October meeting"}, {"when", "When it starts (on Discord, in each reader's own time zone)", "Tue 10/14 7:00 PM CDT"},
          {"when_relative", "How long until it starts (Discord only)", "in 1 day"},
          {"location", "Where", "Brickton Library"}, {"link", "Link to the meeting in LUG Manager", "https://lug.example.org/meetings/7"}},
         {}, 2000},
        {"reminder.event", kGroupChannel, "Event reminder",
         "Posted in the event's thread (or the announcements channel) before it starts, when reminders are on.",
         "", "⏰ **Reminder:** {title} starts {when}[[ ({when_relative})]][[ at {location}]]",
         {{"title", "Event name", "Brick Fest"}, {"when", "When it starts (on Discord, in each reader's own time zone)", "Sat 10/14 9:00 AM CDT"},
          {"when_relative", "How long until it starts (Discord only)", "in 1 day"},
          {"location", "Where", "Expo Hall"}, {"link", "Link to the event in LUG Manager", "https://lug.example.org/events/12"}},
         {}, 2000},
        {"challenge.winner", kGroupChannel, "Build challenge winner",
         "Posted when an admin announces a challenge's winner.",
         "", "🏆 **{challenge}** winner: **{winner}** with \"{entry}\" ({votes} votes). Congratulations!",
         {{"challenge", "Challenge name", "Space Week"}, {"winner", "Winner's name", "Priya S."},
          {"entry", "Winning entry's title", "Moon Base"}, {"votes", "Number of votes", "12"}}, {}, 2000},
        {"report.event.title", kGroupChannel, "Event report title",
         "Title of the report thread posted to the event reports forum.",
         "", "Report: {title}", {{"title", "Event name", "Brick Fest"}}, {}, 100},
        {"report.event", kGroupChannel, "Event report",
         "Posted to the event reports forum with \"Post report to Discord\" (edited if posted again).",
         "", "**Event name:** {title}\n**Chapter:** {chapter}\n**Start date:** {start_date}\n**End date:** {end_date}\n"
             "**Location:** {location}\n**Lead:** {lead}\n**Entrance fee:** {fee}\n{attendance}\n**Public kids:** {public_kids}\n"
             "**Public teens:** {public_teens}\n**Public adults:** {public_adults}"
             "[[\n**Social media links, {lug_name} mentions, announcements for show:** {social_links}]]\n"
             "**What you liked best about event:** {feedback}[[\n\n## Description\n{description}]][[\n\n## Notes\n{notes}]]",
         {{"title", "Event name", "Brick Fest"}, {"chapter", "Chapter, or Group-wide", "Group-wide"},
          {"start_date", "Start date", "2026-10-14"}, {"end_date", "End date", "2026-10-15"}, {"location", "Where", "Expo Hall"},
          {"lead", "Event lead", "Ann B."}, {"fee", "Entrance fee", "$5"},
          {"attendance", "Members who came, listed day by day", "**Member names day1:**\n- Ann B.\n- Ben C."},
          {"public_kids", "Visitors: kids", "40"}, {"public_teens", "Visitors: teens", "10"}, {"public_adults", "Visitors: adults", "50"},
          {"social_links", "Social media links", "https://example.org/post"}, {"feedback", "What people liked best", "The train layout"},
          {"description", "Event description", "Two days of LEGO."}, {"notes", "Event notes", "Tables arrived late."},
          {"lug_name", "Your LUG's name", "Brickton LUG"}}, {}, 2000},
        {"report.meeting.title", kGroupChannel, "Meeting report title",
         "Title of the report thread posted to the meeting reports forum.",
         "", "Report: {title}", {{"title", "Meeting name", "October meeting"}}, {}, 100},
        {"report.meeting", kGroupChannel, "Meeting report",
         "Posted to the meeting reports forum with \"Post report to Discord\" (edited if posted again).",
         "", "**Meeting:** {title}\n**Chapter:** {chapter}\n**Meeting date:** {date}\n**Format:** {format}\n**Location:** {location}\n"
             "**Members by name:**\n{attendance}[[\n\n## Description\n{description}]][[\n\n## Notes\n{notes}]]",
         {{"title", "Meeting name", "October meeting"}, {"chapter", "Chapter, or Group-wide", "Group-wide"},
          {"date", "Meeting date", "2026-10-14"}, {"format", "\"Virtual\" for online meetings, else empty", ""},
          {"location", "Where (empty for virtual meetings)", "Brickton Library"},
          {"attendance", "Members who came, one per line", "- Ann B.\n- Ben C. (virtual)"},
          {"description", "Meeting description", "Bring a build."}, {"notes", "Meeting notes", "Voted on the summer show."}}, {}, 2000},
        {"match.review", kGroupChannel, "New member needs review",
         "Posted (with a button) when someone joins the Discord server and might match an existing member.",
         "", "**New Discord member match needs review**\nDiscord user: **{discord_name}** (`{discord_username}`)\n"
             "Click below to link to an existing member or create a new one.",
         {{"discord_name", "Their display name", "Ann Builder"}, {"discord_username", "Their username", "annb"}}, {}, 2000},
        {"test.message", kGroupChannel, "Test message",
         "Sent by the \"Send a test message\" button.",
         "", "🧱 **LUG Manager test announcement** — bot is connected and posting correctly!", {}, {}, 2000},

        // ── Messages to members ──
        {"dm.event_reminder", kGroupDirect, "RSVP reminder",
         "Sent to members going to an event, before it starts (when reminder DMs are on).",
         "Reminder: {title} - {when}", "⏰ You're going to **{title}** - {when}[[ ({when_relative})]][[ at {location}]]. See you there!",
         {{"title", "Event name", "Brick Fest"}, {"when", "When it starts (on Discord, in each reader's own time zone)", "Sat 10/14 9:00 AM CDT"},
          {"when_relative", "How long until it starts (Discord DMs only)", "in 1 day"},
          {"location", "Where", "Expo Hall"}, {"name", "Member's name", "Ann B."}}, {}, 2000},
        {"dm.meeting_reminder", kGroupDirect, "Meeting reminder",
         "Sent to members who said they're going to a meeting, before it starts (when reminder DMs are on).",
         "Reminder: {title} - {when}", "⏰ You're going to **{title}** - {when}[[ ({when_relative})]][[ at {location}]]. See you there!",
         {{"title", "Meeting name", "October meeting"}, {"when", "When it starts (on Discord, in each reader's own time zone)", "Tue 10/14 7:00 PM CDT"},
          {"when_relative", "How long until it starts (Discord DMs only)", "in 1 day"},
          {"location", "Where", "Library"}, {"name", "Member's name", "Ann B."}}, {}, 2000},
        {"dm.shift_reminder", kGroupDirect, "Volunteer shift reminder",
         "Sent to volunteers before their shift (when reminder DMs are on).",
         "Volunteer shift: {shift} - {when}", "⏰ Reminder: you're volunteering for **{shift}** at {event} - {when}. Thank you!",
         {{"shift", "Shift name", "Set-up crew"}, {"event", "Event name", "Brick Fest"}, {"when", "When the shift starts (on Discord, in each reader's own time zone)", "Fri 10/13 6:00 PM CDT"},
          {"when_relative", "How long until it starts (Discord DMs only)", "in 1 day"},
          {"name", "Member's name", "Ann B."}}, {}, 2000},
        {"dm.waitlist", kGroupDirect, "Off the waitlist",
         "Sent when a spot opens up and a waitlisted member is moved to going.",
         "You're in: {title}",
         "🎉 A spot opened up for **{title}** ({when}) - you're off the waitlist and now going. "
         "If you can't make it, please cancel your RSVP so the next person gets the spot.",
         {{"title", "Event name", "Brick Fest"}, {"when", "When it starts (on Discord, in each reader's own time zone)", "Sat 10/14 9:00 AM CDT"},
          {"name", "Member's name", "Ann B."}}, {}, 2000},
        {"dm.loan_reminder", kGroupDirect, "Borrowed item due",
         "Sent once when an item a member borrowed reaches its due date.",
         "Please return: {item}", "📦 Friendly reminder: **{item}** you borrowed from the LUG was due back {due}. "
                                  "Please get it back to a chapter lead or admin - thanks!",
         {{"item", "What they borrowed (with quantity)", "2 x Display table"}, {"due", "\"today\" or \"on <date>\"", "today"},
          {"name", "Member's name", "Ann B."}}, {}, 2000},
        {"dm.dues_reminder", kGroupDirect, "Dues renewal reminder",
         "Sent before a member's dues run out (when dues reminders are on).",
         "Your LUG dues run out on {paid_until}",
         "Hi {name}! Your LUG membership dues are paid through {paid_until}. Please renew before then to keep your member perks.",
         {{"name", "Member's name", "Ann B."}, {"paid_until", "Paid-through date", "2026-12-31"}}, {}, 2000},
        {"dm.digest", kGroupDirect, "Weekly digest",
         "The Monday-morning summary (when the weekly digest is on). {items} is the list LUG Manager builds.",
         "Your LUG week", "Hi {name}! Here's your LUG week:\n{items}",
         {{"name", "Member's name", "Ann B."}, {"items", "This week's meetings, events, RSVPs, shifts and due items", "- Tue 7:00 PM: October meeting"}},
         {"items"}, 4000},

        // ── Sign-in emails ──
        {"email.sign_in_link", kGroupSignIn, "Sign-in link",
         "Emailed when a member asks for a sign-in link.",
         "Your LUG Manager sign-in link",
         "Use this link to sign in to LUG Manager. It works once and expires in 15 minutes:\n\n{link}\n\n"
         "If you didn't ask for this, you can ignore this email.",
         {{"link", "The one-time sign-in link", "https://lug.example.org/auth/email/..."}, {"name", "Member's name", "Ann B."}},
         {"link"}, 0},
        {"email.confirm", kGroupSignIn, "Confirm an email address",
         "Emailed when a member adds or changes their own email, to check it's theirs.",
         "Confirm your email for LUG Manager",
         "Please confirm that this is your email address for LUG Manager. The link works for 48 hours:\n\n{link}\n\n"
         "Until you do, it can't be used to sign in or get emails. If you didn't add it, you can ignore this email.",
         {{"link", "The confirmation link", "https://lug.example.org/account/confirm-email/..."}, {"name", "Member's name", "Ann B."}},
         {"link"}, 0},
        {"email.password_reset", kGroupSignIn, "Forgot password",
         "Emailed when a member asks to reset their password.",
         "Set a new LUG Manager password",
         "Someone (hopefully you) asked to set a new password for your LUG Manager account. Use this link within an hour; "
         "it works once:\n\n{link}\n\nIf you didn't ask for this, ignore this email: your password stays as it is.",
         {{"link", "The one-time link", "https://lug.example.org/auth/reset/..."}, {"name", "Member's name", "Ann B."}},
         {"link"}, 0},
        {"email.password_link", kGroupSignIn, "Set-password link from an admin",
         "Emailed when an admin sends a member a set-password link.",
         "Set your LUG Manager password",
         "A LUG admin made you a link to set a password for LUG Manager. It works once, for 24 hours:\n\n{link}\n\n"
         "Then sign in with {email} and that password.",
         {{"link", "The one-time link", "https://lug.example.org/auth/reset/..."}, {"email", "Their sign-in email", "ann@example.org"},
          {"name", "Member's name", "Ann B."}}, {"link"}, 0},
        {"email.password_changed", kGroupSignIn, "Password changed",
         "Emailed when a member changes their password.",
         "Your LUG Manager password was changed",
         "The password for your LUG Manager account was just changed. If that wasn't you, ask a LUG admin to lock your account.",
         {{"name", "Member's name", "Ann B."}}, {}, 0},
    };
    return defs;
}
#undef LUGM_EVENT_PH

inline const TemplateDef* find_template(const std::string& key) {
    for (const auto& d : all_templates()) if (key == d.key) return &d;
    return nullptr;
}

// ── Rendering ──

namespace detail {
// Replaces {name} with its value; records whether any placeholder was seen
// and whether all of them were empty.
inline std::string substitute(const std::string& s, const Values& v, bool& saw, bool& all_empty) {
    std::string out;
    saw = false;
    all_empty = true;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s.compare(i, 2, "{{") == 0) { out += '{'; ++i; continue; }
        if (s.compare(i, 2, "}}") == 0) { out += '}'; ++i; continue; }
        if (s[i] == '{') {
            size_t end = s.find('}', i);
            std::string name = end == std::string::npos ? "" : s.substr(i + 1, end - i - 1);
            bool ok = !name.empty() && name.size() <= 40 &&
                      name.find_first_not_of("abcdefghijklmnopqrstuvwxyz_0123456789") == std::string::npos;
            if (ok) {
                saw = true;
                auto it = v.find(name);
                std::string val = it == v.end() ? "" : it->second;
                if (!val.empty()) all_empty = false;
                out += val;
                i = end;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}
} // namespace detail

inline std::string render(const std::string& tmpl, const Values& v) {
    // 1. Optional parts [[ ... ]]
    std::string s;
    for (size_t i = 0; i < tmpl.size();) {
        size_t open = tmpl.find("[[", i);
        if (open == std::string::npos) { s += tmpl.substr(i); break; }
        size_t close = tmpl.find("]]", open + 2);
        if (close == std::string::npos) { s += tmpl.substr(i); break; }
        s += tmpl.substr(i, open - i);
        std::string inner = tmpl.substr(open + 2, close - open - 2);
        bool any_empty = false;
        for (size_t j = 0; j < inner.size(); ++j) {
            if (inner[j] != '{' || inner.compare(j, 2, "{{") == 0) { if (inner.compare(j, 2, "{{") == 0) ++j; continue; }
            size_t e = inner.find('}', j);
            if (e == std::string::npos) break;
            auto it = v.find(inner.substr(j + 1, e - j - 1));
            if (it == v.end() || it->second.empty()) any_empty = true;
            j = e;
        }
        if (!any_empty) s += inner;
        i = close + 2;
    }
    // 2. Placeholders, line by line; a line whose placeholders are all empty is dropped
    std::string out;
    size_t pos = 0;
    bool first = true;
    while (pos <= s.size()) {
        size_t nl = s.find('\n', pos);
        std::string line = s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        bool saw = false, all_empty = true;
        std::string r = detail::substitute(line, v, saw, all_empty);
        if (!(saw && all_empty)) {
            while (!r.empty() && (r.back() == ' ' || r.back() == '\t' || r.back() == '\r')) r.pop_back();
            if (!first) out += '\n';
            out += r;
            first = false;
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    // 3. No leading/trailing blank lines, at most one blank line in a row
    std::string tidy;
    int blanks = 0;
    size_t i = 0;
    while (i < out.size() && out[i] == '\n') ++i;
    for (; i < out.size(); ++i) {
        if (out[i] == '\n') { if (++blanks > 2) continue; } else blanks = 0;
        tidy += out[i];
    }
    while (!tidy.empty() && (tidy.back() == '\n' || tidy.back() == ' ')) tidy.pop_back();
    return tidy;
}

// Placeholder names used in a template that the message doesn't provide.
inline std::vector<std::string> unknown_placeholders(const TemplateDef& d, const std::string& tmpl) {
    std::set<std::string> known;
    for (const auto& p : d.placeholders) known.insert(p.name);
    std::vector<std::string> out;
    static const std::regex re(R"(\{([a-z_0-9]{1,40})\})");
    std::string t;
    for (size_t i = 0; i < tmpl.size(); ++i) {           // drop escaped braces first
        if (tmpl.compare(i, 2, "{{") == 0 || tmpl.compare(i, 2, "}}") == 0) { ++i; continue; }
        t += tmpl[i];
    }
    for (std::sregex_iterator it(t.begin(), t.end(), re), end; it != end; ++it) {
        std::string n = (*it)[1].str();
        if (!known.count(n) && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
    return out;
}

inline Values sample_values(const TemplateDef& d) {
    Values v;
    for (const auto& p : d.placeholders) v[p.name] = p.sample;
    return v;
}

// ── Stored overrides ──
class TemplateStore {
public:
    explicit TemplateStore(SqliteDatabase& db) : db_(db) {}

    std::string body(const std::string& key) { return get("tmpl." + key, def(key) ? def(key)->default_body : ""); }
    std::string subject(const std::string& key) { return get("tmpl." + key + ".subject", def(key) ? def(key)->default_subject : ""); }
    bool customized(const std::string& key) { return has("tmpl." + key) || has("tmpl." + key + ".subject"); }

    void save(const std::string& key, const std::string& subject, const std::string& body) {
        const TemplateDef* d = def(key);
        if (!d) return;
        put("tmpl." + key, body == d->default_body ? "" : body);
        put("tmpl." + key + ".subject", subject == d->default_subject ? "" : subject);
    }
    void reset(const std::string& key) {
        put("tmpl." + key, "");
        put("tmpl." + key + ".subject", "");
    }

    std::string render_body(const std::string& key, const Values& v) { return render(body(key), v); }
    std::string render_subject(const std::string& key, const Values& v) { return render(subject(key), v); }

private:
    static const TemplateDef* def(const std::string& key) { return find_template(key); }
    std::string get(const std::string& k, const std::string& fallback) {
        auto st = db_.prepare("SELECT value FROM lug_settings WHERE key=?");
        st.bind(1, k);
        if (st.step() && !st.col_text(0).empty()) return st.col_text(0);
        return fallback;
    }
    bool has(const std::string& k) {
        auto st = db_.prepare("SELECT 1 FROM lug_settings WHERE key=? AND value<>''");
        st.bind(1, k);
        return st.step();
    }
    void put(const std::string& k, const std::string& v) {
        if (v.empty()) {
            auto st = db_.prepare("DELETE FROM lug_settings WHERE key=?");
            st.bind(1, k);
            st.step();
            return;
        }
        auto st = db_.prepare("INSERT OR REPLACE INTO lug_settings (key, value) VALUES (?, ?)");
        st.bind(1, k); st.bind(2, v);
        st.step();
    }
    SqliteDatabase& db_;
};

} // namespace chat
