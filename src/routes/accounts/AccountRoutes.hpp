#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/members/MemberService.hpp"
#include "services/AuditService.hpp"
#include "services/PhotoStore.hpp"
#include <memory>

// "My Account": notification preferences, signed-in devices, download my
// data (JSON) and delete my account.
void register_account_routes(LugApp& app, SqliteDatabase& db, MemberService& members,
                             std::shared_ptr<PhotoStore> photos, AuditService& audit);
