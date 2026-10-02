#pragma once
#include "db/SqliteDatabase.hpp"
#include "utils/LocalTime.hpp"
#include "utils/text/HtmlEscape.hpp"
#include "utils/text/UrlEncode.hpp"
#include <crow/mustache.h>
#include <cstdio>
#include <ctime>
#include <string>

// Calendar connections that need no credentials: "add this one item" links
// for Google Calendar, Outlook.com and Microsoft 365 (plus our own .ics
// download), and the subscribe block for a whole feed.
namespace cal_links {

struct Item {
    std::string title, description, location;
    std::string start, end;   // stored local "YYYY-MM-DDTHH:MM[:SS]"
    bool all_day = false;     // events are all-day in the feeds too
    std::string ics_path;     // e.g. "/events/12/calendar.ics"
};

namespace detail {
inline std::string ymd(const std::string& iso) { return iso.size() >= 10 ? iso.substr(0, 10) : ""; }
inline std::string next_day(const std::string& date) {   // "YYYY-MM-DD" + 1 day
    std::tm t{};
    if (std::sscanf(date.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) return date;
    t.tm_year -= 1900; t.tm_mon -= 1; t.tm_mday += 1; t.tm_hour = 12;
    timegm(&t);
    char b[16];
    std::snprintf(b, sizeof(b), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return b;
}
inline std::string utc(const std::string& iso, bool compact) {   // local -> UTC
    std::time_t e = local_iso_to_epoch(iso);
    if (e < 0) return "";
    std::tm g{};
    gmtime_r(&e, &g);
    char b[32];
    std::strftime(b, sizeof(b), compact ? "%Y%m%dT%H%M%SZ" : "%Y-%m-%dT%H:%M:%SZ", &g);
    return b;
}
inline std::string strip_dashes(std::string s) { std::string o; for (char c : s) if (c != '-') o += c; return o; }
inline std::string clip(const std::string& s, size_t n) { return s.size() > n ? s.substr(0, n) + "..." : s; }
}

// Google Calendar's "create event" page, filled in.
inline std::string google(const Item& it) {
    using namespace detail;
    std::string dates;
    const std::string end = it.end.empty() ? it.start : it.end;
    if (it.all_day) dates = strip_dashes(ymd(it.start)) + "/" + strip_dashes(next_day(ymd(end)));
    else dates = utc(it.start, true) + "/" + utc(end, true);
    return "https://calendar.google.com/calendar/render?action=TEMPLATE&text=" + url_encode_component(it.title) +
           "&dates=" + url_encode_component(dates) +
           "&details=" + url_encode_component(clip(it.description, 1000)) +
           "&location=" + url_encode_component(it.location);
}

// Outlook.com (personal) and Microsoft 365 (work/school) "new event" pages.
inline std::string outlook(const Item& it, bool office) {
    using namespace detail;
    const std::string end = it.end.empty() ? it.start : it.end;
    std::string s, e;
    if (it.all_day) { s = ymd(it.start); e = next_day(ymd(end)); }
    else { s = utc(it.start, false); e = utc(end, false); }
    return std::string(office ? "https://outlook.office.com" : "https://outlook.live.com") +
           "/calendar/0/action/compose?rru=addevent&subject=" + url_encode_component(it.title) +
           "&startdt=" + url_encode_component(s) + "&enddt=" + url_encode_component(e) +
           (it.all_day ? "&allday=true" : "") +
           "&location=" + url_encode_component(it.location) +
           "&body=" + url_encode_component(clip(it.description, 1000));
}

// Fills cal_google / cal_outlook / cal_m365 / cal_ics for the "Add to
// calendar" menu in a detail template.
inline void add_to(crow::mustache::context& ctx, const Item& it) {
    ctx["cal_google"] = google(it);
    ctx["cal_outlook"] = outlook(it, false);
    ctx["cal_m365"] = outlook(it, true);
    ctx["cal_ics"] = it.ics_path;
}

// The LUG's shared Google calendar ID when Settings > Google Calendar says it
// is public, else "". Google shows its own calendars' changes right away,
// while it re-reads a subscribed feed only about once a day.
inline std::string public_google_calendar(SqliteDatabase& db) {
    std::string id, pub;
    auto st = db.prepare("SELECT key, value FROM lug_settings WHERE key IN ('google_calendar_id','google_calendar_public')");
    while (st.step()) (st.col_text(0) == "google_calendar_id" ? id : pub) = st.col_text(1);
    return pub == "1" ? id : "";
}

// The subscribe block for a feed at `path` (site-relative). static/app.js
// turns the path into full links for the page's own address: webcal:// for
// Apple and phones, Google "add by URL", Outlook.com and Microsoft 365 "add
// from web", a copyable link and a QR code. With `google_calendar` (the whole
// schedule only, see public_google_calendar), the Google button adds that
// shared calendar instead of the feed.
inline std::string subscribe_html(const std::string& path, const std::string& name, const std::string& id,
                                  const std::string& google_calendar = "") {
    const std::string p = html_escape(path), n = html_escape(name), i = html_escape(id);
    const std::string google = google_calendar.empty()
        ? "<a data-cal-link=\"google\" href=\"" + p + "\""
        : "<a data-cal-fixed href=\"" + html_escape("https://calendar.google.com/calendar/render?cid=" + url_encode_component(google_calendar)) + "\"";
    const std::string btn = "inline-flex items-center gap-1 px-3 py-1.5 text-xs font-medium border border-gray-300 "
                            "text-gray-700 rounded-lg hover:bg-gray-50";
    return "<div class=\"cal-subscribe space-y-2\" data-cal-feed=\"" + p + "\" data-cal-name=\"" + n + "\">"
           "<div class=\"flex flex-wrap gap-2\">"
           "<a data-cal-link=\"webcal\" href=\"" + p + "\" class=\"" + btn + "\">Apple / iPhone</a>"
           + google + " target=\"_blank\" rel=\"noopener\" class=\"" + btn + "\">Google Calendar</a>"
           "<a data-cal-link=\"outlook\" href=\"" + p + "\" target=\"_blank\" rel=\"noopener\" class=\"" + btn + "\">Outlook.com</a>"
           "<a data-cal-link=\"m365\" href=\"" + p + "\" target=\"_blank\" rel=\"noopener\" class=\"" + btn + "\">Microsoft 365</a>"
           "<button type=\"button\" data-action=\"copy\" data-copy-target=\"#" + i + "-url\" class=\"" + btn + "\">Copy link</button>"
           "</div>"
           "<input id=\"" + i + "-url\" data-cal-url readonly data-action=\"select-self\" value=\"" + p + "\" aria-label=\"Calendar link\" "
           "class=\"w-full bg-gray-50 rounded-lg px-3 py-1.5 font-mono text-xs text-gray-700 border border-gray-200\">"
           "<details class=\"text-xs text-gray-500\"><summary class=\"cursor-pointer\">QR code for a phone</summary>"
           "<div data-cal-qr class=\"mt-2 inline-block bg-white p-2 rounded\"></div></details>"
           "<p class=\"text-xs text-gray-400\">A subscription stays up to date by itself." +
           std::string(google_calendar.empty() ? " Google Calendar can take up to a day to pick up changes; Apple and Outlook check more often."
                                               : " The Google Calendar button adds our shared Google calendar, which updates right away.") +
           "</p>"
           "</div>";
}

} // namespace cal_links
