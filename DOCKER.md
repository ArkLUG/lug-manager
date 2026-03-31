# Docker Guide

## Quick Start

### Using docker-compose (Recommended)

1. **Set up environment variables**:
   ```bash
   cp .env.example .env
   # Edit .env with your Discord credentials
   ```

2. **Build and run**:
   ```bash
   docker-compose up --build
   ```

   The application will be available at `http://localhost:8080`

3. **Access the application**:
   - Open your browser and navigate to `http://localhost:8080`
   - Login with Discord OAuth

### Using Docker directly

1. **Build the image**:
   ```bash
   docker build -t lug-manager .
   ```

2. **Run the container**:
   ```bash
   docker run -p 8080:8080 \
     --env-file .env \
     -v $(pwd)/lug.db:/app/lug.db \
     lug-manager
   ```

## Building for Different Architectures

### Multi-platform build (arm64, amd64):
```bash
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  -t lug-manager:latest \
  .
```

## Pushing to a Registry

### Docker Hub
```bash
docker tag lug-manager:latest yourusername/lug-manager:latest
docker push yourusername/lug-manager:latest
```

### GitHub Container Registry (ghcr.io)
```bash
docker tag lug-manager:latest ghcr.io/yourusername/lug-manager:latest
docker push ghcr.io/yourusername/lug-manager:latest
```

## Production Deployment

### Environment Setup
Make sure to set these environment variables in your production environment:

```bash
DISCORD_CLIENT_ID=your_client_id
DISCORD_CLIENT_SECRET=your_client_secret
DISCORD_BOT_TOKEN=your_bot_token
DISCORD_GUILD_ID=your_guild_id
DISCORD_REDIRECT_URI=https://your-domain.com/auth/callback
DATABASE_PATH=/app/lug.db
TEMPLATES_DIR=/app/src/templates
```

### Kubernetes Deployment Example

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
        image: ghcr.io/yourusername/lug-manager:latest
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
        - name: DISCORD_GUILD_ID
          value: "your_guild_id"
        - name: DISCORD_REDIRECT_URI
          value: "https://your-domain.com/auth/callback"
        volumeMounts:
        - name: database
          mountPath: /app/lug.db
      volumes:
      - name: database
        persistentVolumeClaim:
          claimName: lug-manager-pvc
---
apiVersion: v1
kind: Service
metadata:
  name: lug-manager-service
spec:
  type: LoadBalancer
  ports:
  - port: 80
    targetPort: 8080
  selector:
    app: lug-manager
```

## Volume Management

### Persist the database across container restarts:
```bash
docker volume create lug-manager-db

docker run -p 8080:8080 \
  --env-file .env \
  -v lug-manager-db:/app/lug.db \
  lug-manager
```

## Troubleshooting

### Container exits immediately
Check logs:
```bash
docker logs <container_id>
```

### Port already in use
```bash
# Use a different port
docker run -p 9000:8080 lug-manager

# Or find what's using port 8080
lsof -i :8080
```

### Database connection issues
Ensure the database file has proper permissions:
```bash
chmod 666 lug.db
```

### Template not found
Verify that templates are being copied correctly in the Dockerfile and the `TEMPLATES_DIR` environment variable matches.

## GitHub Actions CI/CD

The `.github/workflows/docker.yml` workflow automatically:
- Builds on push to `main` and `develop` branches
- Builds on pull requests (without pushing)
- Pushes images on tags (v*)
- Tags images with branch name, version, and SHA

No additional setup needed - it uses GitHub Container Registry (ghcr.io) by default with built-in GitHub token authentication.
