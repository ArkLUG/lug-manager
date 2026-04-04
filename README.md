# LUG Manager

> **Notice:** This project was built entirely using AI (Claude Code by Anthropic). All code, documentation, templates, and configuration were generated through AI-assisted development.

A modern web application for managing LEGO User Groups (LUGs). Built with **C++ (CrowCPP)**, **SQLite**, **Discord**, **Google Calendar**, and **iCal** integration.

## Features

- **Member Management**: Track members with contact info, FOL status (KFOL/TFOL/AFOL), birthday, dues, and Discord role sync. Members without Discord accounts (e.g. KFOLs) are supported.
- **PII Controls**: Members can opt-in to share contact info (email, phone, address) with other members. Admins and chapter leads always see PII; regular members only see PII for opted-in members.
- **Role System**: Three global roles (admin, chapter_lead, member) mapped via Discord roles, plus chapter-level roles (lead, event_manager, member). Anyone in the Discord guild can log in.
- **Chapter System**: Organize members into chapters with leads, event managers, Discord channels, and role sync
- **Meeting & Event Management**: Schedule with Discord events, forum threads, announcements, Google Calendar sync, and per-event suppress flags for historical data entry
- **Notes & Reports**: Markdown notes on events/meetings with WYSIWYG editor (EasyMDE). Publish reports to Discord forum channels with attendance (virtual/in-person split for meetings, multi-day for events)
- **Attendance Tracking**: Admin/lead/event-manager managed attendance with virtual support, year-filtered overview with search/sort/pagination, per-member detail view, perk tier display, and hide-inactive toggle
- **Perk Levels**: Admin-defined attendance tiers per year with Discord role rewards, FOL requirements, and paid dues prerequisites. Clone tiers between years. Members see progress on dashboard.
- **Discord Integration**: OAuth2 login, scheduled events, forum threads, announcements, role sync, perk role assignment
- **Google Calendar Integration**: Push events directly to a shared Google Calendar via service account, import existing events
- **iCal Feed**: RFC 5545 calendar subscription for personal calendar apps
- **Responsive UI**: Mobile-friendly HTMX + Tailwind CSS interface with dynamic nav highlights, page title updates, and EasyMDE markdown editor

## Technology Stack

- **Backend**: C++20 with CrowCPP v1.2.0 (header-only HTTP server)
- **Database**: SQLite with WAL mode and automatic migrations
- **Frontend**: HTMX + Tailwind CSS + TomSelect + EasyMDE (CDN, no build step)
- **External APIs**: Discord OAuth2 + REST, Google Calendar API v3
- **Dependencies**: asio, nlohmann/json, md4c, libcurl, OpenSSL, Google Test
- **CI/CD**: GitHub Actions (test gate + Docker build/push)

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
   ctest --test-dir build --output-on-failure
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

4. **Run**:
   ```bash
   ./build/lug_manager
   ```
   Open `http://localhost:8080` and log in with Discord. The first user matching `BOOTSTRAP_ADMIN_DISCORD_ID` is automatically made admin. All other guild members can log in as regular members.

## Roles & Permissions

### Global Roles (mapped via Discord roles in Settings > Role Mappings)

| Role | Access |
|------|--------|
| **admin** | Full access to everything |
| **chapter_lead** | Can see PII, manage chapter members, assign event managers. Cannot add/remove leads or edit chapter/system settings. |
| **member** | View events, meetings, chapters, own attendance. Cannot see PII unless member opted in. |

### Chapter Roles (assigned per-chapter by admins/leads)

| Role | Access |
|------|--------|
| **lead** | Manage chapter members + assign event_manager + everything event_manager can do |
| **event_manager** | Create/edit/delete events and meetings in their chapter, take attendance |
| **member** | Basic chapter membership |

### PII Visibility

PII fields (email, phone, birthday, address) are visible based on:
- **Admin/chapter_lead**: always see all PII
- **Regular member**: only sees PII for members who have opted in (`pii_public` toggle in member edit form)
- **Phone/address**: only visible in admin-only member edit form (not in the members list)

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
- **Admin/lead/event-manager managed**: searchable multi-select to check in members
- **Virtual support**: meetings track in-person vs. virtual attendance
- **Overview**: year-filtered, paginated, searchable, sortable table with last-seen date and perk tier
- **Per-member detail**: expandable view of attended events/meetings for the selected year
- **Hide inactive**: toggle to hide members with no attendance and no dues

### Perk Levels
- Admin-defined tiers per calendar year (e.g. Bronze, Silver, Gold)
- Separate meeting and event attendance thresholds
- Optional paid dues and minimum FOL status requirements
- Discord role auto-assigned when a member qualifies
- Clone tiers between years for easy setup
- Members see progress on their dashboard

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
| Perk Levels | Attendance tiers with Discord role rewards (per year) |

## Testing

The project includes comprehensive tests across 8 test suites:

```bash
# Build with tests
cmake -B build -S . -DBUILD_TESTS=ON
cmake --build build -j$(nproc)

# Run all tests
ctest --test-dir build --output-on-failure
```

| Suite | Coverage |
|-------|----------|
| test_utils | Database operations, migrations, data persistence |
| test_discord_content | Announcement content generation, ping suppression |
| test_repositories | CRUD for all repositories, pagination, search |
| test_services | Service layer logic, calendar generation |
| test_role_system | Role mappings, FOL ranks, perk levels, member fields |
| test_suppression | Suppress flags, notes persistence, attendance summaries |
| test_markdown | Markdown-to-HTML rendering (md4c) |
| test_integration | Full HTTP stack: auth, permissions, CRUD, PII visibility |

CI runs all tests before Docker build — failing tests block deployment.

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
│   ├── middleware/               # Auth middleware (Discord session)
│   ├── routes/                  # HTTP route handlers
│   ├── services/                # Business logic
│   ├── repositories/            # Database access layer
│   ├── models/                  # Data structs
│   ├── integrations/            # Discord, Google Calendar, iCal
│   ├── auth/                    # OAuth2 + session management
│   ├── utils/                   # Markdown renderer
│   ├── async/                   # Thread pool
│   ├── db/                      # SQLite abstraction + migrations
│   ├── templates/               # Mustache HTML templates
│   └── static/                  # CSS, JS assets
├── tests/                       # Google Test suites
├── sql/migrations/              # Auto-applied database migrations
├── .github/workflows/           # CI/CD pipeline
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

## License

This project is licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPL-3.0). You are free to fork, modify, and distribute this software, but any modifications — including those deployed as a network service — must be made available under the same license.

## Contributing

Pull requests welcome! Please ensure all tests pass (`ctest --test-dir build`) and code compiles cleanly with `-Wall -Wextra`.
