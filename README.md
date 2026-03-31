# LUG Manager

A modern web application for managing LEGO User Groups (LUGs). Built with **C++ (CrowCPP)**, **SQLite**, **Discord**, and **iCal** integration.

## Features

- **Member Management**: Track paid vs. non-paid members, manage dues
- **Meeting Management**: Schedule meetings, create Discord events automatically, sync to shared calendar
- **Event Management**: Organize LEGO events (showcases, swap meets, conventions) with Discord threads
- **Attendance Tracking**: Check-in system for meetings and events
- **Discord Integration**: OAuth2 authentication, automatic scheduled events, thread creation
- **Shared Calendar**: RFC 5545 iCal/webcal feed for Google Calendar, Outlook auto-sync
- **Responsive UI**: HTMX + Tailwind CSS (no build step, CDN-based)

## Technology Stack

- **Backend**: C++ with CrowCPP v1.2.0 (header-only HTTP server)
- **Database**: SQLite with WAL mode
- **Frontend**: HTMX + Tailwind CSS (CDN)
- **External APIs**: Discord OAuth2, Discord REST API
- **Dependencies**: asio, nlohmann/json, libcurl, OpenSSL

## Prerequisites

- CMake 3.24+
- C++17 compiler (GCC, Clang)
- libcurl, sqlite3, OpenSSL (development headers)
- Linux, macOS, or Windows (with WSL)

### On Ubuntu/Debian:
```bash
sudo apt-get install cmake g++ libcurl4-openssl-dev sqlite3 libsqlite3-dev libssl-dev
```

### On macOS:
```bash
brew install cmake curl sqlite openssl
```

## Setup

1. **Clone the repository**:
   ```bash
   git clone https://github.com/yourusername/LUG-Manager.git
   cd LUG-Manager
   ```

2. **Initialize the build directory**:
   ```bash
   cmake --preset=debug
   ```

3. **Build the project**:
   ```bash
   cmake --build build/debug
   ```

4. **Configure environment variables**:
   Copy `.env.example` to `.env` and fill in your values:
   ```bash
   cp .env.example .env
   ```

   Required variables:
   - `DISCORD_CLIENT_ID`: Your Discord app client ID
   - `DISCORD_CLIENT_SECRET`: Your Discord app client secret
   - `DISCORD_BOT_TOKEN`: Your Discord bot token
   - `DISCORD_GUILD_ID`: Your Discord server ID
   - `DISCORD_REDIRECT_URI`: OAuth2 callback URL (e.g., `http://localhost:8080/auth/callback`)

5. **Run the application**:
   ```bash
   ./build/debug/lug_manager
   ```

   The server listens on `http://localhost:8080` by default.

## Discord Setup

1. Create a Discord application at [discord.com/developers](https://discord.com/developers/applications)
2. Under **OAuth2 > General**:
   - Set Redirect URI to your callback URL (e.g., `http://localhost:8080/auth/callback`)
   - Copy Client ID and Client Secret
3. Under **Bot** (create a bot if needed):
   - Copy the token
   - Ensure these **scopes** are enabled: `identify`, `guilds`, `bot`
   - Required **permissions**: `create_events`, `manage_threads`
4. Add the bot to your Discord server using the OAuth2 > URL Generator

## Database

The application uses SQLite with:
- **WAL mode** for better concurrency
- **Foreign key constraints** enabled
- **Automatic migrations** from `sql/migrations/` directory

On first run, migrations are applied automatically. To reset the database:
```bash
rm lug.db
./build/debug/lug_manager
```

## Project Structure

```
.
├── src/
│   ├── main.cpp                 # Entry point, server configuration
│   ├── middleware/
│   │   └── AuthMiddleware.hpp   # Discord session authentication
│   ├── routes/                  # HTTP endpoint handlers
│   ├── db/                      # Database abstractions
│   ├── services/                # Business logic (Members, Meetings, Events, etc.)
│   ├── integrations/            # Discord API, Calendar generation
│   └── templates/               # Mustache HTML templates
├── sql/
│   └── migrations/              # Database schema migration files
├── CMakeLists.txt               # Build configuration
├── CMakePresets.json            # CMake presets
├── .env.example                 # Environment variable template
└── README.md                    # This file
```

## Calendar Subscription

Users can subscribe to the shared LUG calendar in their calendar app:

**Webcal URL**: `webcal://your-server/calendar.ics`

- **Google Calendar**: Settings → Add by URL
- **Outlook**: Add calendar → From internet
- Auto-syncs every 5 minutes (client-side polling)

## API Endpoints

### Authentication
- `GET /login` – Login page
- `GET /auth/login` – Redirect to Discord OAuth
- `GET /auth/callback?code=...` – OAuth callback
- `POST /auth/logout` – Logout

### Dashboard
- `GET /dashboard` – Main dashboard

### Members
- `GET /members` – List all members
- `POST /members` – Create member (admin only)
- `GET /members/<id>` – Get member details
- `PUT /members/<id>` – Update member (admin only)
- `DELETE /members/<id>` – Delete member (admin only)
- `POST /members/<id>/paid` – Set paid status

### Meetings
- `GET /meetings` – List upcoming meetings
- `GET /meetings/<id>` – Meeting details
- `POST /meetings` – Create meeting (admin only)
- `PUT /meetings/<id>` – Update meeting (admin only)
- `POST /meetings/<id>/cancel` – Cancel meeting
- `POST /meetings/<id>/complete` – Mark complete
- `POST /meetings/<id>/checkin` – Check in to meeting

### Events
- `GET /events` – List upcoming events
- `GET /events/<id>` – Event details
- `POST /events` – Create event (admin only)
- `PUT /events/<id>` – Update event (admin only)
- `POST /events/<id>/checkin` – Check in to event

### Calendar
- `GET /calendar.ics` – iCal feed (public, no auth required)

## Development

### Building in Debug Mode (default):
```bash
cmake --preset=debug
cmake --build build/debug
```

### Building in Release Mode:
```bash
cmake --preset=release
cmake --build build/release
```

### Running Tests:
Currently, tests are run manually. Automated test framework coming soon.

### Code Style:
- C++17 standard
- Prefer clarity over cleverness
- Use RAII for resource management
- Async Discord calls via ThreadPool

## Troubleshooting

### Template not found error
Ensure the templates directory path is correctly configured in `.env` and the working directory is the project root when running the binary.

### Discord OAuth fails
- Verify `DISCORD_CLIENT_ID` and `DISCORD_CLIENT_SECRET` are correct
- Ensure redirect URI matches Discord app settings exactly
- Check your Discord server ID in `DISCORD_GUILD_ID`

### Database locked error
The application uses SQLite WAL mode. Ensure no other processes have the database open.

## Docker

Build and run with Docker:

```bash
docker build -t lug-manager .
docker run -p 8080:8080 --env-file .env lug-manager
```

See `.github/workflows/docker.yml` for automated Docker builds on GitHub.

## License

[Choose your license here]

## Contributing

Pull requests welcome! Please ensure code compiles without warnings and follows the existing style.

## Support

For issues, questions, or suggestions, please open a GitHub issue.
