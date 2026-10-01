#pragma once
#include "models/PerkLevel.hpp"
#include "repositories/members/PerkLevelRepository.hpp" // fol_rank
#include <algorithm>
#include <string>
#include <vector>

// Where a member stands against a year's perk tiers (levels in sort order,
// lowest first). meetings = in-person, perk-eligible meetings only.
struct PerkProgress {
    std::string achieved;        // highest tier met ("" if none)
    std::string achieved_desc;
    std::string next;            // first tier not met ("" if all met)
    std::string next_desc;
    int  meetings_needed = 0;
    int  events_needed   = 0;
    bool needs_dues      = false;
    std::string needs_fol;       // e.g. "tfol" if the next tier needs an older FOL status
    // Attendance still missing for the next tier (dues/FOL excluded).
    int gap() const { return meetings_needed + events_needed; }
};

inline bool meets_perk_level(const PerkLevel& lvl, int meetings, int events, bool is_paid,
                             const std::string& fol) {
    return meetings >= lvl.meeting_attendance_required &&
           events >= lvl.event_attendance_required &&
           (!lvl.requires_paid_dues || is_paid) &&
           fol_rank(fol.empty() ? "afol" : fol) >= fol_rank(lvl.min_fol_status);
}

inline PerkProgress compute_perk_progress(const std::vector<PerkLevel>& levels, int meetings,
                                          int events, bool is_paid, const std::string& fol) {
    PerkProgress p;
    for (const auto& lvl : levels) {
        if (meets_perk_level(lvl, meetings, events, is_paid, fol)) {
            p.achieved = lvl.name;
            p.achieved_desc = lvl.description;
        } else if (p.next.empty()) {
            p.next = lvl.name;
            p.next_desc = lvl.description;
            p.meetings_needed = std::max(0, lvl.meeting_attendance_required - meetings);
            p.events_needed   = std::max(0, lvl.event_attendance_required - events);
            p.needs_dues      = lvl.requires_paid_dues && !is_paid;
            if (fol_rank(fol.empty() ? "afol" : fol) < fol_rank(lvl.min_fol_status))
                p.needs_fol = lvl.min_fol_status;
        }
    }
    return p;
}
