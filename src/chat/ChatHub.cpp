#include "chat/ChatHub.hpp"
#include "repositories/events/EventBlocks.hpp"
#include "chat/Format.hpp"
#include "services/Features.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "utils/text/Utf8.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

namespace chat {

// A show's listing on a chat service: from its first public block to its
// last (event_blocks), or whole days when it has no hours.
static ScheduledEvent show_event(SqliteDatabase& db, const LugEvent& e) {
    if (auto span = event_blocks::public_span(event_blocks::list(db, e.id)))
        return ScheduledEvent{e.title, e.description, e.location, span->first, span->second};
    return event_days(e.title, e.description, e.location, e.start_time, e.end_time);
}

namespace {
const std::set<std::string> kEventParts{"announce", "chapter_announce", "thread", "scheduled", "update_note"};
const std::set<std::string> kMeetingParts{"announce", "scheduled"};

bool lug_scope(const std::string& scope) { return scope == "lug_wide" || scope == "non_lug"; }
std::string pings_csv(const std::string& v) { return v == "\x01" ? "" : v; }
}

// ── Settings ──

std::string ChatHub::setting(const std::string& key, const std::string& def) const {
    auto st = db_.prepare("SELECT value FROM lug_settings WHERE key=?");
    st.bind(1, key);
    return st.step() ? st.col_text(0) : def;
}

Provider* ChatHub::provider(const std::string& id) const {
    for (const auto& p : providers_) if (p->id() == id) return p.get();
    return nullptr;
}

bool ChatHub::switch_on(const Provider& p, const std::string& name) const {
    if (auto own = p.own_switch(name)) return *own;
    return setting("chat." + p.id() + "." + name, "1") != "0";
}

void ChatHub::set_switch(const std::string& provider, const std::string& name, bool on) {
    auto st = db_.prepare("INSERT OR REPLACE INTO lug_settings (key, value) VALUES (?, ?)");
    st.bind(1, "chat." + provider + "." + name); st.bind(2, std::string(on ? "1" : "0"));
    st.step();
}

bool ChatHub::quiet() const { return setting("chat.quiet", "0") == "1"; }

std::string ChatHub::lug_name() const {
    std::string n = setting("lug_name", "");
    return n.empty() ? "the LUG" : n;
}

std::string ChatHub::timezone() const {
    if (tz_source_) { std::string tz = tz_source_(); if (!tz.empty()) return tz; }
    return setting("lug_timezone", "UTC");
}

bool ChatHub::can_dm(int64_t member_id) const {
    if (quiet()) return false;
    for (const auto& p : providers_)
        if (p->ready() && p->caps().direct_messages && switch_on(*p, "dms") && !p->member_account(member_id).empty()) return true;
    return false;
}

std::set<std::string> ChatHub::skipped(const std::string& entity_type, int64_t id) const {
    std::set<std::string> out;
    auto st = db_.prepare("SELECT skip FROM chat_item_options WHERE entity_type=? AND entity_id=?");
    st.bind(1, entity_type); st.bind(2, id);
    if (st.step()) for (const auto& s : fmt::csv(st.col_text(0))) out.insert(s);
    return out;
}

void ChatHub::set_skipped(const std::string& entity_type, int64_t id, const std::string& csv) {
    if (csv == "\x01") return;
    const auto& allowed = entity_type == "meeting" ? kMeetingParts : kEventParts;
    std::string clean;
    for (const auto& s : fmt::csv(csv)) if (allowed.count(s)) clean += (clean.empty() ? "" : ",") + s;
    auto st = db_.prepare("INSERT OR REPLACE INTO chat_item_options (entity_type, entity_id, skip) VALUES (?,?,?)");
    st.bind(1, entity_type); st.bind(2, id); st.bind(3, clean);
    st.step();
}

// ── Where posts went ──

ChatHub::Refs ChatHub::refs(const Provider& p, const std::string& entity_type, int64_t id) const {
    Refs r;
    if (p.id() == "discord") {
        if (entity_type == "event") {
            auto st = db_.prepare("SELECT COALESCE(discord_lug_message_id,''), COALESCE(discord_chapter_message_id,''), "
                                  "COALESCE(discord_thread_id,''), COALESCE(discord_event_id,''), discord_thread_owned "
                                  "FROM lug_events WHERE id=?");
            st.bind(1, id);
            if (st.step()) r = Refs{st.col_text(0), st.col_text(1), st.col_text(2), st.col_text(3), st.col_int(4) != 0};
        } else {
            auto st = db_.prepare("SELECT COALESCE(discord_lug_message_id,''), COALESCE(discord_chapter_message_id,''), "
                                  "COALESCE(discord_event_id,'') FROM meetings WHERE id=?");
            st.bind(1, id);
            if (st.step()) { r.announce = st.col_text(0); r.chapter_announce = st.col_text(1); r.scheduled = st.col_text(2); }
        }
        return r;
    }
    auto st = db_.prepare("SELECT purpose, ref FROM chat_posts WHERE provider=? AND entity_type=? AND entity_id=?");
    st.bind(1, p.id()); st.bind(2, entity_type); st.bind(3, id);
    while (st.step()) {
        std::string k = st.col_text(0), v = st.col_text(1);
        if (k == "announce") r.announce = v;
        else if (k == "chapter_announce") r.chapter_announce = v;
        else if (k == "thread") r.thread = v;
        else if (k == "scheduled") r.scheduled = v;
        else if (k == "thread_owned") r.thread_owned = v != "0";
        else if (k == "report") r.report = v;
        else if (k == "reminder") r.reminder = v;
    }
    return r;
}

void ChatHub::save_ref(const Provider& p, const std::string& entity_type, int64_t id, const std::string& purpose,
                       const std::string& value) {
    if (p.id() == "discord") {
        const char* table = entity_type == "event" ? "lug_events" : "meetings";
        const char* col = purpose == "announce" ? "discord_lug_message_id"
                        : purpose == "chapter_announce" ? "discord_chapter_message_id"
                        : purpose == "thread" ? "discord_thread_id" : "discord_event_id";
        if (purpose == "thread" && entity_type != "event") return;
        auto st = db_.prepare(std::string("UPDATE ") + table + " SET " + col + "=? WHERE id=?");
        st.bind(1, value); st.bind(2, id);
        st.step();
        return;
    }
    if (value.empty()) {
        auto st = db_.prepare("DELETE FROM chat_posts WHERE provider=? AND entity_type=? AND entity_id=? AND purpose=?");
        st.bind(1, p.id()); st.bind(2, entity_type); st.bind(3, id); st.bind(4, purpose);
        st.step();
        return;
    }
    auto st = db_.prepare("INSERT OR REPLACE INTO chat_posts (provider, entity_type, entity_id, purpose, ref) VALUES (?,?,?,?,?)");
    st.bind(1, p.id()); st.bind(2, entity_type); st.bind(3, id); st.bind(4, purpose); st.bind(5, value);
    st.step();
}

std::string ChatHub::extra_ref(const Provider& p, const std::string& entity_type, int64_t id, const std::string& purpose) const {
    auto st = db_.prepare("SELECT ref FROM chat_posts WHERE provider=? AND entity_type=? AND entity_id=? AND purpose=?");
    st.bind(1, p.id()); st.bind(2, entity_type); st.bind(3, id); st.bind(4, purpose);
    return st.step() ? st.col_text(0) : "";
}

void ChatHub::forget_posts(const Provider& p, const std::string& entity_type, int64_t id) {
    auto st = db_.prepare("DELETE FROM chat_posts WHERE provider=? AND entity_type=? AND entity_id=?");
    st.bind(1, p.id()); st.bind(2, entity_type); st.bind(3, id);
    st.step();
}

void ChatHub::save_owned(const Provider& p, const std::string& entity_type, int64_t id, bool owned) {
    if (p.id() == "discord") {
        if (entity_type != "event") return;
        auto st = db_.prepare("UPDATE lug_events SET discord_thread_owned=? WHERE id=?");
        st.bind(1, static_cast<int64_t>(owned ? 1 : 0)); st.bind(2, id);
        st.step();
        return;
    }
    save_ref(p, entity_type, id, "thread_owned", owned ? "1" : "0");
}

// ── Messages ──

Message ChatHub::message(const Provider& p, const std::string& key, const Values& v,
                         std::vector<std::string> roles, std::vector<std::string> users) {
    Message m;
    m.text = templates().render_body(key, v);
    if (const TemplateDef* d = find_template(key); d && d->max_len) m.text = utf8_truncate(m.text, d->max_len);
    m.roles = std::move(roles);
    m.users = std::move(users);
    // Buttons under announcements: open it on the site, and "I'm going" (events:
    // the RSVP, "lm:rsvp"; meetings: going / can't make it, "lm:mrsvp" / "lm:mno").
    if (p.caps().buttons && (key == "event.announcement" || key == "event.thread_starter" || key == "meeting.announcement")) {
        auto link = v.find("link");
        if (link != v.end() && !link->second.empty()) {
            const bool event = key != "meeting.announcement";
            const std::string id = link->second.substr(link->second.rfind('/') + 1);
            if (actions_available() && Features::on("rsvps")) {
                if (event) {
                    m.buttons.push_back({"I'm going", "", "lm:rsvp:" + id, "success"});
                } else {
                    m.buttons.push_back({"I'm going", "", "lm:mrsvp:" + id, "success"});
                    m.buttons.push_back({"Can't make it", "", "lm:mno:" + id, "secondary"});
                }
            }
            m.buttons.push_back({event ? "View event" : "View meeting", link->second, "", ""});
        }
    }
    return m;
}

void ChatHub::log(const Provider& p, const std::string& action, const std::string& what, const std::string& entity_type,
                  int64_t entity_id, const Result& r, const std::string& channel, const std::string& payload) {
    try {
        auto st = db_.prepare("INSERT INTO chat_activity (provider, action, what, entity_type, entity_id, ok, error, channel, payload) "
                              "VALUES (?,?,?,?,?,?,?,?,?)");
        st.bind(1, p.id()); st.bind(2, action); st.bind(3, what); st.bind(4, entity_type); st.bind(5, entity_id);
        st.bind(6, static_cast<int64_t>(r.ok ? 1 : 0)); st.bind(7, r.error.substr(0, 300)); st.bind(8, channel);
        st.bind(9, r.ok ? std::string() : payload);
        st.step();
        static int n = 0;
        if (++n % 50 == 0) {   // keep the newest 2000
            auto trim = db_.prepare("DELETE FROM chat_activity WHERE id <= (SELECT MAX(id) - 2000 FROM chat_activity)");
            trim.step();
        }
    } catch (const std::exception& e) {
        std::cerr << "[chat] activity log: " << e.what() << "\n";
    }
    if (!r.ok) std::cerr << "[chat] " << p.id() << " " << action << " " << what << " failed: " << r.error << "\n";
}

bool ChatHub::may_create(const Provider& p, const std::string& what, const std::string& entity_type, int64_t id) {
    if (!quiet()) return true;
    log(p, "skip", what, entity_type, id, Result{true, "", ""});
    return false;
}

// ── Placeholder values ──

// The event lead's account on this service ("" if none).
std::string ChatHub::lead_account(const Provider& p, const LugEvent& e) const {
    std::string a = e.event_lead_id > 0 ? p.member_account(e.event_lead_id) : "";
    if (a.empty() && p.id() == "discord") a = e.event_lead_discord_id;
    return a;
}

std::vector<std::string> ChatHub::event_ping_roles(const Provider& p, const LugEvent& e, const std::string& main_role) const {
    std::vector<std::string> roles;
    if (!switch_on(p, "pings")) return roles;
    if (!main_role.empty()) roles.push_back(main_role);
    for (const auto& r : fmt::csv(pings_csv(e.discord_ping_role_ids))) roles.push_back(r);
    return roles;
}

std::string ChatHub::when_text(const Provider& p, const std::string& local_iso, char style, const std::string& plain) const {
    if (local_iso.size() < 16) return plain;
    const std::time_t t = DiscordClient::local_to_epoch(local_iso, timezone());
    return t > 0 ? p.time(t, style, plain) : plain;
}

std::string ChatHub::when_range(const Provider& p, const std::string& start, const std::string& end) const {
    const std::string plain = fmt::time_range(start, end, timezone());
    const std::string a = when_text(p, start, 'F', "");
    if (a.empty()) return plain;                                   // no reader-local times here
    if (end.size() < 16 || end == start) return a;
    const std::string b = when_text(p, end, end.substr(0, 10) == start.substr(0, 10) ? 't' : 'F', "");
    return b.empty() ? a : a + " – " + b;
}

Values ChatHub::event_values(const LugEvent& e, const Provider& p) const {
    Values v;
    v["title"] = p.inert(e.title);
    v["dates"] = fmt::range(e.start_time, e.end_time);
    v["dates_year"] = fmt::range_year(e.start_time, e.end_time);
    v["location"] = p.inert(e.location);
    v["location_short"] = p.inert(fmt::short_place(e.location));
    v["description"] = p.inert(e.description);
    v["non_lug"] = e.scope == "non_lug" ? "[External] " : "";
    v["fee"] = p.inert(e.entrance_fee);
    v["link"] = public_url().empty() || e.id <= 0 ? "" : public_url() + "/events/" + std::to_string(e.id);
    v["signup_deadline"] = e.signup_deadline.empty() ? "" : fmt::md(e.signup_deadline);
    v["capacity"] = e.max_attendees > 0 ? std::to_string(e.max_attendees) : "";
    std::string lead = lead_account(p, e);
    if (switch_on(p, "pings") && !lead.empty()) v["lead"] = p.user_mention(lead);
    else v["lead"] = p.inert(e.event_lead_name);
    v["when"] = when_text(p, e.start_time, 'F', DiscordClient::friendly_time(e.start_time, timezone()));
    v["when_relative"] = when_text(p, e.start_time, 'R', "");
    return v;
}

Values ChatHub::meeting_values(const Meeting& m, const Provider& p) const {
    Values v;
    v["title"] = p.inert(m.title);
    v["when_range"] = when_range(p, m.start_time, m.end_time);
    v["when"] = when_text(p, m.start_time, 'F', DiscordClient::friendly_time(m.start_time, timezone()));
    v["when_relative"] = when_text(p, m.start_time, 'R', "");
    v["location"] = p.inert(m.location);
    v["description"] = p.inert(m.description);
    v["link"] = public_url().empty() || m.id <= 0 ? "" : public_url() + "/meetings/" + std::to_string(m.id);
    if (m.chapter_id > 0) {
        auto st = db_.prepare("SELECT name FROM chapters WHERE id=?");
        st.bind(1, m.chapter_id);
        if (st.step()) v["chapter"] = p.inert(st.col_text(0));
    }
    return v;
}

// ── Events ──

// Private meetings and events on a chat service (setting chat.<provider>.private):
// "post" as usual (default), "redact" (no title, place or description), or
// "skip" (not posted at all, like "Don't post to Discord").
std::string ChatHub::private_mode(const Provider& p) const {
    const std::string m = setting("chat." + p.id() + ".private", "post");
    return m == "redact" || m == "skip" ? m : "post";
}

LugEvent ChatHub::view(const Provider& p, LugEvent e) const {
    if (!e.is_private) return e;
    const std::string mode = private_mode(p);
    if (mode == "skip") e.suppress_discord = true;
    else if (mode == "redact") { e.title = "Private LUG event"; e.description = ""; e.location = ""; e.notes = ""; e.entrance_fee = ""; }
    return e;
}

Meeting ChatHub::view(const Provider& p, Meeting m) const {
    if (!m.is_private) return m;
    const std::string mode = private_mode(p);
    if (mode == "skip") m.suppress_discord = true;
    else if (mode == "redact") { m.title = "Private LUG meeting"; m.description = ""; m.location = ""; m.notes = ""; }
    return m;
}

void ChatHub::event_published(const LugEvent& ev) {
    for (auto& p : providers_) {
        if (!p->ready()) continue;
        const LugEvent e = view(*p, ev);
        if (!e.suppress_discord) publish_event(*p, e);
    }
}

void ChatHub::event_changed(const LugEvent& before_, const LugEvent& after_, bool notify) {
    for (auto& p : providers_) {
        if (!p->ready()) continue;
        const LugEvent before = view(*p, before_), after = view(*p, after_);
        if (!before.suppress_discord && after.suppress_discord) remove_event(*p, before, refs(*p, "event", before.id));
        else if (before.suppress_discord && !after.suppress_discord) publish_event(*p, after);
        else if (!after.suppress_discord) update_event(*p, before, after, notify);
    }
}

void ChatHub::event_removed(const LugEvent& e, bool thread_owned) {
    for (auto& p : providers_) {
        if (!p->ready()) continue;
        Refs r = refs(*p, "event", e.id);
        if (p->id() == "discord") {  // the row may already be gone: use the ids it had
            r = Refs{e.discord_lug_message_id, e.discord_chapter_message_id, e.discord_thread_id, e.discord_event_id, thread_owned};
            r.report = e.notes_discord_post_id;
            r.reminder = extra_ref(*p, "event", e.id, "reminder");
        }
        remove_event(*p, e, r);
        forget_posts(*p, "event", e.id);
    }
}

void ChatHub::publish_event(Provider& p, const LugEvent& e) {
    auto skip = skipped("event", e.id);
    Refs r = refs(p, "event", e.id);
    Values v = event_values(e, p);
    const std::string ann_ch = p.place(Place::Announcements);
    const std::string forum = p.place(Place::EventsForum);
    std::string title = templates().render_body("event.thread_title", v);
    title = utf8_truncate(title, 100);
    bool want_thread = switch_on(p, "event_thread") && !skip.count("thread");

    // 1. The discussion thread first (so the announcement can link to it), unless one was picked.
    std::string thread = r.thread;
    if (thread.empty() && want_thread && p.caps().forums && !forum.empty() && may_create(p, "event.thread_starter", "event", e.id)) {
        Message first = message(p, "event.thread_starter", [&] {
            Values w = v;
            std::string pings;
            if (switch_on(p, "pings")) for (const auto& role : fmt::csv(pings_csv(e.discord_ping_role_ids))) pings += p.role_mention(role) + " ";
            w["pings"] = pings;
            return w;
        }(), switch_on(p, "pings") ? fmt::csv(pings_csv(e.discord_ping_role_ids)) : std::vector<std::string>{},
           (switch_on(p, "pings") && !lead_account(p, e).empty()) ? std::vector<std::string>{lead_account(p, e)} : std::vector<std::string>{});
        Result t = p.start_forum_thread(forum, title, first);
        log(p, "thread", "event.thread_starter", "event", e.id, t, forum);
        if (t.ok) {
            thread = t.id;
            save_ref(p, "event", e.id, "thread", thread);
            save_owned(p, "event", e.id, true);
        }
    }

    // 2. The announcement
    std::string role = p.announcement_role(e.scope == "non_lug");
    auto roles = event_ping_roles(p, e, role);
    auto announce_values = [&](const std::string& thread_id, const std::vector<std::string>& rs) {
        Values w = v;
        std::string pings;
        for (const auto& x : rs) pings += p.role_mention(x) + " ";
        w["pings"] = pings;
        w["thread_link"] = p.thread_url(thread_id);
        return w;
    };
    std::string announce;
    if (switch_on(p, "event_announce") && !skip.count("announce") && !ann_ch.empty() && may_create(p, "event.announcement", "event", e.id)) {
        Result a = p.post(ann_ch, message(p, "event.announcement", announce_values(thread, roles), roles));
        log(p, "post", "event.announcement", "event", e.id, a, ann_ch);
        if (a.ok) { announce = a.id; save_ref(p, "event", e.id, "announce", announce); }
    }

    // 3. No forum: a thread started from the announcement, which then gets the link
    if (thread.empty() && !announce.empty() && want_thread && p.caps().threads) {
        Result t = p.start_thread(ann_ch, announce, title);
        log(p, "thread", "event.thread_title", "event", e.id, t, ann_ch);
        if (t.ok) {
            thread = t.id;
            save_ref(p, "event", e.id, "thread", thread);
            save_owned(p, "event", e.id, true);
            Result ed = p.edit(ann_ch, announce, message(p, "event.announcement", announce_values(thread, roles), roles));
            log(p, "edit", "event.announcement", "event", e.id, ed, ann_ch);
        }
    }

    // 4. The scheduled event
    if (switch_on(p, "event_scheduled") && !skip.count("scheduled") && p.caps().scheduled_events &&
        may_create(p, "scheduled event", "event", e.id)) {
        Result s = p.create_event(show_event(db_, e));
        log(p, "event", "scheduled event", "event", e.id, s);
        if (s.ok) save_ref(p, "event", e.id, "scheduled", s.id);
    }

    // 5. The chapter's own channel
    if (e.chapter_id > 0 && switch_on(p, "event_chapter_announce") && !skip.count("chapter_announce")) {
        std::string ch = p.chapter_channel(e.chapter_id);
        if (!ch.empty() && may_create(p, "event.announcement", "event", e.id)) {
            std::string ch_role = p.chapter_role(e.chapter_id);
            if (ch_role.empty()) ch_role = p.announcement_role(false);
            auto ch_roles = event_ping_roles(p, e, ch_role);
            Result c = p.post(ch, message(p, "event.announcement", announce_values(thread, ch_roles), ch_roles));
            log(p, "post", "event.announcement", "event", e.id, c, ch);
            if (c.ok) save_ref(p, "event", e.id, "chapter_announce", c.id);
        }
    }
}

void ChatHub::update_event(Provider& p, const LugEvent& before, const LugEvent& after, bool notify) {
    auto skip = skipped("event", after.id);
    Refs r = refs(p, "event", after.id);
    Values v = event_values(after, p);
    const std::string ann_ch = p.place(Place::Announcements);

    if (!r.scheduled.empty()) {
        Result s = p.update_event(r.scheduled, show_event(db_, after));
        log(p, "edit", "scheduled event", "event", after.id, s);
    }
    // Our own thread: new name and first post. A thread someone picked isn't ours to change.
    if (!r.thread.empty() && r.thread_owned) {
        std::string title = utf8_truncate(templates().render_body("event.thread_title", v), 100);
        Result rn = p.rename_thread(r.thread, title);
        log(p, "edit", "event.thread_title", "event", after.id, rn, r.thread);
        Values w = v;
        std::string pings;
        auto extra = switch_on(p, "pings") ? fmt::csv(pings_csv(after.discord_ping_role_ids)) : std::vector<std::string>{};
        for (const auto& x : extra) pings += p.role_mention(x) + " ";
        w["pings"] = pings;
        Result st = p.edit_thread_starter(r.thread, message(p, "event.thread_starter", w, extra,
            (switch_on(p, "pings") && !lead_account(p, after).empty()) ? std::vector<std::string>{lead_account(p, after)}
                                                                         : std::vector<std::string>{}));
        log(p, "edit", "event.thread_starter", "event", after.id, st, r.thread);
    }

    auto announce_msg = [&](const std::string& role) {
        auto roles = event_ping_roles(p, after, role);
        Values w = v;
        std::string pings;
        for (const auto& x : roles) pings += p.role_mention(x) + " ";
        w["pings"] = pings;
        w["thread_link"] = p.thread_url(r.thread);
        return message(p, "event.announcement", w, roles);
    };
    bool scope_changed = before.scope != after.scope || before.chapter_id != after.chapter_id;
    if (scope_changed) {
        // Moved: take the old announcements down, post in the right place.
        if (!r.announce.empty() && !ann_ch.empty()) {
            Result d = p.remove(ann_ch, r.announce);
            log(p, "delete", "event.announcement", "event", after.id, d, ann_ch);
        }
        if (!r.chapter_announce.empty() && before.chapter_id > 0) {
            std::string old_ch = p.chapter_channel(before.chapter_id);
            if (!old_ch.empty()) {
                Result d = p.remove(old_ch, r.chapter_announce);
                log(p, "delete", "event.announcement", "event", after.id, d, old_ch);
            }
        }
        save_ref(p, "event", after.id, "announce", "");
        save_ref(p, "event", after.id, "chapter_announce", "");
        if (lug_scope(after.scope) && !ann_ch.empty() && switch_on(p, "event_announce") && !skip.count("announce") &&
            may_create(p, "event.announcement", "event", after.id)) {
            Result a = p.post(ann_ch, announce_msg(p.announcement_role(after.scope == "non_lug")));
            log(p, "post", "event.announcement", "event", after.id, a, ann_ch);
            if (a.ok) save_ref(p, "event", after.id, "announce", a.id);
        }
        if (after.chapter_id > 0 && switch_on(p, "event_chapter_announce") && !skip.count("chapter_announce")) {
            std::string ch = p.chapter_channel(after.chapter_id);
            if (!ch.empty() && may_create(p, "event.announcement", "event", after.id)) {
                std::string ch_role = p.chapter_role(after.chapter_id);
                if (ch_role.empty()) ch_role = p.announcement_role(false);
                Result c = p.post(ch, announce_msg(ch_role));
                log(p, "post", "event.announcement", "event", after.id, c, ch);
                if (c.ok) save_ref(p, "event", after.id, "chapter_announce", c.id);
            }
        }
    } else {
        if (!r.announce.empty() && !ann_ch.empty()) {
            Result ed = p.edit(ann_ch, r.announce, announce_msg(p.announcement_role(after.scope == "non_lug")));
            log(p, "edit", "event.announcement", "event", after.id, ed, ann_ch);
        }
        if (!r.chapter_announce.empty() && after.chapter_id > 0) {
            std::string ch = p.chapter_channel(after.chapter_id);
            if (!ch.empty()) {
                std::string ch_role = p.chapter_role(after.chapter_id);
                if (ch_role.empty()) ch_role = p.announcement_role(false);
                Result ed = p.edit(ch, r.chapter_announce, announce_msg(ch_role));
                log(p, "edit", "event.announcement", "event", after.id, ed, ch);
            }
        }
    }

    // A note in the thread that it changed (no pings)
    if (notify && switch_on(p, "update_notes") && !skip.count("update_note") && !r.thread.empty() &&
        may_create(p, "event.updated", "event", after.id)) {
        Result n = p.post(r.thread, message(p, "event.updated", v));
        log(p, "post", "event.updated", "event", after.id, n, r.thread);
    }
}

void ChatHub::remove_event(Provider& p, const LugEvent& e, const Refs& r) {
    if (!r.scheduled.empty()) log(p, "delete", "scheduled event", "event", e.id, p.remove_event(r.scheduled));
    if (!r.thread.empty() && r.thread_owned) log(p, "delete", "event thread", "event", e.id, p.remove_thread(r.thread), r.thread);
    std::string ann_ch = p.place(Place::Announcements);
    if (!r.announce.empty() && !ann_ch.empty())
        log(p, "delete", "event.announcement", "event", e.id, p.remove(ann_ch, r.announce), ann_ch);
    if (!r.chapter_announce.empty() && e.chapter_id > 0) {
        std::string ch = p.chapter_channel(e.chapter_id);
        if (!ch.empty()) log(p, "delete", "event.announcement", "event", e.id, p.remove(ch, r.chapter_announce), ch);
    }
    remove_extras(p, "event", e.id, r);
    for (const char* k : {"scheduled", "thread", "announce", "chapter_announce"}) save_ref(p, "event", e.id, k, "");
}

// The report thread and the reminder post, when deleting an event or meeting.
void ChatHub::remove_extras(Provider& p, const std::string& entity_type, int64_t id, const Refs& r) {
    if (!r.report.empty())
        log(p, "delete", "report thread", entity_type, id, p.remove_thread(r.report), r.report);
    if (!r.reminder.empty()) {
        const auto bar = r.reminder.find('|');
        const std::string ch = r.reminder.substr(0, bar), msg = bar == std::string::npos ? "" : r.reminder.substr(bar + 1);
        // A reminder in the event's own thread went with the thread
        const bool gone_with_thread = !r.thread.empty() && r.thread_owned && ch == r.thread;
        if (!msg.empty() && !gone_with_thread)
            log(p, "delete", "reminder", entity_type, id, p.remove(ch, msg), ch);
    }
}

std::string ChatHub::start_event_thread(const LugEvent& e) {
    for (auto& p : providers_) {
        if (!p->ready() || !p->caps().forums) continue;
        std::string forum = p->place(Place::EventsForum);
        if (forum.empty() || !may_create(*p, "event.thread_starter", "event", e.id)) continue;
        Values v = event_values(e, *p);
        std::vector<std::string> extra = switch_on(*p, "pings") ? fmt::csv(pings_csv(e.discord_ping_role_ids)) : std::vector<std::string>{};
        std::string pings;
        for (const auto& x : extra) pings += p->role_mention(x) + " ";
        v["pings"] = pings;
        std::string title = utf8_truncate(templates().render_body("event.thread_title", v), 100);
        Result t = p->start_forum_thread(forum, title, message(*p, "event.thread_starter", v, extra));
        log(*p, "thread", "event.thread_starter", "event", e.id, t, forum);
        if (t.ok) return t.id;
    }
    return "";
}

// ── Meetings ──

void ChatHub::meeting_published(const Meeting& mt) {
    for (auto& p : providers_) {
        if (!p->ready()) continue;
        const Meeting m = view(*p, mt);
        if (!m.suppress_discord) publish_meeting(*p, m);
    }
}

void ChatHub::meeting_changed(const Meeting& before_, const Meeting& after_) {
    for (auto& p : providers_) {
        if (!p->ready()) continue;
        const Meeting before = view(*p, before_), after = view(*p, after_);
        if (!before.suppress_discord && after.suppress_discord) remove_meeting(*p, before, refs(*p, "meeting", before.id));
        else if (before.suppress_discord && !after.suppress_discord) publish_meeting(*p, after);
        else if (!after.suppress_discord) update_meeting(*p, before, after);
    }
}

void ChatHub::meeting_removed(const Meeting& m) {
    for (auto& p : providers_) {
        if (!p->ready()) continue;
        Refs r = refs(*p, "meeting", m.id);
        if (p->id() == "discord") {
            r = Refs{m.discord_lug_message_id, m.discord_chapter_message_id, "", m.discord_event_id, true};
            r.report = m.notes_discord_post_id;
            r.reminder = extra_ref(*p, "meeting", m.id, "reminder");
        }
        remove_meeting(*p, m, r);
        forget_posts(*p, "meeting", m.id);
    }
}

namespace {
struct Target { std::string channel, role, purpose; };
// Where a meeting's announcement goes: the LUG channel for LUG-wide/non-LUG,
// the chapter's channel for chapter meetings.
std::optional<Target> meeting_target(const Provider& p, const Meeting& m) {
    if (lug_scope(m.scope)) {
        std::string ch = p.place(Place::Announcements);
        if (ch.empty()) return std::nullopt;
        return Target{ch, p.announcement_role(m.scope == "non_lug"), "announce"};
    }
    if (m.scope == "chapter" && m.chapter_id > 0) {
        std::string ch = p.chapter_channel(m.chapter_id);
        if (ch.empty()) return std::nullopt;
        std::string role = p.chapter_role(m.chapter_id);
        if (role.empty()) role = p.announcement_role(false);
        return Target{ch, role, "chapter_announce"};
    }
    return std::nullopt;
}
}

void ChatHub::publish_meeting(Provider& p, const Meeting& m) {
    auto skip = skipped("meeting", m.id);
    if (switch_on(p, "meeting_scheduled") && !skip.count("scheduled") && p.caps().scheduled_events &&
        may_create(p, "scheduled event", "meeting", m.id)) {
        Result s = p.create_event(ScheduledEvent{m.title, m.description, m.location, m.start_time, m.end_time});
        log(p, "event", "scheduled event", "meeting", m.id, s);
        if (s.ok) save_ref(p, "meeting", m.id, "scheduled", s.id);
    }
    if (!switch_on(p, "meeting_announce") || skip.count("announce")) return;
    auto t = meeting_target(p, m);
    if (!t || !may_create(p, "meeting.announcement", "meeting", m.id)) return;
    std::vector<std::string> roles;
    if (switch_on(p, "pings") && !t->role.empty()) roles.push_back(t->role);
    Values v = meeting_values(m, p);
    v["pings"] = roles.empty() ? "" : p.role_mention(t->role);
    Result a = p.post(t->channel, message(p, "meeting.announcement", v, roles));
    log(p, "post", "meeting.announcement", "meeting", m.id, a, t->channel);
    if (a.ok) save_ref(p, "meeting", m.id, t->purpose, a.id);
}

void ChatHub::update_meeting(Provider& p, const Meeting& before, const Meeting& after) {
    auto skip = skipped("meeting", after.id);
    Refs r = refs(p, "meeting", after.id);
    if (!r.scheduled.empty()) {
        Result s = p.update_event(r.scheduled, ScheduledEvent{after.title, after.description, after.location, after.start_time, after.end_time});
        log(p, "edit", "scheduled event", "meeting", after.id, s);
    }
    bool scope_changed = before.scope != after.scope || before.chapter_id != after.chapter_id;
    std::string ann_ch = p.place(Place::Announcements);
    if (scope_changed) {
        if (!r.announce.empty() && !ann_ch.empty())
            log(p, "delete", "meeting.announcement", "meeting", after.id, p.remove(ann_ch, r.announce), ann_ch);
        if (!r.chapter_announce.empty() && before.chapter_id > 0) {
            std::string old_ch = p.chapter_channel(before.chapter_id);
            if (!old_ch.empty())
                log(p, "delete", "meeting.announcement", "meeting", after.id, p.remove(old_ch, r.chapter_announce), old_ch);
        }
        save_ref(p, "meeting", after.id, "announce", "");
        save_ref(p, "meeting", after.id, "chapter_announce", "");
        if (switch_on(p, "meeting_announce") && !skip.count("announce")) {
            auto t = meeting_target(p, after);
            if (t && may_create(p, "meeting.announcement", "meeting", after.id)) {
                std::vector<std::string> roles;
                if (switch_on(p, "pings") && !t->role.empty()) roles.push_back(t->role);
                Values v = meeting_values(after, p);
                v["pings"] = roles.empty() ? "" : p.role_mention(t->role);
                Result a = p.post(t->channel, message(p, "meeting.announcement", v, roles));
                log(p, "post", "meeting.announcement", "meeting", after.id, a, t->channel);
                if (a.ok) save_ref(p, "meeting", after.id, t->purpose, a.id);
            }
        }
        return;
    }
    // Same place: edit in place
    auto t = meeting_target(p, after);
    if (!t) return;
    std::string existing = t->purpose == "announce" ? r.announce : r.chapter_announce;
    if (existing.empty()) return;
    std::vector<std::string> roles;
    if (switch_on(p, "pings") && !t->role.empty()) roles.push_back(t->role);
    Values v = meeting_values(after, p);
    v["pings"] = roles.empty() ? "" : p.role_mention(t->role);
    Result ed = p.edit(t->channel, existing, message(p, "meeting.announcement", v, roles));
    log(p, "edit", "meeting.announcement", "meeting", after.id, ed, t->channel);
}

void ChatHub::remove_meeting(Provider& p, const Meeting& m, const Refs& r) {
    if (!r.scheduled.empty()) log(p, "delete", "scheduled event", "meeting", m.id, p.remove_event(r.scheduled));
    std::string ann_ch = p.place(Place::Announcements);
    if (!r.announce.empty() && !ann_ch.empty())
        log(p, "delete", "meeting.announcement", "meeting", m.id, p.remove(ann_ch, r.announce), ann_ch);
    if (!r.chapter_announce.empty() && m.chapter_id > 0) {
        std::string ch = p.chapter_channel(m.chapter_id);
        if (!ch.empty()) log(p, "delete", "meeting.announcement", "meeting", m.id, p.remove(ch, r.chapter_announce), ch);
    }
    remove_extras(p, "meeting", m.id, r);
    for (const char* k : {"scheduled", "announce", "chapter_announce"}) save_ref(p, "meeting", m.id, k, "");
}

// ── Standalone posts ──

Result ChatHub::post_in(Provider& p, const std::string& channel, const std::string& key, const Values& v,
                        const std::string& entity_type, int64_t entity_id) {
    if (channel.empty()) return Result{false, "", "no channel set"};
    if (!may_create(p, key, entity_type, entity_id)) return Result{false, "", "quiet mode"};
    Values safe;
    for (const auto& [k, val] : v) safe[k] = p.inert(val);   // member-supplied text can't ping anyone
    Message m = message(p, key, safe);
    Result r = p.post(channel, m);
    log(p, "post", key, entity_type, entity_id, r, channel, m.text);
    return r;
}

int ChatHub::post_to(Place place, int64_t chapter_id, const std::string& key, const Values& v,
                     const std::string& entity_type, int64_t entity_id, const std::string& switch_name) {
    int sent = 0;
    for (auto& p : providers_) {
        if (!p->ready() || (!switch_name.empty() && !switch_on(*p, switch_name))) continue;
        std::string ch = chapter_id > 0 ? p->chapter_channel(chapter_id) : p->place(place);
        if (ch.empty()) continue;
        if (post_in(*p, ch, key, v, entity_type, entity_id).ok) ++sent;
    }
    return sent;
}

Result ChatHub::publish_report(Provider& p, Place forum_place, const std::string& existing_thread, const std::string& key,
                               const Values& v, const std::string& entity_type, int64_t entity_id) {
    Message body = message(p, key, v);
    if (!existing_thread.empty()) {
        Result r = p.edit_thread_starter(existing_thread, body);
        log(p, "edit", key, entity_type, entity_id, r, existing_thread);
        if (r.ok) r.id = existing_thread;
        return r;
    }
    std::string forum = p.place(forum_place);
    if (forum.empty()) forum = p.place(Place::EventsForum);
    if (forum.empty()) return Result{false, "", "no reports forum set"};
    if (!may_create(p, key, entity_type, entity_id)) return Result{false, "", "quiet mode"};
    std::string title = utf8_truncate(templates().render_body(key + ".title", v), 100);
    Result r = p.start_forum_thread(forum, title, body);
    log(p, "thread", key, entity_type, entity_id, r, forum);
    return r;
}

namespace {
std::string chapter_name_of(SqliteDatabase& db, int64_t chapter_id) {
    if (chapter_id <= 0) return "Group-wide";
    auto st = db.prepare("SELECT name FROM chapters WHERE id=?");
    st.bind(1, chapter_id);
    return st.step() ? st.col_text(0) : "Group-wide";
}
}

Values ChatHub::event_report_values(const LugEvent& e) const {
    Values v;
    v["title"] = e.title;
    v["chapter"] = chapter_name_of(db_, e.chapter_id);
    v["start_date"] = e.start_time.substr(0, 10);
    v["end_date"] = e.end_time.substr(0, 10);
    v["location"] = e.location;
    v["lead"] = e.event_lead_name;
    v["fee"] = e.entrance_fee;
    std::string att;
    auto days = db_.prepare("SELECT id, day_number FROM event_days WHERE event_id=? ORDER BY day_number");
    days.bind(1, e.id);
    std::vector<std::pair<int64_t, int64_t>> list;
    while (days.step()) list.emplace_back(days.col_int(0), days.col_int(1));
    for (const auto& [day_id, n] : list) {
        att += (att.empty() ? "" : "\n") + std::string("**Member names day") + std::to_string(n) + ":**";
        auto st = db_.prepare("SELECT COALESCE(m.display_name,'') FROM event_day_attendance a JOIN members m ON m.id = a.member_id "
                              "WHERE a.event_day_id=? ORDER BY a.checked_in_at");
        st.bind(1, day_id);
        bool any = false;
        while (st.step()) { att += "\n- " + st.col_text(0); any = true; }
        if (!any) att += "\n- (none)";
    }
    v["attendance"] = att;
    v["public_kids"] = std::to_string(e.public_kids);
    v["public_teens"] = std::to_string(e.public_teens);
    v["public_adults"] = std::to_string(e.public_adults);
    v["social_links"] = e.social_media_links;
    v["feedback"] = e.event_feedback;
    v["description"] = e.description;
    v["notes"] = e.notes;
    v["lug_name"] = lug_name();
    return v;
}

Values ChatHub::meeting_report_values(const Meeting& m) const {
    Values v;
    v["title"] = m.title;
    v["chapter"] = chapter_name_of(db_, m.chapter_id);
    v["date"] = m.start_time.substr(0, 10);
    v["format"] = m.is_virtual ? "Virtual" : "";
    v["location"] = m.is_virtual ? "" : m.location;
    std::string in_person, online;
    auto st = db_.prepare("SELECT COALESCE(mb.display_name,''), a.is_virtual FROM attendance a JOIN members mb ON mb.id = a.member_id "
                          "WHERE a.entity_type='meeting' AND a.entity_id=? ORDER BY a.checked_in_at");
    st.bind(1, m.id);
    while (st.step()) {
        if (st.col_int(1)) online += "\n- " + st.col_text(0) + " (virtual)";
        else in_person += "\n- " + st.col_text(0);
    }
    std::string all = in_person + online;
    v["attendance"] = all.empty() ? "- (none)" : all.substr(1);
    v["description"] = m.description;
    v["notes"] = m.notes;
    return v;
}

ChatHub::ReportOutcome ChatHub::publish_event_report(const LugEvent& e) {
    ReportOutcome out;
    for (auto& p : providers_) {
        if (!p->ready()) continue;
        Values v = event_report_values(e);
        for (auto& [k, val] : v) val = p->inert(val);
        std::string existing;
        if (p->id() == "discord") existing = e.notes_discord_post_id;
        else {
            auto st = db_.prepare("SELECT ref FROM chat_posts WHERE provider=? AND entity_type='event' AND entity_id=? AND purpose='report'");
            st.bind(1, p->id()); st.bind(2, e.id);
            if (st.step()) existing = st.col_text(0);
        }
        Result r = publish_report(*p, Place::EventReports, existing, "report.event", v, "event", e.id);
        if (!r.ok) { out.error = r.error; continue; }
        ++out.sent;
        if (r.id != existing) {
            if (p->id() == "discord") {
                auto st = db_.prepare("UPDATE lug_events SET notes_discord_post_id=? WHERE id=?");
                st.bind(1, r.id); st.bind(2, e.id);
                st.step();
            } else save_ref(*p, "event", e.id, "report", r.id);
        }
    }
    return out;
}

ChatHub::ReportOutcome ChatHub::publish_meeting_report(const Meeting& m) {
    ReportOutcome out;
    for (auto& p : providers_) {
        if (!p->ready()) continue;
        Values v = meeting_report_values(m);
        for (auto& [k, val] : v) val = p->inert(val);
        std::string existing;
        if (p->id() == "discord") existing = m.notes_discord_post_id;
        else {
            auto st = db_.prepare("SELECT ref FROM chat_posts WHERE provider=? AND entity_type='meeting' AND entity_id=? AND purpose='report'");
            st.bind(1, p->id()); st.bind(2, m.id);
            if (st.step()) existing = st.col_text(0);
        }
        Result r = publish_report(*p, Place::MeetingReports, existing, "report.meeting", v, "meeting", m.id);
        if (!r.ok) { out.error = r.error; continue; }
        ++out.sent;
        if (r.id != existing) {
            if (p->id() == "discord") {
                auto st = db_.prepare("UPDATE meetings SET notes_discord_post_id=? WHERE id=?");
                st.bind(1, r.id); st.bind(2, m.id);
                st.step();
            } else save_ref(*p, "meeting", m.id, "report", r.id);
        }
    }
    return out;
}

bool ChatHub::retry(int64_t activity_id) {
    std::string prov, channel, payload, what, et;
    int64_t eid = 0;
    {
        auto st = db_.prepare("SELECT provider, channel, payload, what, entity_type, entity_id FROM chat_activity "
                              "WHERE id=? AND ok=0 AND action='post' AND payload<>''");
        st.bind(1, activity_id);
        if (!st.step()) return false;
        prov = st.col_text(0); channel = st.col_text(1); payload = st.col_text(2); what = st.col_text(3);
        et = st.col_text(4); eid = st.col_int(5);
    }
    Provider* p = provider(prov);
    if (!p || !p->ready()) return false;
    Result r = p->post(channel, Message{payload, {}, {}});
    log(*p, "post", what + " (retry)", et, eid, r, channel, payload);
    if (r.ok) {
        auto st = db_.prepare("UPDATE chat_activity SET payload='' WHERE id=?");
        st.bind(1, activity_id);
        st.step();
    }
    return r.ok;
}

// Remember the reminder post so deleting the meeting/event removes it too.
void ChatHub::keep_reminder(const Provider& p, const std::string& entity_type, int64_t id, const std::string& channel,
                            const std::string& message) {
    if (message.empty()) return;
    auto st = db_.prepare("INSERT OR REPLACE INTO chat_posts (provider, entity_type, entity_id, purpose, ref) VALUES (?,?,?,'reminder',?)");
    st.bind(1, p.id()); st.bind(2, entity_type); st.bind(3, id); st.bind(4, channel + "|" + message);
    st.step();
}

int ChatHub::remind_meeting(const Meeting& mt) {
    int sent = 0;
    for (auto& p : providers_) {
        if (!p->ready()) continue;
        const Meeting m = view(*p, mt);
        if (m.suppress_discord) continue;
        std::string ch = m.scope == "chapter" && m.chapter_id > 0 ? p->chapter_channel(m.chapter_id) : p->place(Place::Announcements);
        if (ch.empty()) continue;
        Result r = post_in(*p, ch, "reminder.meeting", meeting_values(m, *p), "meeting", m.id);
        if (r.ok) { ++sent; keep_reminder(*p, "meeting", m.id, ch, r.id); }
    }
    return sent;
}

int ChatHub::remind_event(const LugEvent& ev) {
    int sent = 0;
    for (auto& p : providers_) {
        if (!p->ready()) continue;
        const LugEvent e = view(*p, ev);
        if (e.suppress_discord) continue;
        std::string ch = refs(*p, "event", e.id).thread;
        if (ch.empty()) ch = p->place(Place::Announcements);
        if (ch.empty()) continue;
        Result r = post_in(*p, ch, "reminder.event", event_values(e, *p), "event", e.id);
        if (r.ok) { ++sent; keep_reminder(*p, "event", e.id, ch, r.id); }
    }
    return sent;
}

// ── Direct messages ──

bool ChatHub::direct_message(int64_t member_id, const std::string& key, const Values& v) {
    return direct_message(member_id, key, v, {});
}

bool ChatHub::direct_message(int64_t member_id, const std::string& key, const Values& v, const std::vector<Button>& buttons) {
    for (auto& p : providers_) {
        if (!p->ready() || !p->caps().direct_messages || !switch_on(*p, "dms")) continue;
        std::string account = p->member_account(member_id);
        if (account.empty()) continue;
        if (quiet()) { log(*p, "skip", key, "member", member_id, Result{true, "", ""}); return false; }
        // Callers give the start as "when_at" (local ISO) beside the plain
        // "when": show it the way this service shows times.
        Values pv = v;
        if (auto at = v.find("when_at"); at != v.end()) {
            auto plain = v.find("when");
            pv["when"] = when_text(*p, at->second, 'F', plain != v.end() ? plain->second : "");
            pv["when_relative"] = when_text(*p, at->second, 'R', "");
        }
        Message m = message(*p, key, pv);
        if (p->caps().buttons) m.buttons = buttons;
        Result r = p->direct_message(account, m);
        log(*p, "dm", key, "member", member_id, r);
        if (r.ok) return true;
    }
    return false;
}

} // namespace chat
