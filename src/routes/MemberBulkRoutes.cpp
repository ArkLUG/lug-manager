#include "routes/MemberBulkRoutes.hpp"
#include "utils/ParseId.hpp"
#include "utils/HtmlEscape.hpp"
#include <regex>
#include <sstream>
#include <unordered_set>

// Form fields: ids=1,2,3  action=paid|unpaid|chapter|fol  value=<date|chapter id|kfol/tfol/afol>
void register_member_bulk_routes(LugApp& app, MemberService& members, AuditService& audit) {
    CROW_ROUTE(app, "/members/bulk").methods("POST"_method)([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) { const char* v = params.get(k); return v ? std::string(v) : ""; };
        std::string action = gp("action");
        std::string value  = gp("value");

        std::vector<int64_t> ids;
        {
            std::unordered_set<int64_t> seen;
            std::istringstream ss(gp("ids"));
            std::string tok;
            while (std::getline(ss, tok, ',') && ids.size() < 1000)
                if (int64_t id = parse_id(tok); id > 0 && seen.insert(id).second) ids.push_back(id);
        }
        auto fail = [&](const std::string& msg) {
            res.code = 400;
            res.write("<span class=\"text-red-600 text-sm\">" + html_escape(msg) + "</span>");
            return std::move(res);
        };
        if (ids.empty()) return fail("Select at least one member.");

        static const std::regex ymd(R"(\d{4}-\d{2}-\d{2})");
        std::string summary;
        if (action == "paid") {
            if (!std::regex_match(value, ymd)) return fail("Choose a paid-until date.");
            summary = "Marked paid until " + value;
        } else if (action == "unpaid") {
            summary = "Marked unpaid";
        } else if (action == "chapter") {
            summary = value.empty() || value == "0" ? "Removed from chapter" : "Moved to chapter " + value;
        } else if (action == "fol") {
            if (value != "kfol" && value != "tfol" && value != "afol") return fail("Choose an age range.");
            summary = "Set age range to " + value;
        } else {
            return fail("Unknown action.");
        }

        int changed = 0;
        for (int64_t id : ids) {
            auto m = members.get(id);
            if (!m) continue;
            try {
                if (action == "paid")         members.set_paid(id, true, value);
                else if (action == "unpaid")  members.set_paid(id, false, "");
                else if (action == "chapter") members.set_chapter(id, parse_id(value));
                else if (action == "fol") {
                    Member u = *m;
                    u.fol_status = value;
                    members.update(id, u);
                }
                audit.log(req, app, "member.bulk", "member", id, m->display_name, summary + " (bulk)");
                ++changed;
            } catch (const std::exception&) {}
        }
        res.add_header("HX-Trigger", "membersUpdated"); // members table reloads on this
        res.write("<span class=\"text-green-700 text-sm\">" + html_escape(summary) + " for " +
                  std::to_string(changed) + " member(s).</span>");
        return res;
    });
}
