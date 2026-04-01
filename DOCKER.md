# Docker Guide

## Quick Start with Pre-built Image

The easiest way to run LUG Manager — pull directly from GitHub Container Registry:

```bash
# Create your .env file
cp .env.example .env
# Edit .env with your Discord credentials

# Run with docker compose
docker compose up -d
```

The image is automatically built and pushed to `ghcr.io/arklug/lug-manager` on every push to `main`.

## Pull the Image

```bash
docker pull ghcr.io/arklug/lug-manager:latest
```

Available tags:
- `latest` — latest build from main branch
- `main` — same as latest
- `v1.0.0`, `v1.0` — semver release tags
- `<sha>` — specific commit builds

## Using docker compose (Recommended)

1. **Configure environment**:
   ```bash
   cp .env.example .env
   # Edit .env — only secrets are required:
   #   DISCORD_BOT_TOKEN
   #   DISCORD_CLIENT_ID
   #   DISCORD_CLIENT_SECRET
   #   DISCORD_REDIRECT_URI
   #   BOOTSTRAP_ADMIN_DISCORD_ID (for first-time setup)
   ```

2. **Start**:
   ```bash
   docker compose up -d
   ```

3. **Access**: Open `http://localhost:8080` and log in with Discord.

4. **View logs**:
   ```bash
   docker compose logs -f
   ```

5. **Stop**:
   ```bash
   docker compose down
   ```

Data is persisted in a Docker volume (`lug-data`). The database, migrations, and all settings survive container restarts and upgrades.

## Using Docker Directly

```bash
docker run -d \
  --name lug-manager \
  -p 8080:8080 \
  -v lug-data:/app/data \
  --env-file .env \
  ghcr.io/arklug/lug-manager:latest
```

## Environment Variables

Only secrets need to be in `.env` — everything else is configured from the Settings page after first login.

| Variable | Required | Description |
|----------|----------|-------------|
| `DISCORD_BOT_TOKEN` | Yes | Discord bot token |
| `DISCORD_CLIENT_ID` | Yes | Discord OAuth2 client ID |
| `DISCORD_CLIENT_SECRET` | Yes | Discord OAuth2 client secret |
| `DISCORD_REDIRECT_URI` | Yes | OAuth2 callback URL (e.g. `https://lug.example.com/auth/callback`) |
| `BOOTSTRAP_ADMIN_DISCORD_ID` | First run | Your Discord user ID — auto-creates admin account |
| `LUG_PORT` | No | Server port (default: `8080`) |
| `LUG_DB_PATH` | No | Database path (default: `/app/data/lug.db`) |
| `LUG_TEMPLATES_DIR` | No | Templates path (default: `/app/src/templates`) |

## Data Persistence

The container uses `/app/data` as its data directory (SQLite database). This is declared as a Docker `VOLUME`.

- **docker compose**: uses a named volume `lug-data`
- **docker run**: mount a volume or bind mount to `/app/data`

### Backup the database:
```bash
# docker compose
docker compose exec lug-manager sqlite3 /app/data/lug.db ".backup '/app/data/backup.db'"
docker compose cp lug-manager:/app/data/backup.db ./backup.db

# docker run
docker cp lug-manager:/app/data/lug.db ./lug-backup.db
```

## Google Calendar Integration

If using Google Calendar, mount the service account JSON file into the container:

```bash
docker run -d \
  --name lug-manager \
  -p 8080:8080 \
  -v lug-data:/app/data \
  -v /path/to/service-account.json:/app/data/service-account.json:ro \
  --env-file .env \
  ghcr.io/arklug/lug-manager:latest
```

Or with docker compose, add to the volumes:
```yaml
volumes:
  - lug-data:/app/data
  - /path/to/service-account.json:/app/data/service-account.json:ro
```

Then in the Settings page, set the Service Account JSON Path to `/app/data/service-account.json`.

## Building Locally

```bash
docker build -t lug-manager .
docker run -p 8080:8080 -v lug-data:/app/data --env-file .env lug-manager
```

## Production Deployment

### Reverse Proxy (nginx)

```nginx
server {
    listen 443 ssl;
    server_name lug.example.com;

    ssl_certificate     /etc/letsencrypt/live/lug.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/lug.example.com/privkey.pem;

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

### Kubernetes

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: lug-manager
spec:
  replicas: 1
  selector:
    matchLabels:
      app: lug-manager
  template:
    metadata:
      labels:
        app: lug-manager
    spec:
      containers:
      - name: lug-manager
        image: ghcr.io/arklug/lug-manager:latest
        ports:
        - containerPort: 8080
        env:
        - name: DISCORD_CLIENT_ID
          valueFrom:
            secretKeyRef:
              name: discord-secrets
              key: client_id
        - name: DISCORD_CLIENT_SECRET
          valueFrom:
            secretKeyRef:
              name: discord-secrets
              key: client_secret
        - name: DISCORD_BOT_TOKEN
          valueFrom:
            secretKeyRef:
              name: discord-secrets
              key: bot_token
        - name: DISCORD_REDIRECT_URI
          value: "https://lug.example.com/auth/callback"
        - name: BOOTSTRAP_ADMIN_DISCORD_ID
          value: ""
        volumeMounts:
        - name: data
          mountPath: /app/data
      volumes:
      - name: data
        persistentVolumeClaim:
          claimName: lug-manager-pvc
---
apiVersion: v1
kind: Service
metadata:
  name: lug-manager
spec:
  type: ClusterIP
  ports:
  - port: 80
    targetPort: 8080
  selector:
    app: lug-manager
```

## CI/CD

The GitHub Actions workflow (`.github/workflows/docker.yml`) automatically:
- Builds on push to `main`
- Builds on pull requests (without pushing)
- Pushes to `ghcr.io/arklug/lug-manager` with tags: `latest`, `main`, semver, and commit SHA
- Uses GitHub Actions cache for faster builds

No additional setup needed — it uses the built-in `GITHUB_TOKEN` for ghcr.io authentication.

## Troubleshooting

### Container won't start
```bash
docker compose logs lug-manager
```

### Database locked
Only one instance can write to SQLite at a time. Don't run multiple containers pointing at the same database file.

### Discord OAuth fails
Make sure `DISCORD_REDIRECT_URI` matches exactly what's configured in your Discord application settings (including `http` vs `https`).

### Templates not found
The `LUG_TEMPLATES_DIR` should be `/app/src/templates` (the default). Don't change it unless you're mounting custom templates.
