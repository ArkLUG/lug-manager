#pragma once
#include "models/Member.hpp"
#include "models/LugEvent.hpp"
#include "models/Meeting.hpp"
#include "models/Chapter.hpp"
#include "models/ChapterMember.hpp"
#include "models/RoleMapping.hpp"
#include "models/Attendance.hpp"
#include "models/PerkLevel.hpp"
#include "models/AuditLog.hpp"
#include "models/EventDay.hpp"
#include "models/ApiKey.hpp"
#include <crow.h>
#include <vector>

// One to_json() free function per model, mirroring the field lists in src/models/*.hpp
// exactly. Kept mechanical/one-line-per-field so it stays trivial to keep in sync as
// models gain fields. No from_json(): POST/PUT handlers apply fields directly from the
// parsed request body (see each <Entity>ApiRoutes.cpp), matching the existing partial-
// merge idiom already used by EventRoutes.cpp's JSON PUT branch.

inline crow::json::wvalue to_json(const Member& m) {
    crow::json::wvalue j;
    j["id"]               = m.id;
    j["discord_user_id"]  = m.discord_user_id;
    j["discord_username"] = m.discord_username;
    j["first_name"]       = m.first_name;
    j["last_name"]        = m.last_name;
    j["display_name"]     = m.display_name;
    j["email"]            = m.email;
    j["is_paid"]          = m.is_paid;
    j["paid_until"]       = m.paid_until;
    j["role"]             = m.role;
    j["phone"]            = m.phone;
    j["address_line1"]    = m.address_line1;
    j["address_line2"]    = m.address_line2;
    j["city"]             = m.city;
    j["state"]            = m.state;
    j["zip"]               = m.zip;
    j["sharing_email"]     = m.sharing_email;
    j["sharing_phone"]     = m.sharing_phone;
    j["sharing_address"]   = m.sharing_address;
    j["sharing_birthday"]  = m.sharing_birthday;
    j["sharing_discord"]   = m.sharing_discord;
    j["birthday"]          = m.birthday;
    j["fol_status"]        = m.fol_status;
    j["chapter_id"]        = m.chapter_id;
    j["chapter_name"]      = m.chapter_name;
    j["created_at"]        = m.created_at;
    j["updated_at"]        = m.updated_at;
    return j;
}

inline crow::json::wvalue to_json(const LugEvent& e) {
    crow::json::wvalue j;
    j["id"]                         = e.id;
    j["title"]                      = e.title;
    j["description"]                = e.description;
    j["location"]                   = e.location;
    j["start_time"]                 = e.start_time;
    j["end_time"]                   = e.end_time;
    j["status"]                     = e.status;
    j["discord_thread_id"]          = e.discord_thread_id;
    j["discord_event_id"]           = e.discord_event_id;
    j["google_calendar_event_id"]   = e.google_calendar_event_id;
    j["discord_chapter_message_id"] = e.discord_chapter_message_id;
    j["discord_lug_message_id"]     = e.discord_lug_message_id;
    j["discord_ping_role_ids"]      = (e.discord_ping_role_ids == "\x01") ? "" : e.discord_ping_role_ids;
    j["ical_uid"]                   = e.ical_uid;
    j["signup_deadline"]            = e.signup_deadline;
    j["max_attendees"]              = e.max_attendees;
    j["scope"]                      = e.scope;
    j["chapter_id"]                 = e.chapter_id;
    j["event_lead_id"]              = e.event_lead_id;
    j["event_lead_name"]            = e.event_lead_name;
    j["event_lead_discord_id"]      = e.event_lead_discord_id;
    j["suppress_discord"]           = e.suppress_discord;
    j["suppress_calendar"]          = e.suppress_calendar;
    j["notes"]                      = e.notes;
    j["notes_discord_post_id"]      = e.notes_discord_post_id;
    j["checkin_token"]              = e.checkin_token;
    j["entrance_fee"]               = e.entrance_fee;
    j["public_kids"]                = e.public_kids;
    j["public_teens"]               = e.public_teens;
    j["public_adults"]              = e.public_adults;
    j["social_media_links"]         = e.social_media_links;
    j["event_feedback"]             = e.event_feedback;
    j["created_at"]                 = e.created_at;
    j["updated_at"]                 = e.updated_at;
    return j;
}

inline crow::json::wvalue to_json(const Meeting& m) {
    crow::json::wvalue j;
    j["id"]                         = m.id;
    j["title"]                      = m.title;
    j["description"]                = m.description;
    j["location"]                   = m.location;
    j["start_time"]                 = m.start_time;
    j["end_time"]                   = m.end_time;
    j["status"]                     = m.status;
    j["discord_event_id"]           = m.discord_event_id;
    j["discord_lug_message_id"]     = m.discord_lug_message_id;
    j["discord_chapter_message_id"] = m.discord_chapter_message_id;
    j["google_calendar_event_id"]   = m.google_calendar_event_id;
    j["ical_uid"]                   = m.ical_uid;
    j["scope"]                      = m.scope;
    j["chapter_id"]                 = m.chapter_id;
    j["is_virtual"]                 = m.is_virtual;
    j["discord_voice_channel_id"]   = m.discord_voice_channel_id;
    j["suppress_discord"]           = m.suppress_discord;
    j["suppress_calendar"]          = m.suppress_calendar;
    j["notes"]                      = m.notes;
    j["notes_discord_post_id"]      = m.notes_discord_post_id;
    j["checkin_token"]              = m.checkin_token;
    j["created_at"]                 = m.created_at;
    j["updated_at"]                 = m.updated_at;
    return j;
}

inline crow::json::wvalue to_json(const Chapter& c) {
    crow::json::wvalue j;
    j["id"]                               = c.id;
    j["name"]                             = c.name;
    j["shorthand"]                        = c.shorthand;
    j["description"]                      = c.description;
    j["discord_announcement_channel_id"]  = c.discord_announcement_channel_id;
    j["discord_lead_role_id"]             = c.discord_lead_role_id;
    j["discord_member_role_id"]           = c.discord_member_role_id;
    j["created_by"]                       = c.created_by;
    j["created_at"]                       = c.created_at;
    return j;
}

inline crow::json::wvalue to_json(const ChapterMember& cm) {
    crow::json::wvalue j;
    j["member_id"]        = cm.member_id;
    j["chapter_id"]       = cm.chapter_id;
    j["chapter_role"]     = cm.chapter_role;
    j["granted_by"]       = cm.granted_by;
    j["granted_at"]       = cm.granted_at;
    j["display_name"]     = cm.display_name;
    j["discord_username"] = cm.discord_username;
    j["chapter_name"]     = cm.chapter_name;
    return j;
}

inline crow::json::wvalue to_json(const RoleMapping& rm) {
    crow::json::wvalue j;
    j["discord_role_id"]   = rm.discord_role_id;
    j["discord_role_name"] = rm.discord_role_name;
    j["lug_role"]          = rm.lug_role;
    return j;
}

inline crow::json::wvalue to_json(const Attendance& a) {
    crow::json::wvalue j;
    j["id"]                     = a.id;
    j["member_id"]              = a.member_id;
    j["entity_type"]            = a.entity_type;
    j["entity_id"]               = a.entity_id;
    j["checked_in_at"]           = a.checked_in_at;
    j["notes"]                   = a.notes;
    j["is_virtual"]              = a.is_virtual;
    j["member_display_name"]     = a.member_display_name;
    j["member_discord_username"] = a.member_discord_username;
    return j;
}

inline crow::json::wvalue to_json(const PerkLevel& p) {
    crow::json::wvalue j;
    j["id"]                          = p.id;
    j["name"]                        = p.name;
    j["description"]                 = p.description;
    j["discord_role_id"]             = p.discord_role_id;
    j["meeting_attendance_required"] = p.meeting_attendance_required;
    j["event_attendance_required"]   = p.event_attendance_required;
    j["requires_paid_dues"]          = p.requires_paid_dues;
    j["min_fol_status"]              = p.min_fol_status;
    j["sort_order"]                  = p.sort_order;
    j["year"]                        = p.year;
    j["created_at"]                  = p.created_at;
    j["updated_at"]                  = p.updated_at;
    return j;
}

inline crow::json::wvalue to_json(const AuditLog& a) {
    crow::json::wvalue j;
    j["id"]          = a.id;
    j["actor_id"]    = a.actor_id;
    j["actor_name"]  = a.actor_name;
    j["action"]      = a.action;
    j["entity_type"] = a.entity_type;
    j["entity_id"]   = a.entity_id;
    j["entity_name"] = a.entity_name;
    j["details"]     = a.details;
    j["ip_address"]  = a.ip_address;
    j["created_at"]  = a.created_at;
    return j;
}

inline crow::json::wvalue to_json(const EventDay& d) {
    crow::json::wvalue j;
    j["id"]         = d.id;
    j["event_id"]   = d.event_id;
    j["day_date"]   = d.day_date;
    j["day_number"] = d.day_number;
    return j;
}

inline crow::json::wvalue to_json(const EventDayAttendance& a) {
    crow::json::wvalue j;
    j["id"]                       = a.id;
    j["event_day_id"]             = a.event_day_id;
    j["member_id"]                = a.member_id;
    j["checked_in_at"]            = a.checked_in_at;
    j["notes"]                    = a.notes;
    j["member_display_name"]      = a.member_display_name;
    j["member_discord_username"]  = a.member_discord_username;
    j["day_date"]                 = a.day_date;
    j["day_number"]               = a.day_number;
    return j;
}

// ApiKey is deliberately serialized WITHOUT key_hash (never expose the hash, and the
// raw plaintext key is never stored at all) — only metadata is returned via the API/UI.
inline crow::json::wvalue to_json(const ApiKey& k) {
    crow::json::wvalue j;
    j["id"]               = k.id;
    j["label"]             = k.label;
    j["scope"]             = k.scope;
    j["created_by"]        = k.created_by;
    j["created_by_name"]   = k.created_by_name;
    j["created_at"]        = k.created_at;
    j["last_used_at"]      = k.last_used_at;
    j["revoked_at"]        = k.revoked_at;
    return j;
}

template<typename T>
inline crow::json::wvalue to_json_list(const std::vector<T>& items) {
    crow::json::wvalue arr;
    arr = crow::json::wvalue::list();
    for (size_t i = 0; i < items.size(); ++i) arr[i] = to_json(items[i]);
    return arr;
}
