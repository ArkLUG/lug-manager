#pragma once
#include "repositories/MemberRepository.hpp"
#include "integrations/DiscordClient.hpp"
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
    MemberService(MemberRepository& repo, DiscordClient* discord = nullptr);

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

    // Generate nickname: "Aaron K." — adds more last name letters if conflicts exist
    std::string generate_nickname(const std::string& first_name, const std::string& last_name,
                                   int64_t exclude_id = 0);

    // Regenerate all display names from first/last name and save to DB
    struct NicknameResult { int updated = 0; int skipped = 0; };
    NicknameResult regenerate_all_nicknames();

    // Sync all member nicknames to Discord
    struct SyncResult {
        int synced = 0;
        int skipped = 0;
        int errors = 0;
        std::vector<std::string> error_details;
    };
    SyncResult sync_nicknames_to_discord();

private:
    MemberRepository& repo_;
    DiscordClient*    discord_;
};
