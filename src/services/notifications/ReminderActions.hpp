#pragma once
// What the buttons on reminder DMs do (Notifier::buttons): a member clicks one
// on their chat service, the service tells us (Discord: the signed
// /discord/interactions webhook), and this carries it out:
//   lm:snooze:<id>     Remind me later (send it again later; see Notifier::send_snoozed)
//   lm:mute:<id>       Don't remind me (turn that kind off, as on My Account)
//   lm:unmute:<id>     Undo that
//   lm:rsvp_off:<id>   Can't make it / Give up my spot (cancel the RSVP; the waitlist moves up)
//   lm:shift_off:<id>  Can't make my shift (leave the shift)
//   lm:rsvp:<event>    "I'm going" on an event announcement: RSVP, or cancel it (anyone with
//                      a member record linked to their Discord account; answered privately)
//   lm:mrsvp:<meeting> "I'm going" on a meeting announcement (click again to take it back)
//   lm:mno:<meeting>   "Can't make it" on a meeting announcement (same)
//   lm:mrsvp_off:<id>  Can't make it, on a meeting reminder DM
// <id> is the reminder_dms row the DM was recorded as; it only works for the
// member it was sent to.
#include "db/SqliteDatabase.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "integrations/ical/CalendarGenerator.hpp"
#include "repositories/events/MeetingRsvps.hpp"
#include "repositories/events/RsvpRepository.hpp"
#include "repositories/events/ShiftRepository.hpp"
#include "repositories/members/NotificationPrefs.hpp"
#include "services/AuditService.hpp"
#include "services/Features.hpp"
#include "services/notifications/Notifier.hpp"
#include "utils/LocalTime.hpp"
#include <ctime>
#include <optional>
#include <string>
#include <vector>

class ReminderActions {
public:
    ReminderActions(SqliteDatabase& db, RsvpRepository& rsvps, ShiftRepository& shifts, Notifier& notifier, AuditService& audit)
        : db_(db), rsvps_(rsvps), shifts_(shifts), notifier_(notifier), audit_(audit) {}

    struct Reply {
        std::string note;                    // added under the DM's text
        std::vector<chat::Button> buttons;   // the DM's new buttons
        bool private_only = false;           // answer just the clicker; leave the DM alone
    };

    // nullopt = not one of ours (another feature's button).
    std::optional<Reply> handle(const std::string& chat_user, const std::string& action, std::time_t now = std::time(nullptr)) {
        if (action.rfind("lm:", 0) != 0) return std::nullopt;
        const auto c2 = action.find(':', 3);
        if (c2 == std::string::npos) return Reply{"This button doesn't work any more.", {}, true};
        const std::string verb = action.substr(3, c2 - 3);
        int64_t id = 0;
        try { id = std::stoll(action.substr(c2 + 1)); } catch (...) {}
        if (verb == "rsvp") return rsvp_toggle(chat_user, id, now);
        if (verb == "mrsvp" || verb == "mno") return meeting_answer(chat_user, id, verb == "mrsvp", now);
        Row r;
        if (!load(id, r)) return Reply{"This button doesn't work any more.", {}, true};
        if (member_for(chat_user) != r.member) return Reply{"This button is for someone else.", {}, true};
        const auto about = Notifier::about_for(r.kind, r.ref);
        auto links = notifier_.buttons(r.kind, 0, about);   // link buttons only
        if (verb == "snooze") return snooze(r, links, now);
        if (verb == "mute" || verb == "unmute") {
            const bool on = verb == "unmute";
            NotificationPrefs(db_).set(r.member, r.kind, on);
            audit_.log_system(on ? "member.notifications_on" : "member.notifications_off", "member", r.member, "",
                              std::string(on ? "Turned on " : "Turned off ") + r.kind + " (from a Discord DM)");
            auto b = links;
            b.push_back(on ? chat::Button{"Don't remind me", "", "lm:mute:" + std::to_string(r.id), "secondary"}
                           : chat::Button{"Undo", "", "lm:unmute:" + std::to_string(r.id), "secondary"});
            return Reply{on ? "🔔 Back on: " + label(r.kind) + "."
                            : "🔕 Turned off: " + label(r.kind) + ". Turn it back on any time under My Account > Notifications.", b};
        }
        if (verb == "rsvp_off") return rsvp_off(r, links, now);
        if (verb == "shift_off") return shift_off(r, links, now);
        if (verb == "mrsvp_off") {
            int64_t meeting = 0;
            try { meeting = std::stoll(r.ref); } catch (...) {}
            std::string title;
            if (!meeting_open(meeting, now, title)) return Reply{"That meeting has started or is no longer on.", links};
            MeetingRsvps(db_).set(meeting, r.member, false);
            audit_.log_system("meeting.rsvp", "meeting", meeting, title, "Can't make it, from a Discord DM (member " + std::to_string(r.member) + ")");
            return Reply{"✅ Got it, you can't make " + title + ". Thanks for letting us know.", links};
        }
        return Reply{"This button doesn't work any more.", {}, true};
    }

private:
    struct Row { int64_t id = 0, member = 0; std::string kind, ref, key, values; };

    bool load(int64_t id, Row& r) {
        auto st = db_.prepare("SELECT id, member_id, kind, ref, template_key, values_json FROM reminder_dms WHERE id=?");
        st.bind(1, id);
        if (!st.step()) return false;
        r = {st.col_int(0), st.col_int(1), st.col_text(2), st.col_text(3), st.col_text(4), st.col_text(5)};
        return true;
    }
    int64_t member_for(const std::string& chat_user) {
        if (chat_user.empty()) return -1;
        auto st = db_.prepare("SELECT id FROM members WHERE discord_user_id=?");
        st.bind(1, chat_user);
        return st.step() ? st.col_int(0) : -1;
    }
    static std::string label(const std::string& kind) {
        for (const auto& k : NotificationPrefs::kinds()) if (kind == k.key) return k.label;
        return kind;
    }
    std::string value(const Row& r, const char* key) {
        auto j = crow::json::load(r.values);
        if (!j || j.t() != crow::json::type::Object || !j.has(key) || j[key].t() != crow::json::type::String) return "";
        return std::string(j[key].s());
    }
    std::time_t local_epoch(const std::string& iso) { return DiscordClient::local_to_epoch(iso, notifier_.timezone()); }

    Reply snooze(const Row& r, const std::vector<chat::Button>& links, std::time_t now) {
        if (!Notifier::snoozable(r.kind)) return Reply{"This one can't be snoozed.", {}, true};
        std::time_t until = 0;
        if (r.kind == "dues_reminder") until = now + 3 * 86400;
        else if (r.kind == "loan_reminder") until = now + 86400;
        else {
            // Before an event or shift: two hours before it starts, or in an hour
            const std::time_t start = local_epoch(value(r, "when_at"));
            if (start <= now) return Reply{"It has already started.", {}, true};
            until = start - 2 * 3600;
            if (until < now + 30 * 60) until = now + 3600;
            if (until > start - 10 * 60) return Reply{"It starts soon, so there's no later reminder.", {}, true};
        }
        auto up = db_.prepare("UPDATE reminder_dms SET snooze_until=?, resent=0 WHERE id=?");
        up.bind(1, static_cast<int64_t>(until)); up.bind(2, r.id);
        up.step();
        audit_.log_system("member.reminder_snoozed", "member", r.member, "", r.kind + " until " + std::to_string(until));
        return Reply{"⏰ I'll remind you again <t:" + std::to_string(until) + ":R>.", links};
    }

    Reply rsvp_off(const Row& r, const std::vector<chat::Button>& links, std::time_t now) {
        int64_t event = 0;
        try { event = std::stoll(r.ref); } catch (...) {}
        std::string title, start, end;
        int max = 0;
        {
            auto st = db_.prepare("SELECT title, start_time, end_time, max_attendees FROM lug_events WHERE id=?");
            st.bind(1, event);
            if (!st.step()) return Reply{"That event no longer exists.", {}, true};
            title = st.col_text(0); start = st.col_text(1); end = st.col_text(2); max = static_cast<int>(st.col_int(3));
        }
        if (local_epoch(start) <= now) return Reply{"It has already started; change your RSVP on the event page.", {}, true};
        if (!rsvps_.status_of(event, r.member)) return Reply{"You're not on the list for " + title + " any more.", links};
        auto promoted = rsvps_.cancel(event, r.member, max);
        audit_.log_system("event.rsvp_cancel", "event", event, title, "Cancelled RSVP from a Discord DM (member " + std::to_string(r.member) + ")");
        if (promoted) {
            audit_.log_system("event.rsvp_promoted", "event", event, title, "Member " + std::to_string(*promoted) + " moved off the waitlist");
            const std::string tz = notifier_.timezone();
            LugEvent ev; ev.id = event; ev.title = title; ev.start_time = start; ev.end_time = end;
            notifier_.notify(*promoted, "waitlist", "dm.waitlist",
                             {{"title", title}, {"when", DiscordClient::friendly_time(start, tz)}, {"when_at", start}}, true,
                             CalendarGenerator::ics_for(ev, tz), Notifier::about_for("waitlist", std::to_string(event)));
        }
        return Reply{"✅ Your RSVP for " + title + " is cancelled." + (promoted ? " Your spot went to the next person on the waitlist." : ""), links};
    }

    // "I'm going" under an event announcement: toggles the clicker's RSVP.
    Reply rsvp_toggle(const std::string& chat_user, int64_t event, std::time_t now) {
        const int64_t member = member_for(chat_user);
        if (member <= 0)
            return Reply{"Your Discord account isn't linked to a member yet. Sign in to LUG Manager with Discord once, then try again.", {}, true};
        std::string title, start, end, status, deadline;
        int max = 0;
        {
            auto st = db_.prepare("SELECT title, start_time, end_time, max_attendees, status, COALESCE(signup_deadline,'') FROM lug_events WHERE id=?");
            st.bind(1, event);
            if (!st.step()) return Reply{"That event no longer exists.", {}, true};
            title = st.col_text(0); start = st.col_text(1); end = st.col_text(2); max = static_cast<int>(st.col_int(3));
            status = st.col_text(4); deadline = st.col_text(5);
        }
        if (status == "cancelled") return Reply{title + " is cancelled.", {}, true};
        // Shows are dates: open until the end of their last day
        const std::string last = (end.size() >= 10 ? end : start).substr(0, 10);
        if (local_epoch(last + "T23:59:00") <= now) return Reply{title + " is over.", {}, true};
        if (rsvps_.status_of(event, member)) {
            auto promoted = rsvps_.cancel(event, member, max);
            audit_.log_system("event.rsvp_cancel", "event", event, title, "Cancelled RSVP from Discord (member " + std::to_string(member) + ")");
            if (promoted) {
                audit_.log_system("event.rsvp_promoted", "event", event, title, "Member " + std::to_string(*promoted) + " moved off the waitlist");
                const std::string tz = notifier_.timezone();
                LugEvent ev; ev.id = event; ev.title = title; ev.start_time = start; ev.end_time = end;
                notifier_.notify(*promoted, "waitlist", "dm.waitlist",
                                 {{"title", title}, {"when", DiscordClient::friendly_time(start, tz)}, {"when_at", start}}, true,
                                 CalendarGenerator::ics_for(ev, tz), Notifier::about_for("waitlist", std::to_string(event)));
            }
            return Reply{"You're no longer going to " + title + ". Click again if you change your mind.", {}, true};
        }
        // Same rule as the RSVP button on the site (rsvp_open): a deadline date runs to the end of that day
        if (!deadline.empty()) {
            const std::string dl = deadline.size() <= 10 ? deadline + "T23:59" : deadline.substr(0, 16);
            if (local_epoch(dl + ":00") < now) return Reply{"RSVPs for " + title + " are closed.", {}, true};
        }
        const std::string s = rsvps_.rsvp(event, member, max);
        audit_.log_system("event.rsvp", "event", event, title, "RSVP from Discord (member " + std::to_string(member) + "): " + s);
        if (s == "waitlist")
            return Reply{"It's full, so you're on the waitlist for " + title + ". You'll get a message if a spot opens. Click again to leave the list.", {}, true};
        return Reply{"✅ You're going to " + title + ". Click again to cancel.", {}, true};
    }

    // A meeting that's on and hasn't ended yet; its title in `title`.
    bool meeting_open(int64_t meeting, std::time_t now, std::string& title) {
        auto st = db_.prepare("SELECT title, start_time, COALESCE(end_time,''), COALESCE(status,'') FROM meetings WHERE id=?");
        st.bind(1, meeting);
        if (!st.step()) return false;
        title = st.col_text(0);
        const std::string end = st.col_text(2).empty() ? st.col_text(1) : st.col_text(2);
        return st.col_text(3) != "cancelled" && local_epoch(end.substr(0, 16) + ":00") > now;
    }

    // "I'm going" / "Can't make it" on a meeting announcement; clicking the
    // same answer again takes it back.
    Reply meeting_answer(const std::string& chat_user, int64_t meeting, bool going, std::time_t now) {
        if (!Features::on("rsvps")) return Reply{"RSVPs are switched off.", {}, true};
        const int64_t member = member_for(chat_user);
        if (member <= 0) return Reply{"Your Discord account isn't linked to a member yet. Sign in to LUG Manager with Discord once, then try again.", {}, true};
        std::string title;
        if (!meeting_open(meeting, now, title)) return Reply{"That meeting is over or no longer on.", {}, true};
        MeetingRsvps rs(db_);
        const std::string was = rs.status_of(meeting, member);
        if (was == (going ? "going" : "not_going")) {
            rs.clear(meeting, member);
            audit_.log_system("meeting.rsvp", "meeting", meeting, title, "Took back their answer, from Discord (member " + std::to_string(member) + ")");
            return Reply{"Okay, no answer for " + title + " any more.", {}, true};
        }
        rs.set(meeting, member, going);
        audit_.log_system("meeting.rsvp", "meeting", meeting, title,
                          std::string(going ? "Going" : "Can't make it") + ", from Discord (member " + std::to_string(member) + ")");
        return Reply{going ? "✅ You're going to " + title + ". Click again to take it back."
                           : "Got it, you can't make " + title + ". Click again to take it back.", {}, true};
    }

    Reply shift_off(const Row& r, const std::vector<chat::Button>& links, std::time_t now) {
        int64_t signup = 0;
        try { signup = std::stoll(r.ref.substr(0, r.ref.find(':'))); } catch (...) {}
        int64_t shift = 0;
        std::string title, starts;
        {
            auto st = db_.prepare("SELECT u.shift_id, s.title, s.starts_at FROM event_shift_signups u JOIN event_shifts s ON s.id = u.shift_id "
                                  "WHERE u.id=? AND u.member_id=?");
            st.bind(1, signup); st.bind(2, r.member);
            if (!st.step()) return Reply{"You're not signed up for that shift any more.", links};
            shift = st.col_int(0); title = st.col_text(1); starts = st.col_text(2);
        }
        if (local_epoch(starts) <= now) return Reply{"That shift has already started.", {}, true};
        shifts_.withdraw(shift, r.member);
        audit_.log_system("event.shift_withdraw", "shift", shift, title, "Left the shift from a Discord DM (member " + std::to_string(r.member) + ")");
        return Reply{"✅ You're off the " + title + " shift. Thanks for letting us know.", links};
    }

    SqliteDatabase& db_;
    RsvpRepository& rsvps_;
    ShiftRepository& shifts_;
    Notifier& notifier_;
    AuditService& audit_;
};
