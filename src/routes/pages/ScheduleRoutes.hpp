#pragma once
#include "db/SqliteDatabase.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "routes/accounts/AuthRoutes.hpp"

// GET /schedule - meetings and events together, as a list (default) or a
// month calendar, with filters kept in the URL:
//   view=list|calendar  type=all|meetings|events  when=upcoming|past|all
//   scope=all|group|external|chapters|chapter:<id>  status=active|tentative|cancelled|all
//   mine=1  q=<search>  month=YYYY-MM (calendar)
// Meetings and events keep their own pages, forms and check-in; this is the
// place to browse them.
void register_schedule_routes(LugApp& app, SqliteDatabase& db, ChapterMemberRepository& chapter_members);
