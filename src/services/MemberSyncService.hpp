#pragma once
#include "integrations/DiscordClient.hpp"
#include "repositories/MemberRepository.hpp"
#include "repositories/RoleMappingRepository.hpp"
#include "repositories/ChapterRepository.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include <string>

struct SyncResult {
    int imported = 0;  // New member records created
    int updated  = 0;  // Existing members updated (name, role, or chapter lead)
    int skipped  = 0;  // No role mapping match — not a LUG member
    int errors   = 0;
    std::string error_message;
};

class MemberSyncService {
public:
    MemberSyncService(DiscordClient& discord,
                      MemberRepository& member_repo,
                      RoleMappingRepository& role_mappings,
                      ChapterRepository& chapter_repo,
                      ChapterMemberRepository& chapter_member_repo);

    // Import/update all guild members and sync chapter lead roles.
    // Creates new members, updates display names and LUG roles, promotes/demotes chapter leads.
    // Preserves dues/paid status.
    SyncResult sync_from_guild();

private:
    DiscordClient&           discord_;
    MemberRepository&        member_repo_;
    RoleMappingRepository&   role_mappings_;
    ChapterRepository&       chapter_repo_;
    ChapterMemberRepository& chapter_member_repo_;
};
