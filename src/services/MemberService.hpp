#pragma once
#include "repositories/MemberRepository.hpp"
#include "models/Member.hpp"
#include <vector>
#include <optional>
#include <string>
#include <stdexcept>

struct DatatableParams {
    int         draw     = 1;
    int         start    = 0;
    int         length   = 25;
    std::string search;
    std::string sort_col;   // SQL column name (already validated by route)
    std::string sort_dir;   // "asc" | "desc"
};

struct DatatableResult {
    int                 draw;
    int                 records_total;
    int                 records_filtered;
    std::vector<Member> data;
};

class MemberService {
public:
    explicit MemberService(MemberRepository& repo);

    std::optional<Member> get(int64_t id);
    std::optional<Member> get_by_discord_id(const std::string& discord_id);
    std::vector<Member>   list_all();
    std::vector<Member>   list_paid();
    std::vector<Member>   search(const std::string& q);
    DatatableResult       datatable(const DatatableParams& p);

    Member create(const Member& m);
    Member update(int64_t id, const Member& updates);
    void   delete_member(int64_t id);
    void   set_paid(int64_t id, bool paid, const std::string& paid_until);
    void   set_chapter(int64_t id, int64_t chapter_id);

private:
    MemberRepository& repo_;
};
