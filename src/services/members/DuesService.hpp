#pragma once
#include "integrations/discord/DiscordClient.hpp"
#include "repositories/members/DuesRepository.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include "services/AuditService.hpp"
#include "services/notifications/Notifier.hpp"
#include "services/Features.hpp"
#include <memory>
#include "utils/LocalTime.hpp"
#include <ctime>
#include <string>

// Periodic dues bookkeeping:
//  - flips is_paid off for members whose paid_until has passed (paid_until
//    was never enforced before - "paid" stayed on forever);
//  - optionally DMs a renewal reminder `dues_reminder_days` days ahead
//    (setting; 0/empty = off), once per paid_until value.
class DuesService {
public:
    DuesService(DuesRepository& dues, SettingsRepository& settings, DiscordClient& discord,
                AuditService& audit)
        : dues_(dues), settings_(settings), discord_(discord), audit_(audit) {}

    static std::string ymd(std::time_t t) {
        std::tm tm = local_tm(t);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        return buf;
    }

    struct Result { int expired = 0; int reminded = 0; };

    Result run_once(std::time_t now = std::time(nullptr)) {
        Result r;
        if (!Features::on("dues")) return r;   // Settings > Features
        std::string today = ymd(now);
        // Grace period (Settings > Dues): lapse only once paid_until is more
        // than `dues_grace_days` days ago.
        int grace = 0;
        try { grace = std::stoi(settings_.get("dues_grace_days", "0")); } catch (...) {}
        if (grace < 0 || grace > 365) grace = 0;
        const std::string cutoff = ymd(now - static_cast<std::time_t>(grace) * 86400);
        for (const auto& m : dues_.expire_lapsed(cutoff)) {
            audit_.log_system("member.dues_expired", "member", m.member_id, m.display_name,
                              "Dues lapsed (paid until " + m.paid_until + ")");
            ++r.expired;
        }
        int days = 0;
        try { days = std::stoi(settings_.get("dues_reminder_days", "0")); } catch (...) {}
        if (days > 0 && days <= 90) {
            std::string until = ymd(now + static_cast<std::time_t>(days) * 86400);
            for (const auto& m : dues_.needing_reminder(today, until)) {
                // Mark first: a DM failure (closed DMs) shouldn't retry every 10 minutes.
                dues_.mark_reminded(m.member_id, m.paid_until);
                if (!notifier_) notifier_ = std::make_shared<Notifier>(dues_.db(), discord_, nullptr, "");
                if (notifier_->notify(m.member_id, "dues_reminder", "dm.dues_reminder",
                                      {{"name", m.display_name}, {"paid_until", m.paid_until}}, false, "",
                                      Notifier::about_for("dues_reminder", m.paid_until)))
                    ++r.reminded;
            }
        }
        return r;
    }

    void set_notifier(std::shared_ptr<Notifier> n) { notifier_ = std::move(n); }

private:
    std::shared_ptr<Notifier> notifier_;
    DuesRepository&     dues_;
    SettingsRepository& settings_;
    DiscordClient&      discord_;
    AuditService&       audit_;
};
