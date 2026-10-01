# Hosting LUG Manager

LUG Manager is one small container (`ghcr.io/arklug/lug-manager`) plus one folder of data: the SQLite database, uploads and backups, mounted at `/app/data`. Anything that can run a container with a **persistent disk** can host it.

**Run exactly one copy.** The database is a single SQLite file, so don't scale it out to several instances.

Whichever host you pick, the steps afterwards are the same:

1. **Create the Discord application and bot.** See [README > Discord](README.md#discord).
   - Set its OAuth2 redirect to `https://<your address>/auth/callback`.
2. **Set the configuration:**
   - `DISCORD_CLIENT_ID`, `DISCORD_CLIENT_SECRET`, `DISCORD_BOT_TOKEN`
   - `DISCORD_REDIRECT_URI` (the same redirect address as above)
   - `LUG_PUBLIC_URL` (`https://<your address>`)
3. **Create the first admin.** Open the one-time `/setup?token=…` link from the container's log, or set `BOOTSTRAP_ADMIN_DISCORD_ID`.
4. **Sign in and follow the setup checklist.**

| Option | Good for | Cost (check current pricing) | Notes |
|---|---|---|---|
| [Home server with Docker](#docker--docker-compose) | You already run a NAS or home server | Free | Needs a reverse proxy with HTTPS |
| [Unraid](#unraid) | Unraid users | Free | Template included |
| [Fly.io](#flyio) | A small, always-on cloud box | A few dollars a month | `fly.toml` included, volume for data |
| [Render](#render) | Click-through setup | Paid instance plus disk | `render.yaml` blueprint, "Deploy to Render" |
| [Railway](#railway) | Click-through setup | Usage-based | Deploy the image and add a volume |
| [DigitalOcean, AWS, Google Cloud, Azure, Linode, Hetzner](#digitalocean-aws-google-cloud-azure-and-others-one-paste-setup) | A server you control | About $4-6/month; Google has a free tier | One-paste setup file with automatic HTTPS |

Free tiers that put idle apps to sleep, or that have no persistent disk, won't work. The app runs reminders, backups and Discord sync in the background, and keeps its data on disk.

## Docker / docker compose

```bash
mkdir lug-manager && cd lug-manager
curl -O https://raw.githubusercontent.com/ArkLUG/lug-manager/main/docker-compose.yml
curl -o .env https://raw.githubusercontent.com/ArkLUG/lug-manager/main/.env.example
# fill in .env
docker compose up -d
docker compose logs lug-manager | grep setup
```

See [DOCKER.md](DOCKER.md) for the nginx reverse-proxy config, Kubernetes and backups.

## Unraid

A ready-made template is in [`unraid/lug-manager.xml`](unraid/lug-manager.xml). To install it:

1. Open **Terminal** on your Unraid server and download the template into your user templates:
   ```bash
   wget -O /boot/config/plugins/dockerMan/templates-user/my-lug-manager.xml \
     https://raw.githubusercontent.com/ArkLUG/lug-manager/main/unraid/lug-manager.xml
   ```
2. Go to **Docker > Add Container**. Pick **lug-manager** from the *Template* list.
3. Fill in the Discord fields and the Public URL, then press **Apply**.
4. Open the container's log. It shows the one-time `/setup?token=…` link for creating the first admin.

The defaults:
- Data goes to `/mnt/user/appdata/lug-manager`.
- Files there belong to `nobody:users`, because `PUID`=99 and `PGID`=100 (under *Show more settings*).
- For HTTPS, put it behind your usual reverse proxy, such as Nginx Proxy Manager, SWAG or Traefik. Turn on websocket support for it (in Nginx Proxy Manager: *Websockets Support*), so pages update live; without it everything still works, pages just don't update by themselves.

## Fly.io

[`fly.toml`](fly.toml) is set up for one always-on machine with a 1 GB volume.

```bash
fly launch --copy-config --no-deploy          # choose an app name and region
fly volumes create lug_data --size 1
fly secrets set DISCORD_CLIENT_ID=... DISCORD_CLIENT_SECRET=... DISCORD_BOT_TOKEN=... \
  DISCORD_REDIRECT_URI=https://<app>.fly.dev/auth/callback LUG_PUBLIC_URL=https://<app>.fly.dev
fly deploy
fly logs | grep setup
```

Fly gives you HTTPS at `https://<app>.fly.dev`. You can add your own domain with `fly certs add`.

## Render

[![Deploy to Render](https://render.com/images/deploy-to-render-button.svg)](https://render.com/deploy?repo=https://github.com/ArkLUG/lug-manager)

The button reads [`render.yaml`](render.yaml):
- It creates one web service from the published image, with a 1 GB disk at `/app/data`.
- It asks you for the Discord settings and the public URL.

Persistent disks need a paid instance type. HTTPS is included at `https://<name>.onrender.com`.

## Railway

1. **New Project > Deploy a Docker Image.** Use the image `ghcr.io/arklug/lug-manager:latest`.
2. Open the service and **add a Volume** mounted at `/app/data`.
3. Under **Variables**, add:
   - `LUG_PORT=8080`
   - the Discord variables
   - `LUG_PUBLIC_URL`
4. Under **Settings > Networking**, generate a domain on port 8080, then use that domain for `LUG_PUBLIC_URL` and `DISCORD_REDIRECT_URI`.
5. Check the deploy logs for the `/setup` link.

## DigitalOcean, AWS, Google Cloud, Azure and others (one-paste setup)

On any of these, **use a small Ubuntu 24.04 virtual server with the one-paste setup file** [`deploy/cloud-init.yaml`](deploy/cloud-init.yaml).

When the server first starts, the file:
- installs Docker;
- starts LUG Manager behind [Caddy](https://caddyserver.com), which gets a free HTTPS certificate for your domain automatically;
- keeps your data in `/opt/lug-manager/data`;
- opens only ports 22, 80 and 443;
- checks for a new LUG Manager version every Sunday night.

**Steps:**

1. **Prepare the file.** Copy `deploy/cloud-init.yaml` and fill in the values under the `<-- CHANGE` notes:
   - your domain;
   - an email address for certificate notices;
   - your Discord client ID, client secret and bot token.
   - In the Discord Developer Portal, set the OAuth2 redirect to `https://<your domain>/auth/callback`.
2. **Create the server.** Choose Ubuntu 24.04 and the smallest size with at least 1 GB of memory. Paste the file where your provider asks for start-up settings (see the table).
3. **Point your domain at the server.** Add a DNS **A record** for your domain with the server's IP address.
4. **Create the first admin.** After a few minutes, open `https://<your domain>`. For the one-time setup link, connect to the server and run:
   ```bash
   sudo cloud-init status --wait          # finished?
   sudo docker compose -f /opt/lug-manager/docker-compose.yml logs lug-manager | grep setup
   ```

| Provider | Where to paste the file | Firewall |
|---|---|---|
| **DigitalOcean** Droplet | Create > Droplets > *Advanced options* > **Add initialization scripts** | Nothing extra |
| **AWS Lightsail** | Create instance > *OS only* > Ubuntu > **+ Add launch script** | Networking tab: add a rule for **HTTPS (443)** |
| **AWS EC2** | Launch instance > *Advanced details* > **User data** | Security group: allow HTTP and HTTPS |
| **Google Compute Engine** | Create instance > Advanced > *Management* > **Metadata**: key `user-data`, value = the file | Tick **Allow HTTP/HTTPS traffic** |
| **Microsoft Azure** VM | Create > *Advanced* > **Custom data** | Network settings: allow HTTP (80) and HTTPS (443) |
| **Linode / Akamai** | Create Linode > **Add User Data** (in regions that support it) | Nothing extra |
| **Hetzner Cloud** | Add server > **Cloud config** | Nothing extra |

**Free option:** Google Cloud's always-free tier includes one e2-micro server in some US regions (us-west1, us-central1 or us-east1, at the time of writing). It's small but runs LUG Manager for a typical LUG.

Providers rename their menus from time to time. Look for "user data", "cloud-init", "startup/launch script" or "custom data".

**These don't fit:**
- Heroku
- DigitalOcean App Platform
- Google Cloud Run
- AWS App Runner

They have no disk that survives restarts, or they stop the app when it's idle. LUG Manager keeps its database on disk and runs reminders and Discord sync in the background.

## Updating

- **Docker / compose:** `docker compose pull && docker compose up -d`.
- **Unraid:** Docker > *Check for updates*.
- **Fly:** `fly deploy`.
- **Render / Railway:** redeploy the service.

Database changes are applied automatically when the app starts. Take a backup first: Settings > Backups > *Back up now*.
