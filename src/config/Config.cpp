#include "config/Config.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>

static std::string getenv_or(const char* key, const std::string& def) {
    const char* val = std::getenv(key);
    return val ? std::string(val) : def;
}

// Fills in settings from ./.env for local development. Variables already set
// in the environment win (so a test/CI/rehearsal run can't silently pick up
// real credentials from a developer's .env), and LUG_DOTENV=0 skips the file.
static void load_dotenv(const std::string& path = ".env") {
    const char* off = std::getenv("LUG_DOTENV");
    if (off && std::string(off) == "0") return;
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::cerr << "[config] Loading " << path << " from the working directory (LUG_DOTENV=0 to skip)\n";
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // Remove surrounding quotes if present
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);
        setenv(key.c_str(), val.c_str(), 0); // real environment wins over .env
    }
}

Config load_config() {
    load_dotenv();
    Config cfg;
    cfg.port                             = std::stoi(getenv_or("LUG_PORT", "8080"));
    cfg.db_path                          = getenv_or("LUG_DB_PATH", "./lug.db");
    cfg.templates_dir                    = getenv_or("LUG_TEMPLATES_DIR", "./src/templates");

    cfg.discord_bot_token                = getenv_or("DISCORD_BOT_TOKEN", "");
    cfg.discord_guild_id                 = getenv_or("DISCORD_GUILD_ID", "");
    cfg.discord_announcements_channel_id = getenv_or("DISCORD_ANNOUNCEMENTS_CHANNEL_ID", "");

    cfg.discord_client_id                = getenv_or("DISCORD_CLIENT_ID", "");
    cfg.discord_client_secret            = getenv_or("DISCORD_CLIENT_SECRET", "");
    cfg.discord_redirect_uri             = getenv_or("DISCORD_REDIRECT_URI", "http://localhost:8080/auth/callback");
    cfg.public_url                       = getenv_or("LUG_PUBLIC_URL", "");
    while (!cfg.public_url.empty() && cfg.public_url.back() == '/') cfg.public_url.pop_back();

    cfg.discord_public_key               = getenv_or("DISCORD_PUBLIC_KEY", "");
    cfg.discord_application_id           = getenv_or("DISCORD_APPLICATION_ID", "");

    cfg.ical_timezone                    = getenv_or("ICAL_TIMEZONE", "America/New_York");
    cfg.ical_calendar_name               = getenv_or("ICAL_CALENDAR_NAME", "LUG Events");

    cfg.bootstrap_admin_discord_id       = getenv_or("BOOTSTRAP_ADMIN_DISCORD_ID", "");

    return cfg;
}
