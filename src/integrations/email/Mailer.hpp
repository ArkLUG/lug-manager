#pragma once
#include "async/ThreadPool.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Plain-text email over SMTP (libcurl). Configured from the environment:
//   LUG_SMTP_URL       e.g. smtps://smtp.example.com:465 or smtp://host:587 (STARTTLS required)
//   LUG_SMTP_USER / LUG_SMTP_PASSWORD
//   LUG_SMTP_FROM      e.g. "Arkansas LUG <lug@example.com>"
// Unconfigured = disabled (enabled() is false and send() does nothing).
// Sends happen on a private single worker so requests never wait on SMTP.
class Mailer {
public:
    struct Config { std::string url, user, password, from; };
    struct Message {
        std::string to, subject, body;
        std::string unsubscribe_url;   // adds List-Unsubscribe (+ one-click) headers when set
    };

    static Config from_env();
    explicit Mailer(Config cfg);
    // Test double: enabled, records messages instead of sending.
    static std::shared_ptr<Mailer> capture();

    bool enabled() const { return capture_ || (!cfg_.url.empty() && !cfg_.from.empty()); }
    void send(Message m);
    std::vector<Message> outbox() const;   // capture mode only
    void clear_outbox();

    // Exposed for tests: the RFC 5322 message libcurl uploads.
    std::string build(const Message& m, const std::string& date, const std::string& message_id) const;
    static std::string address_of(const std::string& from);   // "Name <a@b>" -> "a@b"

private:
    bool send_now(const Message& m);

    Config cfg_;
    bool capture_ = false;
    mutable std::mutex mu_;
    std::vector<Message> outbox_;
    std::unique_ptr<ThreadPool> worker_;
};
