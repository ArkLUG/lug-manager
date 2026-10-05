#pragma once
#include "utils/text/Money.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

// Dues run on a club year that ends on the last day of a chosen month
// (Settings > Dues; December = the calendar year). Someone paying part-way
// through is suggested the full amount x months left / 12 (counting the
// month they pay in), rounded up to whole dollars. The person recording a
// payment can always change the suggestion.
namespace dues {

struct Config {
    int64_t amount_cents = 0;   // full year; 0 = no standard amount
    int year_end_month = 12;    // 1-12: the year ends on that month's last day
    bool prorate = true;
    int grace_days = 0;         // stay paid this many days after paid-until
};

struct Suggestion {
    int64_t cents = 0;          // 0 = no standard amount set
    std::string covers_until;   // YYYY-MM-DD
    int months = 12;            // months covered (12 = a full year)
    bool renewal = false;       // already paid for the current year: the next one
    std::string explain;
};

inline int days_in(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : d[m - 1];
}

inline std::string ymd(int y, int m, int d) {
    char b[16];
    std::snprintf(b, sizeof(b), "%04d-%02d-%02d", y, m, d);
    return b;
}

// The end of the club year that `date` (YYYY-MM-DD) falls in.
inline std::string year_end_for(const std::string& date, int end_month, int* end_y = nullptr) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return "";
    if (end_month < 1 || end_month > 12) end_month = 12;
    int ey = m <= end_month ? y : y + 1;
    if (end_y) *end_y = ey;
    return ymd(ey, end_month, days_in(ey, end_month));
}

// The date `days` after a YYYY-MM-DD date ("" if unparseable).
inline std::string add_days(const std::string& date, int days) {
    std::tm t{};
    if (std::sscanf(date.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) return "";
    t.tm_year -= 1900; t.tm_mon -= 1; t.tm_mday += days; t.tm_hour = 12;
    timegm(&t);
    return ymd(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

// Grace period (Settings > Dues): members stay paid for `grace_days` after
// their paid-until date before the daily check marks them unpaid. Returns the
// last day of grace when `today` is inside it, else "".
inline std::string grace_until(const std::string& paid_until, int grace_days, const std::string& today) {
    if (grace_days <= 0 || paid_until.size() != 10 || paid_until >= today) return "";
    std::string last = add_days(paid_until, grace_days);
    return today <= last ? last : "";
}

inline Suggestion suggest(const Config& c, const std::string& paid_on, const std::string& paid_until) {
    Suggestion s;
    int y = 0, m = 0, d = 0, ey = 0;
    if (std::sscanf(paid_on.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) return s;
    const int em = c.year_end_month >= 1 && c.year_end_month <= 12 ? c.year_end_month : 12;
    std::string end = year_end_for(paid_on, em, &ey);
    if (!paid_until.empty() && paid_until >= end) {
        // Already paid for this year: a renewal for the next full year
        s.renewal = true;
        s.covers_until = ymd(ey + 1, em, days_in(ey + 1, em));
        s.months = 12;
        s.cents = c.amount_cents;
        s.explain = "Already paid to " + paid_until + ", so this renews for the next year";
        return s;
    }
    s.covers_until = end;
    s.months = (ey - y) * 12 + (em - m) + 1;
    if (!c.prorate || s.months >= 12) {
        s.months = 12;
        s.cents = c.amount_cents;
        s.explain = c.amount_cents > 0 ? "Full year" : "";
        return s;
    }
    s.cents = c.amount_cents > 0
        ? static_cast<int64_t>(std::ceil(static_cast<double>(c.amount_cents) * s.months / 12.0 / 100.0)) * 100
        : 0;
    if (c.amount_cents > 0)
        s.explain = "Prorated: " + std::to_string(s.months) + " of 12 months of " + money(c.amount_cents) + ", rounded up";
    return s;
}

} // namespace dues
