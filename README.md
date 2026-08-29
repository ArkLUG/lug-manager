# LUG Manager

> **Notice:** This project was built entirely using AI (Claude Code by Anthropic). All code, documentation, templates, and configuration were generated through AI-assisted development.

A modern web application for managing LEGO User Groups (LUGs). Built with **C++ (CrowCPP)**, **SQLite**, **Discord**, **Google Calendar**, and **iCal** integration.

## Features

- **Member Management**: Track members with contact info, age range (KFOL/TFOL/AFOL), birthday, dues, and Discord role sync. Members without Discord accounts (e.g. KFOLs) are fully supported. Phone numbers, addresses, and ZIP codes are auto-formatted and validated.
- **Per-Field Privacy Controls**: Members choose per-field who sees their PII (email, phone, address, birthday, Discord username). Three levels: Don't share, Verified members only, All members. Only the member themselves can change their privacy settings — admins cannot override.
- **Verified Members**: "Verified" means a member has physically attended at least one meeting/event (virtual attendance doesn't count) or has paid dues. This prevents random Discord joiners from scraping contact info.
- **Self-Service Profile**: Members can edit their own profile (name, contact info, privacy settings) without admin involvement via "Edit My Profile" on the dashboard and members page.
- **Role System**: Three global roles (admin, chapter_lead, member) mapped via Discord roles, plus chapter-level roles (lead, event_manager, member). Anyone in the Discord guild can log in. Chapter leads can add/edit members and manage dues. Only admins can delete members or change roles.
- **Chapter System**: Organize members into chapters with leads, event managers, Discord channels, and role sync. Chapter names shown in meeting/event scope columns.
- **Meeting & Event Management**: Sortable tables with search, pagination, and responsive mobile layout. Meetings support virtual mode with Discord voice channel selection. Events have tentative/confirmed status. Map links on locations open the user's default map app (Google Maps, Apple Maps, Waze, etc.).
- **QR Code Check-in**: Managers generate a QR code for any meeting or event. Attendees scan to reach a public check-in page with three methods: Discord OAuth (auto-creates member if new), search existing members, or manual entry for new members. Duplicate detection prevents double check-ins.
- **Notes & Reports**: Markdown notes on events/meetings with WYSIWYG editor (EasyMDE). Publish reports to Discord forum channels with attendance (virtual/in-person split for meetings, multi-day for events).
- **Attendance Tracking**: Admin/lead/event-manager managed attendance with virtual support, year-filtered overview with search/sort/pagination, per-member detail view, perk tier display, and hide-inactive toggle. Members cannot self-check-in — only managers can add attendees (or via QR check-in).
- **Perk Levels**: Admin-defined attendance tiers per year with Discord role rewards, minimum age range requirements, and paid dues prerequisites. Clone tiers between years. Dashboard shows progress with meetings attended, events attended, dues status, and what's needed for the next tier.
- **Audit Log**: Every action is tracked — who did what, when, to which entity, from what IP. Admin-only viewer with search, category filtering, and pagination. 47 distinct audited actions covering members, meetings, events, chapters, attendance, check-ins, perks, settings, syncs, and role mappings.
- **Discord Integration**: OAuth2 login, scheduled events, forum threads, announcements, role sync, perk role assignment, voice channel selection for virtual meetings, member sync every 6 hours
- **Google Calendar Integration**: Push events directly to a shared Google Calendar via service account, import existing events
- **iCal Feed**: RFC 5545 calendar subscription for personal calendar apps (collapsible on dashboard)
- **Help & Onboarding**: Role-aware help page with getting-started guides for each role (Member, Event Manager, Chapter Lead, Admin). Interactive multi-page tour powered by driver.js auto-navigates through the app, highlighting features the user actually has access to. Permissions quick-reference table.
- **Responsive UI**: Mobile-friendly tables that progressively hide columns on smaller screens. HTMX + Tailwind CSS interface with proper browser back/forward support. Detail modals for viewing full meeting/event info including attendance panel.
- **JSON API**: Full CRUD REST API (`/api/v1/*`) covering members, events, meetings, chapters, chapter roles, attendance, perk levels, role mappings, audit log, and settings. Authenticated by admin-issued API keys with `read`/`write`/`admin` scopes, fully independent of session login. Self-service key management at Settings > API Keys, interactive docs at `/static/api-docs.html`.

## Technology Stack

- **Backend**: C++20 with CrowCPP v1.2.0 (header-only HTTP server)
- **Database**: SQLite with WAL mode and automatic migrations (39 migrations)
- **Frontend**: HTMX + Tailwind CSS + TomSelect + EasyMDE + QRCode.js + DataTables + Driver.js (CDN, no build step)
- **External APIs**: Discord OAuth2 + REST, Google Calendar API v3
- **Dependencies**: asio, nlohmann/json, md4c, libcurl, OpenSSL, Google Test
- **CI/CD**: GitHub Actions (tests run inside Docker build, single-job pipeline)

## Prerequisites

- CMake 3.20+
- C++20 compiler (GCC 10+, Clang 12+)
- libcurl, sqlite3, OpenSSL (development headers)
- pkg-config
- Linux, macOS, or Windows (with WSL)

### Ubuntu/Debian:
```bash
sudo apt-get install cmake g++ pkg-config libcurl4-openssl-dev sqlite3 libsqlite3-dev libssl-dev
```

### Arch/Manjaro:
```bash
sudo pacman -S cmake gcc pkgconf curl sqlite openssl
```

### macOS:
```bash
brew install cmake curl sqlite openssl pkg-config
```

## Quick Start

1. **Clone and build**:
   ```bash
   git clone https://github.com/ArkLUG/lug-manager.git
   cd lug-manager
   cmake -B build -S .
   cmake --build build -j$(nproc)
   ```

2. **Run tests**:
   ```bash
   cmake -B build -S . -DBUILD_TESTS=ON
   cmake --build build -j$(nproc)
   ctest --test-dir build --output-on-failure -j$(nproc)
   ```

3. **Configure**:
   ```bash
   cp .env.example .env
   # Edit .env with your Discord credentials
   ```

   Required environment variables:
   | Variable | Description |
   |----------|-------------|
   | `DISCORD_BOT_TOKEN` | Discord bot token (from Developer Portal) |
   | `DISCORD_CLIENT_ID` | Discord OAuth2 application client ID |
   | `DISCORD_CLIENT_SECRET` | Discord OAuth2 application client secret |
   | `DISCORD_REDIRECT_URI` | OAuth2 callback URL (e.g. `http://localhost:8080/auth/callback`) |
   | `BOOTSTRAP_ADMIN_DISCORD_ID` | Your Discord user ID (creates admin account on first login) |

   Optional defaults (can also be set from the Settings page after first login):
   | Variable | Description | Default |
   |----------|-------------|---------|
   | `ICAL_TIMEZONE` | IANA timezone name | `America/New_York` |
   | `ICAL_CALENDAR_NAME` | Calendar display name | `LUG Events` |
   | `DISCORD_GUILD_ID` | Discord server ID | *(set in Settings)* |

   Optional, only needed for [in-Discord match resolution](#discord-member-match-review) (no Settings-page equivalent — startup secrets, not runtime config):
   | Variable | Description |
   |----------|-------------|
   | `DISCORD_PUBLIC_KEY` | Application's Ed25519 public key (Developer Portal > General Information) |
   | `DISCORD_APPLICATION_ID` | Application ID (Developer Portal > General Information) |

4. **Run**:
   ```bash
   ./build/lug_manager
   ```
   Open `http://localhost:8080` and log in with Discord. The first user matching `BOOTSTRAP_ADMIN_DISCORD_ID` is automatically made admin. All other guild members can log in as regular members.

## Roles & Permissions

### Global Roles

| Role | Access |
|------|--------|
| **admin** | Full access to everything |
| **chapter_lead** | Can see all PII, manage members (add/edit/dues), manage chapter members, assign event managers. Cannot delete members, change roles, or edit system settings. |
| **moderator** | Same privilege tier as chapter_lead — can see all PII, manage members, manage chapter members, and review Discord member matches. Cannot delete members, change roles, or edit system settings. |
| **member** | View events, meetings, chapters, own attendance. PII visibility depends on each member's per-field privacy settings. |

`admin` and `member` are assignable via Discord role mapping (Settings > Role Mappings) as well as manually. `chapter_lead` and `moderator` are granted manually only, from a member's edit form — there's no Discord-role-mapping path for either.

### Chapter Roles (assigned per-chapter by admins/leads)

| Role | Access |
|------|--------|
| **lead** | Manage chapter members + assign event_manager + everything event_manager can do |
| **event_manager** | Create/edit/delete events and meetings in their chapter, manage attendance, generate QR check-in codes |
| **member** | Basic chapter membership |

### PII Privacy

Each member controls their own privacy settings via "Edit My Profile". Per field (email, phone, address, birthday, Discord username), they choose:

| Setting | Who can see |
|---------|-------------|
| **Don't share** | Only admins and chapter leads |
| **Verified members** | Members who have attended in-person or paid dues |
| **All members** | Any logged-in member |

Admins and chapter leads always see all PII regardless of settings. Members always see their own info. Virtual-only attendance does not count toward "verified" status.

## QR Code Check-in

Managers (admin, chapter lead, event lead, event manager) can generate a QR code for any meeting or event from the detail modal. When scanned, attendees reach a public check-in page with three options:

1. **Discord**: OAuth login that auto-creates a member account if new, then checks in
2. **Find Me**: Search existing members by name, select, and check in
3. **New Member**: Enter first/last name and optional email. If a matching name exists, checks in the existing member instead of creating a duplicate.

The QR token is generated once per entity and reused by all managers.

## Virtual Meetings

Meetings can be marked as "Virtual" in the create/edit form. Virtual meetings:
- Hide the location field and show a Discord voice channel dropdown instead
- Display "Virtual (Discord)" as the location in calendar feeds and Discord events
- Show a camera icon and "Virtual" label in the meetings table
- Provide a "Join Voice Channel" link in the detail modal
- Show a virtual attendance toggle on the QR check-in page

## Discord Setup

1. Go to [discord.com/developers/applications](https://discord.com/developers/applications) and create a new application.

2. **OAuth2** tab:
   - Add redirect URI: `http://your-server:8080/auth/callback`
   - Copy **Client ID** and **Client Secret** to `.env`

3. **Bot** tab:
   - Create a bot and copy the **Token** to `.env`
   - Enable the **Server Members Intent** (required for role sync)

4. **Invite the bot** to your server using OAuth2 URL Generator:
   - Scopes: `bot`, `identify`, `guilds`
   - Bot permissions: `Manage Events`, `Create Public Threads`, `Send Messages`, `Manage Roles`, `Manage Channels`

5. **Important**: In your Discord server's role settings, drag the bot's role **above** any roles it needs to manage. Discord requires the bot role to be higher in the hierarchy.

6. After first login, go to **Settings** in the web UI to configure guild ID, channels, roles, and timezone.

## Discord Member Match Review

Every 6 hours (and on manual "Sync Now"), LUG Manager pulls the Discord guild's member list and reconciles it against the roster. An exact `discord_user_id` match updates the existing member; no match at all creates a new one. When a guild member's name is a *plausible but not certain* match for an existing Discord-less member (e.g. a KFOL added by a parent before the kid joined Discord), the match is held in a review queue instead of silently creating a duplicate.

Admins, moderators, and chapter leads can resolve these from **Settings > Member Import > pending Discord match(es)**, or from `/settings/discord-matches` directly — link the Discord identity to the suggested (or any other) existing member, or create a new member from it.

### Resolving matches from Discord

Matches can also be resolved without leaving Discord. If a notification channel is configured (**Settings > Match Notification Channel**), each new pending match posts a message with a **Resolve Match** button to that channel. Clicking it opens a Discord modal with a single field — a member ID to link to, or blank to create a new member — right where the click happened.

Access is restricted to an explicit, admin-configured Discord role allowlist (**Settings > Authorized Roles**) — never a role-name match, since guild roles can be renamed or recreated by anyone with the right Discord permissions. Leave the notification channel unset to disable this entirely; the browser-based review queue always still works regardless.

This requires the app's own Discord Application **Public Key** and **Application ID** (Developer Portal > General Information — distinct from the bot token and OAuth2 client secret) set as `DISCORD_PUBLIC_KEY` / `DISCORD_APPLICATION_ID` in `.env`. Every request to `/discord/interactions` is verified against the Public Key with Ed25519 before anything else happens — requests that don't verify get a bare `401` with no further processing. After deploying with these set, register `https://your-domain/discord/interactions` as the **Interactions Endpoint URL** in the Developer Portal; Discord sends a signed PING and requires a valid response before the field will save, so this step must happen after the app is live, not before.

## Google Calendar Integration

LUG Manager can push meetings and events directly to a shared Google Calendar.

### Setup

1. **Create a Google Cloud project** and enable the **Google Calendar API**
2. **Create a service account** with a JSON key file
3. **Share your Google Calendar** with the service account email as an editor
4. **Configure in Settings**: enter the JSON path and Calendar ID

See the Settings page for detailed instructions. Import existing events with the "Import from Google Calendar" button.

## Chapters

Chapters allow you to organize your LUG into sub-groups. Each chapter can have:

- **Chapter leads** with a mapped Discord role (automatically synced)
- **Event managers** who can create/edit events and meetings for their chapter
- **A Discord announcement channel** for chapter-specific announcements
- **Members** assigned via the chapter management page

Chapter leads can manage members and assign event managers. Only admins can add/remove chapter leads or edit chapter settings.

## Attendance & Perk Levels

### Attendance
- **Manager-controlled**: Only admins, chapter leads, event leads, and event managers can check in members (via attendance panel or QR code)
- **Virtual support**: Meetings track in-person vs. virtual attendance (virtual does not count toward "verified" member status)
- **Overview**: Year-filtered, paginated, searchable, sortable table with last-seen date and perk tier
- **Per-member detail**: Expandable view of attended events/meetings for the selected year
- **Hide inactive**: Toggle to hide members with no attendance and no dues

### Perk Levels
- Admin-defined tiers per calendar year (e.g. Bronze, Silver, Gold)
- Separate meeting and event attendance thresholds
- Optional paid dues and minimum age range (KFOL/TFOL/AFOL) requirements
- Discord role auto-assigned when a member qualifies
- Clone tiers between years for easy setup
- Dashboard shows progress: meetings attended, events attended, dues status, and remaining requirements for the next tier

## Event Reports

Events and meetings support markdown notes and structured report fields:

**Meeting reports** (published to Discord forum):
```
Chapter: [chapter name]
Meeting date: [date]
Members by name: [in-person]; Virtual: [virtual]
Topics: [notes]
```

**Event reports** (published to Discord forum):
```
Event name: [title]
Start date / End date
Entrance fee: [fee]
Member names day1: [names by check-in date]
Public kids / teens / adults: [counts]
Social media links: [links]
What you liked best: [feedback]
```

## Settings (Admin)

All runtime configuration is managed from the Settings page (`/settings`):

| Setting | Description |
|---------|-------------|
| Discord Guild ID | Your Discord server ID |
| Announcements Channel | Channel for LUG-wide announcements |
| Event Forum Channel | Forum channel for event discussion threads |
| Event/Meeting Reports Forum | Forum channels for published reports |
| Announcement/Non-LUG Roles | Roles @mentioned in announcements |
| Timezone | IANA timezone for Discord/Calendar |
| Calendar Name | Display name in calendar apps |
| Suppress Pings/Updates | Disable @mentions or update notifications |
| Google Calendar | Service account JSON path and Calendar ID |
| Role Mappings | Map Discord roles to admin/member |
| Match Notification Channel | Discord channel for Resolve Match buttons (Discord member match review); unset disables |
| Authorized Roles | Discord role-ID allowlist permitted to resolve matches via the Discord button/modal |
| Perk Levels | Attendance tiers with Discord role rewards (per year) |
| Audit Log | Searchable history of all actions taken by all users |

## JSON API

LUG Manager exposes a full CRUD JSON API under `/api/v1/*` for programmatic access — separate from the browser session used by the web UI.

### Authentication

API requests are authenticated with an **API key**, sent as either:
```
X-API-Key: <key>
```
or
```
Authorization: Bearer <key>
```

Keys are issued by admins from **Settings > API Keys**. The plaintext key is shown once at creation time and is never stored or displayed again — only its SHA-256 hash is kept. Revoking a key takes effect immediately.

### Scopes

Each key has one of three scopes, checked per-request:

| Scope | Grants |
|-------|--------|
| `read` | All `GET` endpoints |
| `write` | `read`, plus create/update/delete for routine content (events, meetings, members, attendance, chapters, perk levels) |
| `admin` | `write`, plus role/settings changes and destructive operations — required for member role changes, role mappings (all verbs, including reads, since they control privilege), settings (all verbs), and deletes of members/chapters/events/meetings/perk levels |

A key's scope is independent of the issuing admin's own session — API keys never inherit or check the web session at all, and a session cookie alone does not grant API access.

### Coverage

Members, events (+ per-day schedule), meetings, chapters, chapter member roles, attendance (+ per-day event attendance), perk levels, role mappings, audit log, and settings all have endpoints. Audit log is read-only; event days are read-only (derived from the event's date range); everything else supports the full verb set its scope allows.

Beyond plain CRUD, most of the browser UI's admin actions have an API equivalent too: generating a QR check-in token, publishing an event/meeting report to its Discord forum, converting an event to a meeting, toggling an attendance record's virtual flag, cloning perk tiers between years, and bulk-syncing perk Discord roles. A chapter-member role change into or out of `lead` also syncs the chapter's Discord lead role, same as the browser. The handful of things that stay browser/session-only are inherently tied to a specific user's session or an in-person scan (self check-in/out, the public QR check-in flow, and viewing your own profile) — an admin API key can already achieve the same end state directly (e.g. checking any member into any event/meeting).

### Docs

Interactive OpenAPI/Swagger documentation is served at `/static/api-docs.html` (self-hosted, no external CDN calls) — the raw spec is at `/static/openapi.yaml`.

### Example

```bash
curl -H "X-API-Key: $LUG_API_KEY" https://your-lug.example.com/api/v1/events

curl -X POST -H "X-API-Key: $LUG_API_KEY" -H "Content-Type: application/json" \
  -d '{"title":"Build Night","start_time":"2026-09-01T18:00:00","end_time":"2026-09-01T20:00:00"}' \
  https://your-lug.example.com/api/v1/events
```

## Testing

The project includes comprehensive tests across 28 test suites (8 unit + 20 integration):

```bash
# Build with tests
cmake -B build -S . -DBUILD_TESTS=ON
cmake --build build -j$(nproc)

# Run all tests in parallel
ctest --test-dir build --output-on-failure -j$(nproc)
```

| Suite | Coverage |
|-------|----------|
| test_utils | Database operations, migrations, data persistence |
| test_discord_content | Announcement content generation, ping suppression |
| test_repositories | CRUD for all repositories, pagination, search |
| test_services | Service layer logic, calendar generation |
| test_role_system | Role mappings, age range ranks, perk levels, member fields, per-field PII sharing, moderator role privilege tier |
| test_suppression | Suppress flags, notes persistence, attendance summaries |
| test_markdown | Markdown-to-HTML rendering (md4c) |
| test_discord_signature | Ed25519 signature verification for the Discord Interactions endpoint — valid/tampered/wrong-key/malformed-hex cases |
| test_integration_auth | Login, logout, session, dashboard, calendar |
| test_integration_auth2 | Login page states, OAuth callback errors, denied/failed login flows |
| test_integration_members | Member CRUD, PII visibility |
| test_integration_chapters | Chapter CRUD, leads, members |
| test_integration_coverage | Chapter detail/edit pages, member role/removal edge cases, permission boundaries |
| test_integration_meetings | Meeting CRUD, pagination, detail |
| test_integration_events | Event CRUD, status, detail |
| test_integration_attendance | Attendance check-in, virtual toggle, overview |
| test_integration_attendance2 | Attendance overview filtering/sorting/pagination, per-member detail view |
| test_integration_settings | Settings save, Discord API, sync, nicknames |
| test_integration_settings2 | Discord/nickname sync admin gating, regenerate-nicknames permissions |
| test_integration_permissions | Per-field PII sharing, role-based access, chapter permissions, verified member logic |
| test_integration_perks2 | Perk level CRUD by year, edit form permissions, deletion |
| test_integration_audit | Audit log entries for all action types, search, filtering, pagination |
| test_integration_checkin | QR check-in token generation, public page, search, select, manual entry, duplicates |
| test_integration_help | Role-aware help page rendering for each role, HTMX partial loads |
| test_integration_ui | Content validation, calendar output, badge rendering |
| test_integration_api | JSON API CRUD per entity; auth/scope enforcement, key revocation, and cross-auth isolation between sessions and API keys |
| test_integration_discord_matches | Discord member match review queue — admin/chapter-lead access, link/create-new resolution |
| test_integration_discord_interactions | Discord Interactions webhook — PING, signature rejection, role-gated button click, modal submit (link/create-new), idempotency on already-resolved matches |

CI runs all tests inside the Docker build — failing tests block image creation.

## Docker

Pre-built images are available on GitHub Container Registry:

```bash
docker pull ghcr.io/arklug/lug-manager:latest
```

Or run with docker compose:
```bash
cp .env.example .env
# Edit .env with your Discord credentials
docker compose up -d
```

See [DOCKER.md](DOCKER.md) for detailed deployment instructions.

## Project Structure

```
.
├── src/
│   ├── main.cpp                 # Entry point, server setup
│   ├── config/                  # Configuration (env vars, .env loading)
│   ├── middleware/               # Auth middleware (Discord session, API key)
│   ├── routes/                  # HTTP route handlers (15 route files)
│   │   └── api/                 # JSON API route handlers, one per entity (/api/v1/*)
│   ├── services/                # Business logic
│   ├── repositories/            # Database access layer
│   ├── models/                  # Data structs
│   ├── integrations/            # Discord, Google Calendar, iCal
│   ├── auth/                    # OAuth2 + session management
│   ├── utils/                   # Markdown renderer, crypto helpers (API key hashing)
│   ├── async/                   # Thread pool
│   ├── db/                      # SQLite abstraction + migrations
│   ├── templates/               # Mustache HTML templates
│   │   ├── checkin/             # Public QR check-in page
│   │   ├── members/             # Member list, forms, views
│   │   ├── meetings/            # Meeting list, forms, detail, attendance
│   │   ├── events/              # Event list, forms, detail
│   │   ├── chapters/            # Chapter list, detail, members
│   │   ├── attendance/          # Attendance overview, personal history
│   │   ├── settings/            # Admin settings, roles, perks, API keys
│   │   └── dashboard/           # Dashboard with member info, perk progress
│   └── static/                  # CSS, JS assets, OpenAPI spec + Swagger UI (vendored)
├── tests/                       # Google Test suites (7 unit + 18 integration)
│   ├── integration_test_base.hpp # Shared fixture for integration tests
│   └── test_integration_*.cpp   # Split integration tests by feature
├── sql/migrations/              # Auto-applied database migrations (39)
├── .github/workflows/           # CI/CD pipeline (single Docker build job)
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
└── README.md
```

## Troubleshooting

### Template not found
Ensure `LUG_TEMPLATES_DIR` points to the templates directory and the working directory is the project root.

### Discord OAuth fails
- Verify `DISCORD_CLIENT_ID` and `DISCORD_CLIENT_SECRET`
- Ensure redirect URI matches exactly
- Check that the bot has been added to the server

### Discord "Missing Permissions" (50013)
The bot's role must be **higher** in the Discord role hierarchy than any roles it manages.

### Google Calendar not creating events
- Verify the service account JSON file path is correct and readable
- Ensure the service account email has editor access on the target calendar
- Check the Calendar ID format (`abc123@group.calendar.google.com`)

### Database locked
SQLite WAL mode supports concurrent reads but only one writer.

### Duplicate controls on browser back/forward
HTMX history restoration is handled automatically — DataTables and TomSelect instances are cleaned up before page cache. If you see duplicates, hard-refresh the page.

### API returns 401 with a valid-looking key
Confirm the key hasn't been revoked (Settings > API Keys shows Active/Revoked status) and that it's sent as `X-API-Key` or `Authorization: Bearer <key>` — session cookies alone do not authenticate `/api/v1/*`.

### API returns 403 on an endpoint that should work
Check the key's scope against the endpoint's requirement (see [JSON API](#json-api) above) — `role-mappings` and `settings` require `admin` scope even for `GET`.

## License

This project is licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPL-3.0). You are free to fork, modify, and distribute this software, but any modifications — including those deployed as a network service — must be made available under the same license.

## Contributing

Pull requests welcome! Please ensure all tests pass (`ctest --test-dir build -j$(nproc)`) and code compiles cleanly with zero warnings.
