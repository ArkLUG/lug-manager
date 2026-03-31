#include "config/Config.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>

static std::string getenv_or(const char* key, const std::string& def) {
    const char* val = std::getenv(key);
    return val ? std::string(val) : def;
}

static void load_dotenv(const std::string& path = ".env") {
    std::ifstream f(path);
    if (!f.is_open()) return;
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
        setenv(key.c_str(), val.c_str(), 1); // .env overrides shell env
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

    cfg.ical_timezone                    = getenv_or("ICAL_TIMEZONE", "America/New_York");
    cfg.ical_calendar_name               = getenv_or("ICAL_CALENDAR_NAME", "LUG Events");

    cfg.bootstrap_admin_discord_id       = getenv_or("BOOTSTRAP_ADMIN_DISCORD_ID", "");

    return cfg;
}
