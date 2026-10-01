#pragma once
// A local stand-in for Google's OAuth token endpoint and Calendar API v3.
// Constructing one points the app at it (LUG_GOOGLE_BASE) and writes a
// throwaway service-account file (fresh RSA key, token_uri -> the fake), so
// GoogleCalendarClient can be configured without real credentials.
#include "fake_server.hpp"
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <filesystem>
#include <fstream>
#include <map>

class FakeGoogle : public FakeServer {
public:
    using json = nlohmann::json;
    std::map<std::string, json> events;   // by id
    std::string service_account_path;

    explicit FakeGoogle(const std::string& dir) {
        start();
        setenv("LUG_GOOGLE_BASE", base_url().c_str(), 1);
        service_account_path = dir + "/fake-service-account.json";
        json sa = {{"type", "service_account"}, {"client_email", "lug-test@fake.iam.example"},
                   {"private_key", make_key()}, {"token_uri", base_url() + "/token"}};
        std::ofstream(service_account_path) << sa.dump();
    }
    ~FakeGoogle() override {
        stop();
        unsetenv("LUG_GOOGLE_BASE");
        std::error_code ec;
        std::filesystem::remove(service_account_path, ec);
    }

protected:
    crow::response handle(const crow::request& req) override {
        std::string path = req.url, method = crow::method_name(req.method);
        std::lock_guard<std::mutex> l(mu_);
        if (method == "POST" && path == "/token")
            return json_reply(200, json{{"access_token", "fake-google-token"}, {"expires_in", 3600}}.dump());
        std::smatch m;
        if (std::regex_match(path, m, std::regex("/calendar/v3/calendars/[^/]+/events"))) {
            if (method == "POST") {
                auto b = json::parse(req.body, nullptr, false);
                std::string id = "gcal" + std::to_string(++seq_);
                if (b.is_object()) { b["id"] = id; events[id] = b; }
                return json_reply(200, json{{"id", id}}.dump());
            }
            if (method == "GET") {
                json items = json::array();
                for (const auto& [id, e] : events) items.push_back(e);
                return json_reply(200, json{{"items", items}}.dump());
            }
        }
        if (std::regex_match(path, m, std::regex("/calendar/v3/calendars/[^/]+/events/([^/]+)"))) {
            std::string id = m[1];
            if (method == "PUT") {
                auto b = json::parse(req.body, nullptr, false);
                if (b.is_object()) { b["id"] = id; events[id] = b; }
                return json_reply(200, json{{"id", id}}.dump());
            }
            if (method == "DELETE") { events.erase(id); return crow::response(204); }
        }
        return json_reply(404, R"({"error":{"code":404}})");
    }

private:
    static std::string make_key() {
        EVP_PKEY* k = EVP_RSA_gen(2048);
        BIO* b = BIO_new(BIO_s_mem());
        PEM_write_bio_PrivateKey(b, k, nullptr, nullptr, 0, nullptr, nullptr);
        char* data = nullptr;
        long n = BIO_get_mem_data(b, &data);
        std::string pem(data, static_cast<size_t>(n));
        BIO_free(b);
        EVP_PKEY_free(k);
        return pem;
    }
    std::mutex mu_;
    int seq_ = 0;
};
