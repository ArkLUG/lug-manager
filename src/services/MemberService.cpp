#include "services/MemberService.hpp"
#include <algorithm>
#include <iostream>
#include <thread>
#include <chrono>
#include <regex>

// Normalize phone to (XXX) XXX-XXXX format; returns empty if invalid/empty
static std::string normalize_phone(const std::string& raw) {
    if (raw.empty()) return "";
    std::string digits;
    for (char c : raw) if (c >= '0' && c <= '9') digits += c;
    if (digits.size() == 11 && digits[0] == '1') digits = digits.substr(1); // strip leading 1
    if (digits.size() != 10) return raw; // return as-is if not 10 digits
    return "(" + digits.substr(0,3) + ") " + digits.substr(3,3) + "-" + digits.substr(6);
}

// Normalize state to uppercase 2-letter; returns as-is if not exactly 2 alpha
static std::string normalize_state(const std::string& raw) {
    if (raw.size() != 2) return raw;
    std::string out;
    for (char c : raw) {
        if (std::isalpha(static_cast<unsigned char>(c)))
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        else return raw;
    }
    return out;
}

// Validate ZIP is 5 or 5+4 format
static std::string normalize_zip(const std::string& raw) {
    if (raw.empty()) return "";
    static const std::regex zip_re(R"(\d{5}(-\d{4})?)");
    if (std::regex_match(raw, zip_re)) return raw;
    // Strip non-digits and try to format
    std::string digits;
    for (char c : raw) if (c >= '0' && c <= '9') digits += c;
    if (digits.size() == 5) return digits;
    if (digits.size() == 9) return digits.substr(0,5) + "-" + digits.substr(5);
    return raw; // return as-is
}

MemberService::MemberService(MemberRepository& repo, DiscordClient* discord)
    : repo_(repo), discord_(discord) {}

// Generate a nickname like "Aaron K." from first/last name.
// If another member would have the same nickname, add more letters from the last name.
std::string MemberService::generate_nickname(const std::string& first_name,
                                              const std::string& last_name,
                                              int64_t exclude_id) {
    if (first_name.empty()) return last_name.empty() ? "" : last_name;
    if (last_name.empty())  return first_name;

    auto all = repo_.find_all();

    // Try increasing lengths of last name initial: "K.", "Ki.", "Kim.", etc.
    for (size_t len = 1; len <= last_name.size(); ++len) {
        std::string candidate = first_name + " " + last_name.substr(0, len) + ".";

        bool conflict = false;
        for (auto& m : all) {
            if (m.id == exclude_id) continue;
            if (m.display_name == candidate) {
                conflict = true;
                break;
            }
        }
        if (!conflict) return candidate;
    }

    // All prefixes conflict — use full name
    return first_name + " " + last_name;
}

std::optional<Member> MemberService::get(int64_t id) {
    return repo_.find_by_id(id);
}

std::optional<Member> MemberService::get_by_discord_id(const std::string& discord_id) {
    return repo_.find_by_discord_id(discord_id);
}

std::vector<Member> MemberService::list_all() {
    return repo_.find_all();
}

std::vector<Member> MemberService::list_paid() {
    return repo_.find_paid();
}

Member MemberService::create(const Member& m) {
    if (m.first_name.empty() && m.discord_user_id.empty()) {
        throw std::invalid_argument("first_name or discord_user_id required");
    }
    Member to_create = m;
    to_create.phone = normalize_phone(to_create.phone);
    to_create.state = normalize_state(to_create.state);
    to_create.zip   = normalize_zip(to_create.zip);
    // Auto-generate display_name from first/last if both are set
    if (!to_create.first_name.empty() && !to_create.last_name.empty()) {
        to_create.display_name = generate_nickname(to_create.first_name, to_create.last_name, 0);
    }
    if (to_create.display_name.empty()) {
        if (!to_create.discord_username.empty())
            to_create.display_name = to_create.discord_username;
        else if (!to_create.first_name.empty())
            to_create.display_name = to_create.first_name;
        else
            to_create.display_name = to_create.discord_user_id;
    }
    return repo_.create(to_create);
}

Member MemberService::update(int64_t id, const Member& updates) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("Member not found: " + std::to_string(id));
    }
    Member m = *existing;
    std::string old_display_name = m.display_name;

    if (!updates.discord_username.empty()) m.discord_username = updates.discord_username;
    if (!updates.first_name.empty())       m.first_name       = updates.first_name;
    if (!updates.last_name.empty())        m.last_name        = updates.last_name;
    m.email            = updates.email;
    if (!updates.role.empty())             m.role             = updates.role;
    if (!updates.paid_until.empty())       m.paid_until       = updates.paid_until;
    m.is_paid      = updates.is_paid;
    m.phone        = normalize_phone(updates.phone);
    m.address_line1 = updates.address_line1;
    m.address_line2 = updates.address_line2;
    m.city         = updates.city;
    m.state        = normalize_state(updates.state);
    m.zip          = normalize_zip(updates.zip);
    m.birthday     = updates.birthday;
    // Per-field sharing: only update if explicitly set (non-empty and not "none" default)
    if (!updates.sharing_email.empty())    m.sharing_email    = updates.sharing_email;
    if (!updates.sharing_phone.empty())    m.sharing_phone    = updates.sharing_phone;
    if (!updates.sharing_address.empty())  m.sharing_address  = updates.sharing_address;
    if (!updates.sharing_birthday.empty()) m.sharing_birthday = updates.sharing_birthday;
    if (!updates.sharing_discord.empty())  m.sharing_discord  = updates.sharing_discord;
    if (!updates.fol_status.empty()) m.fol_status = updates.fol_status;

    // Regenerate display_name if first/last names are set
    if (!m.first_name.empty() && !m.last_name.empty()) {
        m.display_name = generate_nickname(m.first_name, m.last_name, m.id);
    } else if (!updates.display_name.empty()) {
        m.display_name = updates.display_name;
    }

    repo_.update(m);

    // Sync nickname to Discord if display name changed
    if (discord_ && !m.discord_user_id.empty() && m.display_name != old_display_name) {
        try {
            discord_->set_member_nickname(m.discord_user_id, m.display_name);
        } catch (const std::exception& ex) {
            std::cerr << "[MemberService] Warning: failed to sync nickname to Discord: " << ex.what() << "\n";
        }
    }

    return repo_.find_by_id(id).value_or(m);
}

void MemberService::delete_member(int64_t id) {
    if (discord_) {
        auto member = repo_.find_by_id(id);
        if (member && !member->discord_user_id.empty()) {
            try {
                discord_->kick_member(member->discord_user_id);
            } catch (const std::exception& ex) {
                std::cerr << "[MemberService] Warning: failed to kick Discord member: " << ex.what() << "\n";
            }
        }
    }
    repo_.delete_by_id(id);
}

void MemberService::set_paid(int64_t id, bool paid, const std::string& paid_until) {
    repo_.set_paid(id, paid, paid_until);
}

std::vector<Member> MemberService::search(const std::string& q) {
    if (q.empty()) return repo_.find_all();
    return repo_.find_search(q);
}

void MemberService::set_chapter(int64_t id, int64_t chapter_id) {
    repo_.set_chapter(id, chapter_id);
}

MemberService::NicknameResult MemberService::regenerate_all_nicknames() {
    NicknameResult result;
    auto all = repo_.find_all();
    for (auto& m : all) {
        if (m.first_name.empty() || m.last_name.empty()) {
            ++result.skipped;
            continue;
        }
        std::string new_nick = generate_nickname(m.first_name, m.last_name, m.id);
        if (new_nick != m.display_name) {
            result.changes.push_back({m.id, m.display_name, new_nick});
            m.display_name = new_nick;
            repo_.update(m);
            ++result.updated;
        } else {
            ++result.skipped;
        }
    }
    return result;
}

MemberService::SyncResult MemberService::sync_nicknames_to_discord() {
    SyncResult result;
    if (!discord_) return result;

    // Regenerate all nicknames first
    regenerate_all_nicknames();

    // Push to Discord (with rate limit spacing — ~10s per nickname change)
    auto all = repo_.find_all();
    bool first = true;
    for (auto& m : all) {
        if (m.discord_user_id.empty() || m.display_name.empty()) {
            ++result.skipped;
            continue;
        }
        // Brief pause between requests; DiscordClient handles retry_after if rate-limited
        if (!first) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        first = false;
        try {
            std::string err = discord_->set_member_nickname(m.discord_user_id, m.display_name);
            if (err.empty()) {
                ++result.synced;
            } else {
                ++result.errors;
                result.error_details.push_back(m.display_name + ": " + err);
            }
        } catch (const std::exception& ex) {
            ++result.errors;
            result.error_details.push_back(m.display_name + ": " + ex.what());
        }
    }
    return result;
}

DatatableResult MemberService::datatable(const DatatableParams& p) {
    DatatableResult r;
    r.draw             = p.draw;
    r.records_total    = repo_.count_all();
    r.records_filtered = repo_.count_search(p.search);
    r.data             = repo_.find_paginated(p.search, p.sort_col, p.sort_dir,
                                              p.length, p.start);
    return r;
}
