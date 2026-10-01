#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "models/LugEvent.hpp"
#include "repositories/ChapterMemberRepository.hpp"

// Who may manage an event's RSVPs, display requests, etc.: an admin, the
// event lead, or a lead/event manager of the event's chapter (same rule as
// the event page's can_manage flag).
template <typename App>
bool can_manage_event(const crow::request& req, App& app, const LugEvent& ev,
                      ChapterMemberRepository& chapter_members) {
    auto& a = app.template get_context<AuthMiddleware>(req).auth;
    if (a.is_admin()) return true;
    if (ev.event_lead_id > 0 && ev.event_lead_id == a.member_id) return true;
    if (ev.chapter_id > 0) {
        auto r = chapter_members.get_chapter_role(a.member_id, ev.chapter_id);
        return r && chapter_role_rank(*r) >= chapter_role_rank("event_manager");
    }
    return false;
}
