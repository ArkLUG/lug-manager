#include "routes/attendance/ReportRoutes.hpp"
#include "routes/events/EventAccess.hpp"
#include "services/FanCoLab.hpp"
#include "utils/web/AssetVersion.hpp"
#include "utils/LocalTime.hpp"
#include "utils/text/Money.hpp"
#include <crow/mustache.h>
#include <algorithm>
#include <cstdio>
#include <set>

void register_report_routes(LugApp& app, SqliteDatabase& db, EventService& events,
                            EventDayRepository& days, EventDayAttendanceRepository& day_att,
                            std::shared_ptr<DisplayRequestRepository> displays,
                            ChapterMemberRepository& chapter_members, AuditService& audit) {

    CROW_ROUTE(app, "/reports/annual")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        int year = local_year();
        if (const char* y = req.url_params.get("year")) { try { year = std::stoi(y); } catch (...) {} }
        std::string ys = std::to_string(year), lo = ys + "-01-01", hi = std::to_string(year + 1) + "-01-01";
        const std::vector<std::string> range{lo, hi};

        crow::mustache::context ctx;
        ctx["year"] = year;
        ctx["prev_year"] = year - 1;
        ctx["next_year"] = year + 1;
        ctx["members_total"] = query_int(db, "SELECT COUNT(*) FROM members WHERE created_at < ?", {hi});
        ctx["members_new"]   = query_int(db, "SELECT COUNT(*) FROM members WHERE created_at >= ? AND created_at < ?", range);
        ctx["members_paid"]  = query_int(db, "SELECT COUNT(*) FROM members WHERE is_paid=1", {});
        ctx["dues_collected"] = money(query_int(db,
            "SELECT COALESCE(SUM(amount_cents),0) FROM dues_payments WHERE paid_on >= ? AND paid_on < ?", range));
        ctx["meetings_held"] = query_int(db,
            "SELECT COUNT(*) FROM meetings WHERE status <> 'cancelled' AND start_time >= ? AND start_time < ?", range);
        ctx["meeting_checkins"] = query_int(db,
            "SELECT COUNT(*) FROM attendance a JOIN meetings mt ON mt.id = a.entity_id "
            "WHERE a.entity_type='meeting' AND mt.start_time >= ? AND mt.start_time < ?", range);
        ctx["events_held"] = query_int(db,
            "SELECT COUNT(*) FROM lug_events WHERE status <> 'cancelled' AND start_time >= ? AND start_time < ?", range);
        ctx["event_attendees"] = query_int(db,
            "SELECT COUNT(*) FROM (SELECT DISTINCT ed.event_id, eda.member_id FROM event_day_attendance eda "
            "JOIN event_days ed ON ed.id = eda.event_day_id WHERE ed.day_date >= ? AND ed.day_date < ?)", range);
        ctx["active_members"] = query_int(db,
            "SELECT COUNT(*) FROM (SELECT a.member_id FROM attendance a JOIN meetings mt ON mt.id = a.entity_id "
            " WHERE a.entity_type='meeting' AND mt.start_time >= ?1 AND mt.start_time < ?2 "
            " UNION SELECT eda.member_id FROM event_day_attendance eda JOIN event_days ed ON ed.id = eda.event_day_id "
            " WHERE ed.day_date >= ?1 AND ed.day_date < ?2)", range);
        int64_t kids = query_int(db, "SELECT COALESCE(SUM(public_kids),0) FROM lug_events WHERE start_time >= ? AND start_time < ?", range);
        int64_t teens = query_int(db, "SELECT COALESCE(SUM(public_teens),0) FROM lug_events WHERE start_time >= ? AND start_time < ?", range);
        int64_t adults = query_int(db, "SELECT COALESCE(SUM(public_adults),0) FROM lug_events WHERE start_time >= ? AND start_time < ?", range);
        ctx["visitors_total"] = kids + teens + adults;
        ctx["visitors_kids"] = kids;
        ctx["visitors_teens"] = teens;
        ctx["visitors_adults"] = adults;

        // Check-ins per month (meetings + event-days), as CSS bars.
        int per_month[12] = {0};
        {
            auto st = db.prepare(
                "SELECT CAST(substr(d,6,2) AS INTEGER), COUNT(*) FROM ("
                " SELECT mt.start_time AS d FROM attendance a JOIN meetings mt ON mt.id = a.entity_id "
                "  WHERE a.entity_type='meeting' AND mt.start_time >= ?1 AND mt.start_time < ?2 "
                " UNION ALL SELECT ed.day_date FROM event_day_attendance eda JOIN event_days ed ON ed.id = eda.event_day_id "
                "  WHERE ed.day_date >= ?1 AND ed.day_date < ?2) GROUP BY 1");
            st.bind(1, lo); st.bind(2, hi);
            while (st.step()) { int m = static_cast<int>(st.col_int(0)); if (m >= 1 && m <= 12) per_month[m - 1] = static_cast<int>(st.col_int(1)); }
        }
        int peak = *std::max_element(per_month, per_month + 12);
        static const char* mon[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        crow::json::wvalue months = crow::json::wvalue::list();
        for (int i = 0; i < 12; ++i) {
            months[i]["label"] = mon[i];
            months[i]["count"] = per_month[i];
            months[i]["pct"] = peak > 0 ? per_month[i] * 100 / peak : 0;
        }
        ctx["months"] = std::move(months);

        // Top attendees
        {
            auto st = db.prepare(
                "SELECT COALESCE(m.display_name,''), COUNT(*) AS n FROM ("
                " SELECT a.member_id AS mid FROM attendance a JOIN meetings mt ON mt.id = a.entity_id "
                "  WHERE a.entity_type='meeting' AND mt.start_time >= ?1 AND mt.start_time < ?2 "
                " UNION ALL SELECT DISTINCT eda.member_id FROM event_day_attendance eda JOIN event_days ed ON ed.id = eda.event_day_id "
                "  WHERE ed.day_date >= ?1 AND ed.day_date < ?2 GROUP BY ed.event_id, eda.member_id) x "
                "JOIN members m ON m.id = x.mid GROUP BY x.mid ORDER BY n DESC, m.display_name LIMIT 10");
            st.bind(1, lo); st.bind(2, hi);
            crow::json::wvalue top = crow::json::wvalue::list();
            int i = 0;
            while (st.step()) { top[i]["name"] = st.col_text(0); top[i]["count"] = st.col_int(1); ++i; }
            ctx["top"] = std::move(top);
            ctx["has_top"] = i > 0;
        }
        // Events
        {
            auto st = db.prepare(
                "SELECT e.id, e.title, substr(e.start_time,1,10), e.public_kids + e.public_teens + e.public_adults, "
                "(SELECT COUNT(DISTINCT eda.member_id) FROM event_day_attendance eda JOIN event_days ed ON ed.id = eda.event_day_id WHERE ed.event_id = e.id) "
                "FROM lug_events e WHERE e.status <> 'cancelled' AND e.start_time >= ? AND e.start_time < ? ORDER BY e.start_time");
            st.bind(1, lo); st.bind(2, hi);
            crow::json::wvalue evs = crow::json::wvalue::list();
            int i = 0;
            const std::string today = local_iso_now().substr(0, 10);
            while (st.step()) {
                evs[i]["id"] = st.col_int(0); evs[i]["title"] = st.col_text(1); evs[i]["date"] = friendly_date(st.col_text(2));
                evs[i]["upcoming"] = st.col_text(2) > today;
                evs[i]["visitors"] = st.col_int(3); evs[i]["members"] = st.col_int(4); ++i;
            }
            ctx["events"] = std::move(evs);
            ctx["has_events"] = i > 0;
        }
        // ── Growth & retention ──
        auto active_ids = [&](int y) {
            std::set<int64_t> ids;
            auto st = db.prepare(
                "SELECT a.member_id FROM attendance a JOIN meetings mt ON mt.id = a.entity_id "
                " WHERE a.entity_type='meeting' AND mt.start_time >= ?1 AND mt.start_time < ?2 "
                "UNION SELECT eda.member_id FROM event_day_attendance eda JOIN event_days ed ON ed.id = eda.event_day_id "
                " WHERE ed.day_date >= ?1 AND ed.day_date < ?2");
            st.bind(1, std::to_string(y) + "-01-01"); st.bind(2, std::to_string(y + 1) + "-01-01");
            while (st.step()) ids.insert(st.col_int(0));
            return ids;
        };
        {
            auto now_ids = active_ids(year), prev_ids = active_ids(year - 1);
            int returning = 0;
            for (auto id : prev_ids) returning += now_ids.count(id) ? 1 : 0;
            ctx["prev_active"] = static_cast<int>(prev_ids.size());
            ctx["returning"] = returning;
            ctx["lapsed"] = static_cast<int>(prev_ids.size()) - returning;
            ctx["first_timers"] = static_cast<int>(now_ids.size()) - returning;
            ctx["retention_pct"] = prev_ids.empty() ? 0 : returning * 100 / static_cast<int>(prev_ids.size());
            ctx["has_prev"] = !prev_ids.empty();
        }
        // Five-year trend
        {
            crow::json::wvalue rows = crow::json::wvalue::list();
            int i = 0;
            bool started = false;  // skip the empty years before the LUG's records begin
            for (int y = year - 4; y <= year; ++y) {
                std::vector<std::string> r{std::to_string(y) + "-01-01", std::to_string(y + 1) + "-01-01"};
                int64_t held = query_int(db, "SELECT COUNT(*) FROM meetings WHERE status <> 'cancelled' AND start_time >= ? AND start_time < ?", r);
                int64_t checkins = query_int(db, "SELECT COUNT(*) FROM attendance a JOIN meetings mt ON mt.id = a.entity_id "
                                              "WHERE a.entity_type='meeting' AND mt.start_time >= ? AND mt.start_time < ?", r);
                int64_t visitors = query_int(db, "SELECT COALESCE(SUM(public_kids+public_teens+public_adults),0) FROM lug_events "
                                              "WHERE start_time >= ? AND start_time < ?", r);
                const int active = static_cast<int>(active_ids(y).size());
                const int64_t new_members = query_int(db, "SELECT COUNT(*) FROM members WHERE created_at >= ? AND created_at < ?", r);
                started = started || y == year || held || active || new_members || visitors;
                if (!started) continue;
                rows[i]["year"] = y;
                rows[i]["active"] = active;
                rows[i]["new_members"] = new_members;
                rows[i]["meetings"] = held;
                char avg[16];
                std::snprintf(avg, sizeof(avg), "%.1f", held ? static_cast<double>(checkins) / held : 0.0);
                rows[i]["avg"] = std::string(avg);
                rows[i]["visitors"] = visitors;
                rows[i]["current"] = y == year;
                ++i;
            }
            ctx["trend"] = std::move(rows);
        }
        // New members per month
        {
            int nm[12] = {0};
            auto st = db.prepare("SELECT CAST(substr(created_at,6,2) AS INTEGER), COUNT(*) FROM members "
                                 "WHERE created_at >= ? AND created_at < ? GROUP BY 1");
            st.bind(1, lo); st.bind(2, hi);
            while (st.step()) { int m = static_cast<int>(st.col_int(0)); if (m >= 1 && m <= 12) nm[m - 1] = static_cast<int>(st.col_int(1)); }
            int top = *std::max_element(nm, nm + 12);
            crow::json::wvalue arr = crow::json::wvalue::list();
            for (int i = 0; i < 12; ++i) {
                arr[i]["label"] = mon[i]; arr[i]["count"] = nm[i]; arr[i]["pct"] = top > 0 ? nm[i] * 100 / top : 0;
            }
            ctx["new_months"] = std::move(arr);
        }
        // Busiest venues (meetings + events, by location)
        {
            auto st = db.prepare(
                "SELECT loc, COUNT(*) AS gatherings, SUM(n) AS people FROM ("
                " SELECT TRIM(mt.location) AS loc, (SELECT COUNT(*) FROM attendance a WHERE a.entity_type='meeting' AND a.entity_id=mt.id) AS n "
                "  FROM meetings mt WHERE mt.status <> 'cancelled' AND mt.is_virtual = 0 AND mt.start_time >= ?1 AND mt.start_time < ?2 "
                " UNION ALL SELECT TRIM(e.location), e.public_kids + e.public_teens + e.public_adults + "
                "  (SELECT COUNT(DISTINCT eda.member_id) FROM event_day_attendance eda JOIN event_days ed ON ed.id = eda.event_day_id WHERE ed.event_id = e.id) "
                "  FROM lug_events e WHERE e.status <> 'cancelled' AND e.start_time >= ?1 AND e.start_time < ?2) "
                "WHERE COALESCE(loc,'') <> '' GROUP BY loc COLLATE NOCASE ORDER BY people DESC, gatherings DESC LIMIT 8");
            st.bind(1, lo); st.bind(2, hi);
            crow::json::wvalue v = crow::json::wvalue::list();
            int i = 0;
            while (st.step()) { v[i]["place"] = st.col_text(0); v[i]["gatherings"] = st.col_int(1); v[i]["people"] = st.col_int(2); ++i; }
            ctx["venues"] = std::move(v);
            ctx["has_venues"] = i > 0;
        }
        add_fan_colab(ctx, db);
        std::string page = crow::mustache::load("reports/_annual.html").render(ctx).dump();
        return html_page(req, app, page, "Annual Report " + ys, "active_reports");
    });

    CROW_ROUTE(app, "/events/<int>/report")([&app, &db, &events, &days, &day_att, displays, &chapter_members, &audit](
            const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }

        crow::mustache::context ctx;
        ctx["title"] = ev->title;
        ctx["start"] = ev->start_time.substr(0, 10);
        ctx["end"] = ev->end_time.substr(0, 10);
        ctx["multi_day"] = ev->end_time.substr(0, 10) != ev->start_time.substr(0, 10);
        ctx["location"] = ev->location;
        ctx["lead"] = ev->event_lead_name;
        ctx["description"] = ev->description;
        ctx["entrance_fee"] = ev->entrance_fee;
        ctx["kids"] = ev->public_kids;
        ctx["teens"] = ev->public_teens;
        ctx["adults"] = ev->public_adults;
        ctx["visitors"] = ev->public_kids + ev->public_teens + ev->public_adults;
        ctx["social"] = ev->social_media_links;
        ctx["feedback"] = ev->event_feedback;
        ctx["notes"] = ev->notes;

        crow::json::wvalue dayarr = crow::json::wvalue::list();
        auto dl = days.find_by_event(ev->id);
        std::unordered_map<int64_t, bool> distinct;
        for (size_t i = 0; i < dl.size(); ++i) {
            auto rows = day_att.find_by_day(dl[i].id);
            dayarr[i]["day"] = dl[i].day_number;
            dayarr[i]["date"] = dl[i].day_date;
            dayarr[i]["count"] = static_cast<int>(rows.size());
            std::string names;
            for (const auto& r : rows) { names += (names.empty() ? "" : ", ") + r.member_display_name; distinct[r.member_id] = true; }
            dayarr[i]["names"] = names;
        }
        ctx["days"] = std::move(dayarr);
        ctx["members"] = static_cast<int>(distinct.size());

        int approved = 0; long sq_in = 0; std::string mocs;
        for (const auto& d : displays->list_for_event(ev->id)) {
            if (d.status != "approved") continue;
            ++approved;
            sq_in += static_cast<long>(d.width_in) * d.depth_in;
            mocs += (mocs.empty() ? "" : "; ") + d.title + " (" + d.member_display_name + ")";
        }
        char sqft[32];
        std::snprintf(sqft, sizeof(sqft), "%.1f", sq_in / 144.0);
        ctx["displays"] = approved;
        ctx["display_sqft"] = std::string(sqft);
        ctx["mocs"] = mocs;
        {
            auto st = db.prepare("SELECT COALESCE(SUM(CASE WHEN kind='income' THEN amount_cents END),0), "
                                 "COALESCE(SUM(CASE WHEN kind='expense' THEN amount_cents END),0), COUNT(*) "
                                 "FROM treasury_entries WHERE event_id=?");
            st.bind(1, ev->id);
            if (st.step() && st.col_int(2) > 0) {
                ctx["has_money"] = true;
                ctx["money_in"] = money(st.col_int(0));
                ctx["money_out"] = money(st.col_int(1));
                ctx["money_net"] = money(st.col_int(0) - st.col_int(1));
            }
        }
        {
            auto st = db.prepare("SELECT public_interest FROM lug_events WHERE id=?");
            st.bind(1, ev->id);
            if (st.step() && st.col_int(0) > 0) ctx["public_interest"] = st.col_int(0);
        }
        add_fan_colab(ctx, db);
        ctx["asset_v"] = asset_version();
        audit.log(req, app, "event.report_view", "event", ev->id, ev->title, "Viewed event report");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(crow::mustache::load("reports/_event.html").render(ctx).dump());
        return res;
    });
}
