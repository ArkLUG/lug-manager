# LUG Manager

> **Notice:** This project was built entirely using AI (Claude Code by Anthropic). All code, documentation, templates, and configuration were generated through AI-assisted development.

A modern web application for managing LEGO User Groups (LUGs). Built with **C++ (CrowCPP)**, **SQLite**, **Discord**, **Google Calendar**, and **iCal** integration.

## Features

- **Member Management**: Track paid vs. non-paid members, manage dues, Discord role sync
- **Chapter System**: Organize members into chapters with their own leads, channels, and roles
- **Meeting Management**: Schedule meetings, create Discord events, sync to Google Calendar
- **Event Management**: Organize events with Discord threads, forum posts, announcements, and convert to meetings
- **Attendance Tracking**: Admin/event lead managed attendance with virtual attendance support for meetings
- **Google Calendar Import**: Pull existing events from a shared Google Calendar into LUG Manager
- **Discord Integration**: OAuth2 login, automatic scheduled events, forum threads, announcement messages, role sync
- **Google Calendar Integration**: Push events directly to a shared Google Calendar via service account
- **iCal Feed**: RFC 5545 calendar subscription for individual members (Google Calendar, Outlook, Apple Calendar)
- **Responsive UI**: Mobile-friendly HTMX + Tailwind CSS interface with search and pagination

## Technology Stack

- **Backend**: C++20 with CrowCPP v1.2.0 (header-only HTTP server)
- **Database**: SQLite with WAL mode and automatic migrations
- **Frontend**: HTMX + Tailwind CSS (CDN, no build step)
- **External APIs**: Discord OAuth2 + REST, Google Calendar API v3
- **Dependencies**: asio, nlohmann/json, libcurl, OpenSSL

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

2. **Configure**:
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

3. **Run**:
   ```bash
   ./build/lug_manager
   ```
   Open `http://localhost:8080` and log in with Discord. The first user matching `BOOTSTRAP_ADMIN_DISCORD_ID` is automatically made admin.

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

5. **Important**: In your Discord server's role settings, drag the bot's role **above** any roles it needs to manage (chapter lead roles, etc.). Discord requires the bot role to be higher in the hierarchy.

6. After first login, go to **Settings** in the web UI to configure:
   - Guild ID (Discord server ID)
   - Announcements channel
   - Event forum channel
   - Announcement roles
   - Timezone

## Google Calendar Integration

LUG Manager can push meetings and events directly to a shared Google Calendar, so they appear instantly (unlike iCal which polls every few hours).

### Setup

1. **Create a Google Cloud project**:
   - Go to [console.cloud.google.com](https://console.cloud.google.com)
   - Create a new project (or use an existing one)
   - Enable the **Google Calendar API**: APIs & Services > Library > search "Calendar" > Enable

2. **Create a service account**:
   - Go to APIs & Services > Credentials
   - Create Credentials > Service Account
   - Give it a name (e.g. "LUG Manager")
   - **Skip** the "Grant this service account access" step (no project roles needed — access is granted by sharing the calendar directly)
   - Click Done, then click into the new service account
   - Keys tab > Add Key > Create new key > JSON
   - Download the JSON key file and place it on your server (e.g. `/etc/lug-manager/service-account.json`)
   - Note the `client_email` field in the JSON — you'll need it in the next step

3. **Create or choose a Google Calendar**:
   - In Google Calendar, create a new calendar for your LUG (or use an existing one)
   - Go to Calendar Settings > Share with specific people
   - Add the service account's email (found in the JSON file as `client_email`, looks like `name@project.iam.gserviceaccount.com`)
   - Set permission to **Make changes to events**
   - Copy the **Calendar ID** from the Calendar Settings page (looks like `abc123@group.calendar.google.com`)

4. **Configure in LUG Manager**:
   - Go to Settings in the web UI
   - Under "Google Calendar", enter:
     - **Service Account JSON Path**: absolute path to the key file on the server
     - **Google Calendar ID**: the calendar ID from step 3
   - Click Save Settings
   - The green "Connected" indicator confirms the integration is working

5. **Import existing events** (optional):
   - Once connected, click "Import Events from Google Calendar" to pull in upcoming events
   - Imported events appear as LUG-wide events and won't be re-imported on subsequent imports

### How it works

- When you **create** a meeting or event, it's automatically added to Google Calendar
- When you **edit** a meeting or event, the Google Calendar entry is updated
- When you **delete** a meeting or event, the Google Calendar entry is removed
- If Google Calendar is not configured, all Google Calendar operations are silently skipped

## Calendar Subscription (iCal)

Members can subscribe to the LUG calendar in their personal calendar app. The subscription URL is shown on the Dashboard after login.

- **URL**: `https://your-server/calendar.ics` (public, no auth required)
- **Google Calendar**: Other calendars (+) > From URL > paste the URL
- **Apple Calendar**: File > New Calendar Subscription > paste the URL
- **Outlook**: Add calendar > Subscribe from web > paste the URL

The feed includes all meetings and events with proper timezone handling (`DTSTART;TZID=...`) and updates within 5 minutes of any change.

## Chapters

Chapters allow you to organize your LUG into sub-groups (e.g. by city, theme, or age group). Each chapter can have:

- **Chapter leads** with a mapped Discord role (automatically synced)
- **Event managers** who can create events for their chapter
- **A Discord announcement channel** for chapter-specific announcements
- **Members** assigned via the member management page

Chapter leads and event managers can create meetings and events scoped to their chapter. Admins can create LUG-wide events.

## Attendance

Attendance is managed by admins, chapter leads, and event leads (for their events):

- **Add members**: Searchable multi-select dropdown to check in one or more members at once
- **Virtual attendance**: Meetings support marking attendees as virtual (in-person vs. remote)
- **Toggle**: Admins can switch between virtual and in-person for any attendee
- **Remove**: Remove attendees with confirmation
- **Counts**: Live-updating attendance counts on meeting and event cards

## Database

SQLite with WAL mode for concurrent reads. Migrations are applied automatically on startup from `sql/migrations/`.

To reset the database:
```bash
rm lug.db
./build/lug_manager
```

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
│   ├── models/                  # Data structs (Member, Meeting, LugEvent, etc.)
│   ├── integrations/            # Discord, Google Calendar, iCal
│   ├── auth/                    # OAuth2 service
│   ├── async/                   # Thread pool for async Discord calls
│   ├── db/                      # SQLite abstraction
│   ├── templates/               # Mustache HTML templates
│   └── static/                  # CSS, JS assets
├── sql/
│   └── migrations/              # Auto-applied database migrations
├── CMakeLists.txt
├── CMakePresets.json
├── Dockerfile
├── docker-compose.yml
├── .env.example
└── README.md
```

## Settings (Admin)

All runtime configuration is managed from the Settings page (`/settings`):

| Setting | Description |
|---------|-------------|
| Discord Guild ID | Your Discord server ID |
| Announcements Channel | Channel for LUG-wide event/meeting announcements |
| Event Forum Channel | Forum channel for event discussion threads |
| Event Announcement Role | Role @mentioned in announcements |
| Non-LUG Event Role | Role @mentioned for non-LUG events |
| Timezone | IANA timezone (e.g. `America/Chicago`) — used for Discord, Google Calendar, and iCal |
| Calendar Name | Display name in calendar apps |
| Suppress Pings | Disable all @mentions in Discord announcements |
| Google Calendar SA Path | Path to Google service account JSON key |
| Google Calendar ID | Target Google Calendar ID |

Settings are seeded from environment variables on first run, then managed exclusively from the web UI.

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

See [DOCKER.md](DOCKER.md) for detailed deployment instructions including Google Calendar service account mounting, reverse proxy setup, and Kubernetes examples.

## API Endpoints

### Authentication
- `GET /login` – Login page
- `GET /auth/login` – Redirect to Discord OAuth
- `GET /auth/callback` – OAuth callback
- `POST /auth/logout` – Logout

### Meetings
- `GET /meetings` – List meetings (paginated, searchable)
- `POST /meetings` – Create meeting
- `GET /meetings/<id>/edit` – Edit form
- `PUT /meetings/<id>` – Update meeting
- `POST /meetings/<id>/cancel` – Delete meeting
- `POST /meetings/<id>/discord-sync` – Force sync to Discord

### Events
- `GET /events` – List upcoming events (paginated, searchable)
- `GET /events/all` – List all events
- `POST /events` – Create event
- `GET /events/<id>/edit` – Edit form
- `PUT /events/<id>` – Update event
- `POST /events/<id>/cancel` – Delete event
- `POST /events/<id>/discord-sync` – Force sync to Discord
- `POST /events/<id>/status` – Change event status (open/closed)
- `POST /events/<id>/convert-to-meeting` – Convert event to meeting

### Attendance
- `GET /attendance` – Personal attendance history
- `GET /attendance/count/<type>/<id>` – Live count
- `GET /attendance/list/<type>/<id>` – Attendee list with admin controls
- `POST /attendance/admin/checkin` – Admin add members
- `POST /attendance/admin/<id>/remove` – Admin remove attendee
- `POST /attendance/admin/<id>/toggle-virtual` – Toggle virtual status

### Members
- `GET /members` – Member list (DataTables, searchable)
- `POST /members` – Create member
- `POST /members/<id>` – Update member
- `DELETE /members/<id>` – Delete member
- `POST /members/<id>/paid` – Set dues status

### Calendar
- `GET /calendar.ics` – iCal feed (public, no auth)

### Settings (Admin)
- `GET /settings` – Settings page
- `POST /settings` – Save settings
- `POST /api/google-calendar/import` – Import events from Google Calendar
- `POST /api/discord/sync-members` – Sync members from Discord
- `POST /api/discord/test-announcement` – Send test announcement

## Troubleshooting

### Template not found
Ensure `LUG_TEMPLATES_DIR` points to the templates directory and the working directory is the project root.

### Discord OAuth fails
- Verify `DISCORD_CLIENT_ID` and `DISCORD_CLIENT_SECRET`
- Ensure redirect URI matches exactly (including trailing slash or lack thereof)
- Check that the bot has been added to the server

### Discord "Missing Permissions" (50013)
The bot's role must be **higher** in the Discord role hierarchy than any roles it tries to manage. Drag it up in Server Settings > Roles.

### Google Calendar not creating events
- Verify the service account JSON file path is correct and readable by the app
- Ensure the service account email has been shared as an editor on the target calendar
- Check the Calendar ID (should look like `abc123@group.calendar.google.com`, not the calendar name)

### Database locked
SQLite WAL mode supports concurrent reads but only one writer. Ensure no other process has the database open.

## License

[Choose your license here]

## Contributing

Pull requests welcome! Please ensure code compiles cleanly with `-Wall -Wextra` and follows the existing patterns.
