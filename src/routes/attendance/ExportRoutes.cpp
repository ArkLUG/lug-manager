#include "routes/attendance/ExportRoutes.hpp"
#include "services/members/PerkProgress.hpp"
#include "utils/text/Csv.hpp"
#include "utils/LocalTime.hpp"
#include <sstream>

namespace {

crow::response csv_response(const std::string& filename, const std::string& body) {
    crow::response res;
    res.add_header("Content-Type", "text/csv; charset=utf-8");
    res.add_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    res.add_header("Cache-Control", "no-store"); // contains personal data
    res.write("\xEF\xBB\xBF" + body);            // BOM so Excel reads UTF-8
    return res;
}


} // namespace

void register_export_routes(LugApp& app, MemberRepository& members, AttendanceRepository& attendance,
                            PerkLevelRepository& perks, AuditService& audit) {

    // GET /members.csv - chapter lead+ (same PII visibility as the members page for that role)
    CROW_ROUTE(app, "/members.csv")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;
        std::ostringstream csv;
        csv << "ID,Display name,First name,Last name,Discord username,Email,Phone,Address line 1,"
               "Address line 2,City,State,ZIP,Birthday,Age range,Role,Chapter,Dues paid,Paid until,Joined\r\n";
        for (const auto& m : members.find_all()) {
            csv << m.id << ',' << csv_field(m.display_name) << ',' << csv_field(m.first_name) << ','
                << csv_field(m.last_name) << ',' << csv_field(m.discord_username) << ','
                << csv_field(m.email) << ',' << csv_field(m.phone) << ',' << csv_field(m.address_line1) << ','
                << csv_field(m.address_line2) << ',' << csv_field(m.city) << ',' << csv_field(m.state) << ','
                << csv_field(m.zip) << ',' << csv_field(m.birthday) << ',' << csv_field(m.fol_status) << ','
                << csv_field(m.role) << ',' << csv_field(m.chapter_name) << ','
                << (m.is_paid ? "yes" : "no") << ',' << csv_field(m.paid_until) << ','
                << csv_field(m.created_at.substr(0, 10)) << "\r\n";
        }
        audit.log(req, app, "export.members", "member", 0, "", "Exported members CSV");
        return csv_response("members.csv", csv.str());
    });

    // GET /attendance/overview.csv?year=YYYY - admin
    CROW_ROUTE(app, "/attendance/overview.csv")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        int year = local_year();
        if (const char* y = req.url_params.get("year")) { try { year = std::stoi(y); } catch (...) {} }
        AttendanceRepository::OverviewParams p;
        p.year = year;
        p.limit = 1000000;
        auto levels = perks.find_by_year(year);
        std::ostringstream csv;
        csv << "Member,First name,Last name,Discord username,Meetings (in person),Meetings (virtual),"
               "Events,Total,Last attendance,Dues paid,Perk tier (" << year << ")\r\n";
        for (const auto& s : attendance.get_overview_paginated(p)) {
            int in_person = s.meeting_count - s.meeting_virtual_count;
            auto pp = compute_perk_progress(levels, in_person, s.event_count, s.is_paid, s.fol_status);
            csv << csv_field(s.display_name) << ',' << csv_field(s.first_name) << ','
                << csv_field(s.last_name) << ',' << csv_field(s.discord_username) << ','
                << in_person << ',' << s.meeting_virtual_count << ',' << s.event_count << ','
                << (s.meeting_count + s.event_count) << ',' << csv_field(s.last_attendance.substr(0, 10)) << ','
                << (s.is_paid ? "yes" : "no") << ',' << csv_field(pp.achieved) << "\r\n";
        }
        audit.log(req, app, "export.attendance", "attendance", 0, "", "Exported attendance CSV for " + std::to_string(year));
        return csv_response("attendance-" + std::to_string(year) + ".csv", csv.str());
    });

    // GET /audit.csv?search=&action_filter= - admin, newest first, capped at 50k rows
    CROW_ROUTE(app, "/audit.csv")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        const char* s  = req.url_params.get("search");
        const char* af = req.url_params.get("action_filter");
        std::ostringstream csv;
        csv << "When (UTC),Actor,Action,Entity type,Entity ID,Entity,Details,IP\r\n";
        for (const auto& e : audit.repo().find_paginated(s ? s : "", af ? af : "", 50000, 0)) {
            csv << csv_field(e.created_at) << ',' << csv_field(e.actor_name) << ',' << csv_field(e.action) << ','
                << csv_field(e.entity_type) << ',' << e.entity_id << ',' << csv_field(e.entity_name) << ','
                << csv_field(e.details) << ',' << csv_field(e.ip_address) << "\r\n";
        }
        audit.log(req, app, "export.audit", "audit", 0, "", "Exported audit log CSV");
        return csv_response("audit-log.csv", csv.str());
    });
}
