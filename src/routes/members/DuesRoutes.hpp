#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/members/DuesRepository.hpp"
#include "services/members/MemberService.hpp"
#include "services/AuditService.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include <memory>

// Also Settings > Dues (/settings/dues): the standard yearly amount, when the
// dues year ends, and whether part-year payments are prorated.
void register_dues_routes(LugApp& app, MemberService& members, std::shared_ptr<DuesRepository> dues,
                          SettingsRepository& settings, AuditService& audit);
