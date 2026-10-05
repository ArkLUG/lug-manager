#pragma once
#include <string>
#include <vector>

// Every settings page, grouped. The Settings overview lists these and the
// breadcrumb above a settings page is built from them.
struct SettingsLink  { const char* href; const char* label; const char* help; const char* feature; };
struct SettingsGroup { const char* title; const char* feature; std::vector<SettingsLink> links; };
const std::vector<SettingsGroup>& settings_groups();

// One step of a breadcrumb; an empty href is plain text.
struct Crumb { std::string label; std::string href; };

// The trail for a page, e.g. Settings > Members and money > Dues, or
// Schedule > <event title>. Empty when the page sits at the top of its
// section (the sidebar already says where you are). `deeper_title` names the
// last step on a page below a known one; pass "" to stop at the known page.
std::vector<Crumb> breadcrumbs_for(const std::string& path, const std::string& deeper_title, bool is_admin);

// The <nav> for a trail ("" when there's nothing to show).
std::string breadcrumb_html(const std::vector<Crumb>& trail);
