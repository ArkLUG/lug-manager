#pragma once
#include <string>
#include <sstream>
#include <vector>

// Build a human-readable diff string from old/new field pairs.
// Usage: AuditDiff d; d.field("name", old.name, new.name); return d.str();
class AuditDiff {
public:
    void field(const std::string& name, const std::string& old_val, const std::string& new_val) {
        if (old_val != new_val) {
            changes_.push_back(name + ": \"" + old_val + "\" -> \"" + new_val + "\"");
        }
    }
    void field(const std::string& name, int old_val, int new_val) {
        if (old_val != new_val) {
            changes_.push_back(name + ": " + std::to_string(old_val) + " -> " + std::to_string(new_val));
        }
    }
    void field(const std::string& name, bool old_val, bool new_val) {
        if (old_val != new_val) {
            changes_.push_back(name + ": " + (old_val ? "true" : "false") + " -> " + (new_val ? "true" : "false"));
        }
    }
    void field(const std::string& name, int64_t old_val, int64_t new_val) {
        if (old_val != new_val) {
            changes_.push_back(name + ": " + std::to_string(old_val) + " -> " + std::to_string(new_val));
        }
    }

    bool has_changes() const { return !changes_.empty(); }
    int count() const { return static_cast<int>(changes_.size()); }

    std::string str() const {
        std::ostringstream oss;
        for (size_t i = 0; i < changes_.size(); ++i) {
            if (i > 0) oss << "; ";
            oss << changes_[i];
        }
        return oss.str();
    }

private:
    std::vector<std::string> changes_;
};
