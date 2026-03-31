#include "services/MemberService.hpp"

MemberService::MemberService(MemberRepository& repo) : repo_(repo) {}

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
    if (m.discord_user_id.empty()) {
        throw std::invalid_argument("discord_user_id required");
    }
    if (m.display_name.empty()) {
        throw std::invalid_argument("display_name required");
    }
    return repo_.create(m);
}

Member MemberService::update(int64_t id, const Member& updates) {
    auto existing = repo_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("Member not found: " + std::to_string(id));
    }
    Member m = *existing;
    if (!updates.display_name.empty()) m.display_name = updates.display_name;
    if (!updates.discord_username.empty()) m.discord_username = updates.discord_username;
    if (!updates.email.empty()) m.email = updates.email;
    if (!updates.role.empty()) m.role = updates.role;
    if (!updates.paid_until.empty()) m.paid_until = updates.paid_until;
    m.is_paid = updates.is_paid;
    repo_.update(m);
    return repo_.find_by_id(id).value_or(m);
}

void MemberService::delete_member(int64_t id) {
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

DatatableResult MemberService::datatable(const DatatableParams& p) {
    DatatableResult r;
    r.draw             = p.draw;
    r.records_total    = repo_.count_all();
    r.records_filtered = repo_.count_search(p.search);
    r.data             = repo_.find_paginated(p.search, p.sort_col, p.sort_dir,
                                              p.length, p.start);
    return r;
}
