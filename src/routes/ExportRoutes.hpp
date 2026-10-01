#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "repositories/PerkLevelRepository.hpp"
#include "services/AuditService.hpp"

// CSV downloads: members (chapter lead+), attendance overview by year with
// perk tiers (admin), audit log (admin).
void register_export_routes(LugApp& app, MemberRepository& members, AttendanceRepository& attendance,
                            PerkLevelRepository& perks, AuditService& audit);
