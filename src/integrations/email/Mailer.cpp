#include "utils/Offline.hpp"
#include "integrations/email/Mailer.hpp"
#include "auth/SessionStore.hpp"
#include <curl/curl.h>
#include <openssl/evp.h>
#include <cstdlib>
#include <functional>
#include <cstring>
#include <ctime>
#include <iostream>

namespace {

std::string env(const char* k) {
    const char* v = std::getenv(k);
    return v ? v : "";
}

// Header values must never carry CR/LF (header injection).
std::string one_line(std::string s) {
    for (auto& c : s) if (c == '\r' || c == '\n') c = ' ';
    return s;
}

bool ascii(const std::string& s) {
    for (unsigned char c : s) if (c >= 0x80) return false;
    return true;
}

std::string b64(const std::string& in) {
    std::string out(4 * ((in.size() + 2) / 3), '\0');
    int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&out[0]),
                            reinterpret_cast<const unsigned char*>(in.data()), static_cast<int>(in.size()));
    out.resize(static_cast<size_t>(n));
    return out;
}

// RFC 2047 for non-ASCII subjects.
std::string encode_header(const std::string& s) {
    return ascii(s) ? s : "=?UTF-8?B?" + b64(s) + "?=";
}

struct Upload { const std::string* data; size_t pos = 0; };

size_t read_cb(char* buf, size_t size, size_t n, void* user) {
    auto* u = static_cast<Upload*>(user);
    size_t left = u->data->size() - u->pos, want = size * n;
    size_t k = left < want ? left : want;
    std::memcpy(buf, u->data->data() + u->pos, k);
    u->pos += k;
    return k;
}

} // namespace

Mailer::Config Mailer::from_env() {
    return {env("LUG_SMTP_URL"), env("LUG_SMTP_USER"), env("LUG_SMTP_PASSWORD"), env("LUG_SMTP_FROM")};
}

Mailer::Mailer(Config cfg) : cfg_(std::move(cfg)) {
    if (enabled()) worker_ = std::make_unique<ThreadPool>(1);
}

void Mailer::reconfigure(Config cfg) {
    std::lock_guard<std::mutex> l(cfg_mu_);
    if (capture_) return;
    cfg_ = std::move(cfg);
    if (!cfg_.url.empty() && !cfg_.from.empty() && !worker_) worker_ = std::make_unique<ThreadPool>(1);
}

std::shared_ptr<Mailer> Mailer::capture() {
    auto m = std::make_shared<Mailer>(Config{});
    m->capture_ = true;
    m->cfg_.from = "LUG Manager <lug@example.test>";
    return m;
}

std::string Mailer::address_of(const std::string& from) {
    auto a = from.rfind('<'), b = from.rfind('>');
    return (a != std::string::npos && b != std::string::npos && b > a) ? from.substr(a + 1, b - a - 1) : from;
}

std::string Mailer::build(const Message& m, const std::string& date, const std::string& message_id) const {
    std::string h;
    h += "Date: " + date + "\r\n";
    h += "From: " + one_line(config().from) + "\r\n";
    h += "To: " + one_line(m.to) + "\r\n";
    h += "Subject: " + encode_header(one_line(m.subject)) + "\r\n";
    h += "Message-ID: " + message_id + "\r\n";
    h += "MIME-Version: 1.0\r\n";
    // With a calendar file: multipart/mixed, the text then the .ics
    const std::string boundary = "lugmgr-" + std::to_string(std::hash<std::string>{}(message_id + m.subject));
    if (m.ics.empty()) {
        h += "Content-Type: text/plain; charset=UTF-8\r\n";
        h += "Content-Transfer-Encoding: 8bit\r\n";
    } else {
        h += "Content-Type: multipart/mixed; boundary=\"" + boundary + "\"\r\n";
    }
    h += "Auto-Submitted: auto-generated\r\n";
    if (!m.unsubscribe_url.empty()) {
        h += "List-Unsubscribe: <" + one_line(m.unsubscribe_url) + ">\r\n";
        h += "List-Unsubscribe-Post: List-Unsubscribe=One-Click\r\n";
    }
    std::string body;
    for (size_t i = 0; i < m.body.size(); ++i) {     // normalise to CRLF
        char c = m.body[i];
        if (c == '\r') continue;
        if (c == '\n') body += "\r\n"; else body += c;
    }
    if (m.ics.empty()) return h + "\r\n" + body + "\r\n";
    std::string out = h + "\r\n";
    out += "--" + boundary + "\r\n";
    out += "Content-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n";
    out += body + "\r\n";
    out += "--" + boundary + "\r\n";
    out += "Content-Type: text/calendar; charset=UTF-8; method=PUBLISH; name=\"event.ics\"\r\n";
    out += "Content-Disposition: attachment; filename=\"event.ics\"\r\n";
    out += "Content-Transfer-Encoding: base64\r\n\r\n";
    const std::string enc = b64(m.ics);
    for (size_t i = 0; i < enc.size(); i += 76) out += enc.substr(i, 76) + "\r\n";
    out += "--" + boundary + "--\r\n";
    return out;
}

void Mailer::send(Message m) {
    if (!enabled() || m.to.empty() || m.to.find('@') == std::string::npos) return;
    if (capture_) {
        std::lock_guard<std::mutex> l(mu_);
        outbox_.push_back(std::move(m));
        return;
    }
    ThreadPool* worker = nullptr;
    { std::lock_guard<std::mutex> l(cfg_mu_); worker = worker_.get(); }
    if (!worker) return;
    worker->enqueue([this, m = std::move(m)] {
        try { send_now(m); } catch (const std::exception& e) { std::cerr << "[mailer] " << e.what() << "\n"; }
    });
}

std::vector<Mailer::Message> Mailer::outbox() const {
    std::lock_guard<std::mutex> l(mu_);
    return outbox_;
}

void Mailer::clear_outbox() {
    std::lock_guard<std::mutex> l(mu_);
    outbox_.clear();
}

bool Mailer::send_now(const Message& m) {
    char date[64];
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    std::strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S +0000", &tm);
    const Config cfg = config();
    std::string from_addr = address_of(cfg.from);
    std::string domain = from_addr.substr(from_addr.find('@') == std::string::npos ? 0 : from_addr.find('@') + 1);
    std::string payload = build(m, date, "<" + SessionStore::generate_token().substr(0, 24) + "@" + domain + ">");

    CURL* curl = curl_easy_init();
    if (!curl) return false;
    Upload up{&payload};
    struct curl_slist* rcpt = curl_slist_append(nullptr, ("<" + address_of(m.to) + ">").c_str());
    curl_easy_setopt(curl, CURLOPT_URL, cfg.url.c_str());
    if (!cfg.user.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, cfg.user.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg.password.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, ("<" + from_addr + ">").c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpt);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_cb);
    curl_easy_setopt(curl, CURLOPT_READDATA, &up);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    CURLcode rc = guarded_perform(curl);
    curl_slist_free_all(rcpt);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        std::cerr << "[mailer] send to " << address_of(m.to) << " failed: " << curl_easy_strerror(rc) << "\n";
        return false;
    }
    return true;
}
