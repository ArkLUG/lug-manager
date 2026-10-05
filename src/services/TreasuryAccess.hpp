#pragma once
#include "repositories/admin/SettingsRepository.hpp"
#include <string>

// Who can see which part of the Treasury (Settings > Treasury). Editing is
// always treasurers and admins; these settings only widen who can look.
// Each part's audience is one of:
//   "treasurers" - admins and treasurers only (the default, as before)
//   "leads"      - also chapter leads and moderators
//   "members"    - every signed-in member
namespace treasury {

enum class Part { Summary, Ledger, Receipts, DuesNames };

inline const char* setting_key(Part p) {
    switch (p) {
        case Part::Summary:   return "treasury_view_summary";
        case Part::Ledger:    return "treasury_view_ledger";
        case Part::Receipts:  return "treasury_view_receipts";
        case Part::DuesNames: return "treasury_view_dues_names";
    }
    return "";
}

inline bool valid_audience(const std::string& a) { return a == "treasurers" || a == "leads" || a == "members"; }

inline std::string audience(SettingsRepository* settings, Part p) {
    std::string a = settings ? settings->get(setting_key(p), "treasurers") : "treasurers";
    return valid_audience(a) ? a : "treasurers";
}

// `can_treasury` = admin or treasurer; `lead` = chapter lead / moderator (or admin).
inline bool can_see(SettingsRepository* settings, Part p, bool can_treasury, bool lead, bool signed_in) {
    if (can_treasury) return true;
    if (!signed_in) return false;
    const std::string a = audience(settings, p);
    if (a == "members") return true;
    return a == "leads" && lead;
}

// Parts a viewer sees; the ledger, receipts and member names never show to
// someone who can't see the page itself.
struct View {
    bool edit = false, summary = false, ledger = false, receipts = false, dues_names = false;
    bool any() const { return summary || ledger; }
};

inline View view_for(SettingsRepository* settings, bool can_treasury, bool lead, bool signed_in) {
    View v;
    v.edit = can_treasury;
    v.summary = can_see(settings, Part::Summary, can_treasury, lead, signed_in);
    v.ledger = can_see(settings, Part::Ledger, can_treasury, lead, signed_in);
    v.receipts = v.ledger && can_see(settings, Part::Receipts, can_treasury, lead, signed_in);
    v.dues_names = v.ledger && can_see(settings, Part::DuesNames, can_treasury, lead, signed_in);
    return v;
}

} // namespace treasury
