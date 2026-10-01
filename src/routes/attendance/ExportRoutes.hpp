#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/events/AttendanceRepository.hpp"
#include "repositories/members/MemberRepository.hpp"
#include "repositories/members/PerkLevelRepository.hpp"
#include "services/AuditService.hpp"

// CSV downloads: members (chapter lead+), attendance overview by year with
// perk tiers (admin), audit log (admin).
void register_export_routes(LugApp& app, MemberRepository& members, AttendanceRepository& attendance,
                            PerkLevelRepository& perks, AuditService& audit);
