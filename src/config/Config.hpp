#pragma once
#include <string>
#include <stdexcept>

struct Config {
    int         port                             = 8080;
    std::string db_path                          = "./lug.db";
    std::string templates_dir                    = "./src/templates";

    // Discord Bot (for events/posts)
    std::string discord_bot_token;
    std::string discord_guild_id;
    std::string discord_announcements_channel_id;

    // Discord OAuth2 App (for login)
    std::string discord_client_id;
    std::string discord_client_secret;
    std::string discord_redirect_uri;

    // iCal feed (defaults, overridden by settings page)
    std::string ical_timezone                    = "America/New_York";
    std::string ical_calendar_name               = "LUG Events";

    // Bootstrap: if set, this Discord user ID is auto-created as admin on first login
    std::string bootstrap_admin_discord_id;
};

Config load_config();
