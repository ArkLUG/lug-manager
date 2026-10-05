// Every form field in the templates has a label: wrapped in a <label>, a
// <label for=...> pointing at its id, or an aria-label. (A placeholder isn't a
// label: it disappears as soon as someone types.)
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

TEST(TemplateLabels, EveryFieldHasALabel) {
    const std::regex control(R"re(<(input|select|textarea)\b([^>]*)>)re");
    const std::regex skip_type(R"re(type="(hidden|submit|button|reset|image)")re");
    const std::regex for_attr(R"re(<label[^>]*\bfor="([^"]+)")re");
    const std::regex id_attr(R"re(\bid="([^"]+)")re");
    std::vector<std::string> missing;
    int checked = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator("src/templates")) {
        if (e.path().extension() != ".html") continue;
        std::ifstream in(e.path());
        std::stringstream ss; ss << in.rdbuf();
        const std::string s = ss.str();
        std::set<std::string> label_for;
        for (std::sregex_iterator it(s.begin(), s.end(), for_attr), end; it != end; ++it) label_for.insert((*it)[1]);
        for (std::sregex_iterator it(s.begin(), s.end(), control), end; it != end; ++it) {
            const std::string attrs = (*it)[2];
            if (std::regex_search(attrs, skip_type)) continue;
            ++checked;
            if (attrs.find("aria-label") != std::string::npos) continue;
            std::smatch id;
            if (std::regex_search(attrs, id, id_attr) && label_for.count(id[1])) continue;
            const size_t pos = static_cast<size_t>(it->position());
            const size_t open = s.rfind("<label", pos), close = s.rfind("</label>", pos);
            if (open != std::string::npos && (close == std::string::npos || open > close)) continue;   // inside a <label>
            const auto line = std::count(s.begin(), s.begin() + static_cast<long>(pos), '\n') + 1;
            missing.push_back(e.path().string() + ":" + std::to_string(line) + " " + std::string((*it)[0]).substr(0, 80));
        }
    }
    EXPECT_GT(checked, 200);
    std::string list;
    for (const auto& m : missing) list += "\n  " + m;
    EXPECT_TRUE(missing.empty()) << missing.size() << " form fields without a label:" << list;
}
