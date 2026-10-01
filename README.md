# LUG Manager

**Run your LEGO® User Group in one place**: members, meetings, shows, check-ins, dues, volunteers, build challenges, inventory and money, with Discord (optional) and Google Calendar built in. Self-hosted, open source, and light enough for a small home server.

**Who it's for:**
- LEGO User Groups (LUGs), adult fan (AFOL) clubs, and other LEGO fan communities, big or small.
- Groups recognized in **LEGO Fan CoLab** (formerly the LEGO Ambassador Network) as a *Recognized LEGO Fan Community*. You can record your Community Ambassador and their history, keep a yearly Fan CoLab to-do list, and print or download the year's activity summary. Your reports show your recognition.

**[Try the demo →](https://arklug.github.io/lug-manager/)** It's a fictional "Brickton LUG". Switch between a member, a moderator and an admin from the yellow bar. Nothing you do there is saved.

> **Notice:** This project was built entirely using AI (Claude Code by Anthropic). All code, documentation, templates, and configuration were generated through AI-assisted development.

---

## What it does

**Members**
- Member records with age range (KFOL/TFOL/AFOL), contact details and per-field privacy. Each member decides who sees their email, phone, address, birthday and Discord name.
- Sign in with an email and password, with Discord, or with an emailed link. Two-factor codes from an authenticator app (with recovery codes), and an option to require them for organisers or everyone. Forgot-password emails and admin-made set-password links.
- With Discord: anyone in your server can sign in, and Discord roles can map to admin. Without Discord: add members with their email address and send them a set-password link.
- Dues: a payment history, automatic expiry, an "expiring soon" list, renewal reminders and bulk updates.
- Perk levels: yearly attendance tiers that hand out Discord roles automatically.
- Young-member consent: guardian contact, signed consent and photo release, flagged at check-in.
- My Account: notification choices, signed-in devices, download my data, delete my account.
- Merge duplicate members, for example when someone added by hand later signs in with Discord.

**Meetings and shows**
- Meetings (in person or Discord voice) and multi-day events, with recurring schedules ("2nd Tuesday at 7 PM").
- RSVPs with capacity and a waitlist that moves people up automatically.
- Volunteer shifts, display (MOC) table requests, and a visitor tap counter for public shows.
- QR self check-in, a venue kiosk screen, and attendance by the people running the event.
- Event photos, Markdown notes, and reports posted to a Discord forum.
- A printable per-event report, suited to LEGO Fan CoLab reporting (formerly the LEGO Ambassador Network).
- An annual report covering growth, retention, venues and a five-year trend.
- A LEGO Fan CoLab page: Community Ambassador history, a yearly to-do list with a dashboard reminder, and the year's activity summary as a printable page or CSV.

**Discord, calendars and the public**
- Discord (optional): announcements, forum threads, scheduled events, reminders, role sync and DMs, all kept in step as meetings and events change. Choose what's posted (per area, and per meeting or event), a quiet mode, an activity log of everything sent with retry for failures, and nickname options. Switch Discord off entirely and messages go by email.
- Your own wording for every message: Discord posts, messages to members and sign-in emails, with placeholders and a live preview (Settings > Message wording).
- Google Calendar sync, a public iCal feed, per-chapter feeds and a private personal feed.
- A public "upcoming shows" page you can embed on your website, with an "I plan to come" button.
- A public About page about your group, written in the app with a what-you-see-is-what-you-get editor (headings, lists, links, photos), with the past year's highlights.
- Email (optional SMTP) for members who aren't on Discord, with a no-login unsubscribe link in every message.
- An optional weekly digest: one Monday message per member covering their week.

**Running the LUG**
- Chapters for groups with sub-groups. Turn them off if you don't have any.
- Build challenges with entries and voting, then a winner announcement on Discord.
- Inventory: tables, baseplates and display cases.
  - Storage locations (the storage unit, the show trailer, someone's garage), each with a person who looks after it. Items can be split across several locations.
  - Check-outs come from a location and are returned to it, with due-date reminders.
  - A photo and a condition (good / worn / needs repair / broken) for each item.
  - A pack list on each event, with what to bring, where it's kept, and a printable checklist to tick off.
- Treasury: income, expenses, receipts and a yearly balance, with a treasurer role that isn't full admin.
- Feature toggles: switch off anything your LUG doesn't use. Its data is kept.
- First-run setup, an audit log of every change, CSV exports, daily backups (photos included) and a JSON API.
- Dark mode, phone-friendly pages, and installable as an app.

## Install

LUG Manager is a single C++ server with SQLite, shipped as a Docker image (`ghcr.io/arklug/lug-manager`). The one-line version, for any machine with Docker:

```bash
mkdir lug-manager && cd lug-manager
curl -O https://raw.githubusercontent.com/ArkLUG/lug-manager/main/docker-compose.yml
curl -o .env https://raw.githubusercontent.com/ArkLUG/lug-manager/main/.env.example
# edit .env: Discord app/bot credentials and LUG_PUBLIC_URL (see Configuration)
docker compose up -d
docker compose logs lug-manager | grep setup
```

Then:

1. **Create the first admin.** On a fresh install the log prints a one-time link, `/setup?token=…`. Open it and enter your name and Discord user ID (or an email address).
   - Alternative: set `BOOTSTRAP_ADMIN_DISCORD_ID`, and that Discord account becomes admin on its first sign-in.
2. **Sign in and follow the setup checklist.** A dashboard banner takes you to Settings > Setup, which covers:
   - your LUG's name and time zone
   - which features you use
   - your Discord server and announcements channel
   - which Discord roles are admins
   - optionally, your LEGO Fan CoLab recognition and Community Ambassador
3. **Put it behind HTTPS** with a reverse proxy, and set `LUG_PUBLIC_URL`. Cookies are marked secure when that URL is `https://`.

Data lives in the `/app/data` volume: the database, uploads and backups.

**Other ways to host it:** see [HOSTING.md](HOSTING.md) for:
- **DigitalOcean, AWS, Google Cloud, Azure, Linode or Hetzner**, with a one-paste setup file that includes automatic HTTPS
- **Unraid**, with a template included
- **Fly.io** (`fly.toml`)
- **Render** (one-click blueprint)
- **Railway**

[DOCKER.md](DOCKER.md) covers compose, nginx, Kubernetes and backups.

## Configuration

Secrets and start-up options are environment variables, read from the environment or from `.env`. Real environment variables always take precedence over `.env`. Everything else is configured in the app under **Settings**.

| Variable | Needed | What it's for |
|---|---|---|
| `DISCORD_CLIENT_ID`, `DISCORD_CLIENT_SECRET` | With Discord | Discord sign-in (OAuth2 app) |
| `DISCORD_REDIRECT_URI` | With Discord | `https://your-host/auth/callback` |
| `DISCORD_BOT_TOKEN` | With Discord | Bot for announcements, roles, DMs and member sync |
| `LUG_PUBLIC_URL` | Recommended | e.g. `https://lug.example.org`. Used for links in emails and redirects. |
| `BOOTSTRAP_ADMIN_DISCORD_ID` | Optional | Makes this Discord account admin on first sign-in. The `/setup` link works too. |
| `DISCORD_PUBLIC_KEY`, `DISCORD_APPLICATION_ID` | Optional | Lets duplicate-member matches be resolved from Discord buttons |
| `LUG_SMTP_URL`, `LUG_SMTP_USER`, `LUG_SMTP_PASSWORD`, `LUG_SMTP_FROM` | Optional | Email sign-in and email notifications for members without Discord |
| `LUG_PORT`, `LUG_DB_PATH`, `LUG_TEMPLATES_DIR` | Optional | Defaults `8080`, `./lug.db`, `./src/templates`. The Docker image sets these. |
| `ICAL_TIMEZONE`, `ICAL_CALENDAR_NAME`, `DISCORD_GUILD_ID` | Optional | Starting values; change them later in Settings |
| `LUG_OFFLINE` | Never in production | `1` blocks every outbound request (Discord, Google, email). Use it for testing and copies of real data. |
| `LUG_DOTENV` | Optional | `0` skips reading `.env` |

### Without Discord

Switch **Discord** off under Settings > Features (or untick it in the setup checklist). Then:
- Create the first admin from the `/setup` link with an email address and a password.
- Add members with their email addresses, and send each one a set-password link from their member page (Settings > Sign-in has the options, including required two-factor).
- Notifications go by email if `LUG_SMTP_*` is set.

### Discord

1. Create an application at [discord.com/developers/applications](https://discord.com/developers/applications).
2. **OAuth2:**
   - Add the redirect URI `https://your-host/auth/callback`.
   - Copy the Client ID and Client Secret.
3. **Bot:**
   - Create the bot and copy its token.
   - Turn on the **Server Members Intent**, which member sync needs.
4. **Invite the bot:**
   - Scopes: `bot`, `identify`, `guilds`.
   - Permissions: Manage Events, Create Public Threads, Send Messages, Manage Roles, Manage Channels.
5. In your server's role list, drag the bot's role **above** every role it should assign.
6. Optional: to resolve duplicate-member matches from Discord buttons, set `DISCORD_PUBLIC_KEY` and `DISCORD_APPLICATION_ID`. Then register `https://your-host/discord/interactions` as the Interactions Endpoint URL.

Members are synced from Discord every 6 hours. A Discord member who may already have a record (for example, a kid added by a parent) is held for review rather than duplicated.

**What gets posted** is set under Settings > Discord: for each new meeting (announcement, Discord event) and event (thread, announcement, chapter copy, Discord event, update notes), pings, DMs and challenge winners. Each meeting and event can leave parts out for itself. **Quiet mode** posts nothing new (it only edits and removes), for imports or maintenance. Every post, edit, deletion and failure is in **Activity log**, where failed posts can be retried. **Repair times** fixes Discord copies of times sent by older versions without notifying anyone.

### Google Calendar (optional)

1. Create a Google Cloud service account and download its JSON key.
2. Enable the Calendar API.
3. Share your calendar with the service account's email address as an editor.
4. Enter the key path and the Calendar ID under Settings > Google Calendar.

Private meetings and events still appear on the calendar, but only as "Private LUG Event", with no title, place or description.

### Email (optional)

Set the four `LUG_SMTP_*` variables and `LUG_PUBLIC_URL`. Then:
- **Email me a link** and **Forgot it?** work on the sign-in page (single use, short-lived, rate limited);
- members who can't get a Discord DM (no Discord, DMs off, or Discord switched off) get reminders, waitlist notices, digests and dues reminders by email.

Every email has a no-login unsubscribe link and one-click `List-Unsubscribe` headers.

## Roles

| Role | Can |
|---|---|
| **Member** | See meetings, events and members (contact details follow each member's privacy choices); RSVP, volunteer, enter challenges, add photos, manage their own account. |
| **Moderator** (and **Chapter Lead**) | Everything a member can, plus: add and edit members, manage dues, see all contact details, run meetings and events, review Discord matches. |
| **Admin** | Everything, including settings, roles, features, reports, deleting members and merging duplicates. |
| **Treasurer** (flag, any role) | The Treasury page and recording dues there. |

**How roles are assigned**
- Admin and Member can come from Discord role mappings. Moderator and Chapter Lead are set by hand.
- A role that came from Discord follows Discord: losing the Discord role removes it. A role set by hand is never lowered by sync.

**Chapter roles** (only when Chapters is on)
- A chapter's **leads** manage its members and event managers.
- **Event managers** run that chapter's meetings and events.
- **Event leads** manage one specific event.
- With chapters off, "Chapter Lead" is no longer offered. Moderator gives the same permissions.

**Privacy:** each member chooses, field by field, between *Don't share*, *Verified members* (members who have attended in person or paid dues) and *All members*. Moderators and admins always see everything.

## Features you can switch off

Settings > Features (admin). A switched-off feature disappears from the sidebar, pages and forms, its pages return 404, and its reminders stop. Its data is kept, and the JSON API isn't affected.

Features you can switch off:
- Discord (sign-in, sync, posts and DMs; messages then go by email)
- Chapters
- Membership dues
- Perk levels
- Event RSVPs
- Display requests
- Volunteer shifts
- Event photos
- Build challenges
- Inventory
- Treasury
- Recurring meetings
- Reports
- LEGO Fan CoLab
- Kiosk and visitor counter
- QR self check-in
- Calendar feeds
- Discord reports
- Young-member consent

These start off and are opt-in:
- Weekly digest
- Public shows page
- Public About page

## Backups and your data

- **Daily database snapshots** go to `data/backups`; the newest 14 are kept by default. Settings > Backups has "Back up now" and download links.
- **Uploads:** photos and receipts are copied to `data/backups/uploads` with each backup. You can also download them all as one `.zip`.
- **Members' own data:** every member can download it as JSON, or delete their account, from My Account.
- **Audit log:** records who did what, when, and from which IP address.

## JSON API

Full create/read/update/delete under `/api/v1/*`:
- **Covers:** members, events, meetings, chapters, attendance, perk levels, role mappings, settings, the audit log and Discord match review.
- **Keys:** admins create API keys under Settings > API Keys, each with a `read`, `write` or `admin` scope. Send the key as `X-API-Key: <key>` or `Authorization: Bearer <key>`. Only a hash of each key is stored.
- **Docs:** interactive docs at `/static/api-docs.html`, with the spec at `/static/openapi.yaml`.

```bash
curl -H "X-API-Key: $LUG_API_KEY" https://lug.example.org/api/v1/events
```

## Development

**Stack:**
- Server: C++20 and [CrowCPP](https://crowcpp.org), with SQLite in WAL mode and 64 automatic migrations.
- Pages: server-rendered Mustache templates with htmx and Tailwind CSS.
- JavaScript libraries: DataTables, Tom Select, EasyMDE, driver.js and QRCode.js, all bundled in the repo with no CDN.
- Security: a strict nonce-based Content-Security-Policy.
- Other libraries: libcurl, OpenSSL, zlib, nlohmann/json and md4c.

```bash
# Ubuntu/Debian: sudo apt-get install cmake g++ pkg-config libcurl4-openssl-dev libsqlite3-dev libssl-dev zlib1g-dev
# Arch:          sudo pacman -S cmake gcc pkgconf curl sqlite openssl zlib
# macOS:         brew install cmake curl sqlite openssl pkg-config
cmake -B build -S . -DBUILD_TESTS=ON
cmake --build build -j$(nproc)
bash scripts/build_css.sh                      # Tailwind -> src/static/tailwind.min.css
LUG_OFFLINE=1 ./build/lug_manager              # http://localhost:8080
```

### Tests

```bash
ctest --test-dir build --output-on-failure -j$(nproc)
```

There are 58 test suites, unit and integration. **Tests never contact real services:**
- The test fixture sets `LUG_OFFLINE=1`.
- Discord and Google Calendar run against local fakes that record every request. These are `tests/fake_discord.hpp` and `tests/fake_google.hpp`, built on `tests/fake_server.hpp`.
- Email uses a capturing mailer.

**Browser checks:** these need Firefox and Selenium. Point them at a running server with `LUG_BASE`.
- `tests/e2e/smoke.py <session-token> <screenshot-dir>` covers every page and pop-up, and fails on any CSP violation or JavaScript error.
- `tests/e2e/mobile.py` loads the main pages at phone size and fails if any page scrolls sideways.

**Safety rules for local runs:**
- Use `LUG_OFFLINE=1` for development and for any copy of real data.
- Don't start test servers from a folder whose `.env` holds real credentials. Real environment variables take precedence over `.env`, and `LUG_DOTENV=0` skips it.
- `LUG_DISCORD_BASE` and `LUG_GOOGLE_BASE` point the clients at a fake server, and are accepted only for loopback addresses.

### Demo site

The workflow `.github/workflows/demo.yml` builds the demo and publishes it to GitHub Pages:
- **When:** on every push to `main`, every Monday so the dates stay current, and on demand.
- **How:**
  - `scripts/demo/seed.py` creates the fictional LUG.
  - `scripts/demo/export.py` crawls a throwaway offline server once per role and saves its pages and panels as static files.
  - `scripts/demo/demo.js` maps the app's requests onto those files and blocks anything that would save.
- **Checking it:** `tests/e2e/demo_check.py` clicks through the published site.

To build the demo locally:

```bash
bash scripts/demo/build.sh build/lug_manager /tmp/demo-site /lug-manager
mkdir -p /tmp/site && ln -s /tmp/demo-site /tmp/site/lug-manager
python3 -m http.server 8000 --directory /tmp/site      # http://localhost:8000/lug-manager/
```

### Adding another chat service (Slack, Matrix, ...)

Discord is one `chat::Provider` (`src/chat/Provider.hpp`, implemented in `src/chat/DiscordProvider.hpp`). `chat::ChatHub` decides what to post and keeps it in step; a provider only knows how to talk to its service. To add one:

1. Implement `chat::Provider`: post/edit/delete messages, threads (or report `caps().threads = false`), scheduled events (or `false`), direct messages, where things go (`place()`, chapter channels), and mention syntax. `inert()` must make member-supplied text unable to ping anyone.
2. Register it next to Discord in `register_all_routes` (`svc.chat->add(...)`), and add a `Features` entry and a settings page for its channels.
3. Its posts are recorded in `chat_posts`, its switches are `chat.<id>.<name>` settings, and the message templates are shared. Test it like `tests/test_chat_hub.cpp` does with a recording provider.

### Layout

```
src/
  main.cpp            start-up, background jobs (sync, reminders, digest, backups)
  routes/             HTTP handlers (one file per area; api/ = /api/v1)
  services/           business logic (events, sync, reminders, digest, features, notifier...)
  repositories/       SQLite access
  integrations/       Discord, Discord OAuth, Google Calendar, iCal, SMTP mailer
  middleware/         auth, CSP and security headers, feature gating
  templates/          Mustache pages and partials
  static/             CSS/JS (vendored), app.js behaviours, OpenAPI docs
sql/migrations/       applied automatically at start-up
tests/                unit + integration suites, fakes, e2e browser checks
scripts/              Tailwind build, theme CSS generator, demo site
cmake/                build helpers (Crow template patch)
unraid/               Unraid Community Applications template
deploy/               one-paste cloud-init setup for VPS providers
fly.toml, render.yaml cloud hosting configs (see HOSTING.md)
```

## Troubleshooting

- **"Missing Permissions" (50013) from Discord:** move the bot's role above the roles it manages.
- **Sign-in fails:** check that `DISCORD_REDIRECT_URI` matches the redirect in the Developer Portal exactly, and that the person is in your Discord server.
- **Nothing shows on Google Calendar:** check that the key file path is readable, that the service account has editor access, and that the Calendar ID looks like `…@group.calendar.google.com`.
- **Templates not found:** run from the project folder, or set `LUG_TEMPLATES_DIR`.
- **API returns 401 or 403:** the key may be revoked, or it may lack the right scope. Role mappings and settings need `admin` scope, even to read.

## License

[GNU Affero General Public License v3.0](LICENSE). You may fork, modify and share LUG Manager, but changes you run as a network service must be shared under the same license.

## Contributing

Pull requests are welcome. Please make sure `ctest` passes, the code builds without warnings, and new features come with tests.

*LEGO® is a trademark of the LEGO Group, which does not sponsor, authorize or endorse this project. LUG Manager is an independent, fan-made tool and is not part of LEGO Fan CoLab.*
