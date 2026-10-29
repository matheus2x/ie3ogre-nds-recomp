// Local achievements (ntr/achievements.h).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <system_error>

#include <ntr/achievements.h>
#include <ntr/common.h>

namespace ntr {

namespace fs = std::filesystem;

namespace {

bool read_text(const fs::path& path, std::string& out) {
    FILE* f = open_file(path, "rb");
    if (!f) return false;
    out.clear();
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    std::fclose(f);
    return true;
}

// Days since 1970-01-01 of a date in the proleptic Gregorian calendar.
int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

bool compare(double a, const std::string& op, double b, bool& ok) {
    if (op == ">=") ok = a >= b;
    else if (op == ">") ok = a > b;
    else if (op == "==") ok = a == b;
    else if (op == "!=") ok = a != b;
    else if (op == "<=") ok = a <= b;
    else if (op == "<") ok = a < b;
    else return false;
    return true;
}

std::string text_of(const Json& j, const char* language) {
    if (j.is_string()) return j.as_string();
    return j.get(language).as_string();
}

}  // namespace

GameFacts game_facts(const SaveSummary& save) {
    GameFacts f;
    f.saved = save.valid && !save.empty && !save.unknown;
    if (!f.saved) return f;
    f.chapter = save.chapter;
    f.level = save.level;
    f.members = save.members;
    f.play_frames = save.play_frames;
    f.units = save.units;
    return f;
}

const std::string& Achievement::name() const {
    return language() == Language::Portuguese && !name_pt.empty() ? name_pt : name_en;
}

const std::string& Achievement::description() const {
    return language() == Language::Portuguese && !description_pt.empty() ? description_pt : description_en;
}

bool achievement_condition(const Json& c, const GameFacts& facts, bool& ok, std::string& error) {
    if (!c.is_object()) {
        error = "a condition must be an object";
        return false;
    }
    for (const char* group : {"all", "any"}) {
        const Json& list = c.get(group);
        if (list.is_null()) continue;
        if (!list.is_array() || list.items().empty()) {
            error = std::string("\"") + group + "\" must be a list of conditions";
            return false;
        }
        bool all = group[1] == 'l';
        ok = all;
        for (const Json& item : list.items()) {
            bool one = false;
            if (!achievement_condition(item, facts, one, error)) return false;
            ok = all ? ok && one : ok || one;
        }
        return true;
    }
    const std::string& source = c.get("source").as_string();
    if (source != "save") {
        error = "unknown source \"" + source + "\" (only \"save\" so far)";
        return false;
    }
    const std::string& field = c.get("field").as_string();
    const Json& value = c.get("value");
    if (field == "saved") {
        ok = facts.saved;
        return true;
    }
    if (value.type() != Json::Type::Number) {
        error = "\"" + field + "\" needs a number in \"value\"";
        return false;
    }
    double want = value.as_number();
    if (field == "has_unit") {
        ok = facts.saved && std::find(facts.units.begin(), facts.units.end(), uint16_t(want)) != facts.units.end();
        return true;
    }
    double have;
    if (field == "chapter") have = facts.chapter;
    else if (field == "level") have = facts.level;
    else if (field == "members") have = facts.members;
    else if (field == "play_hours") have = facts.play_frames / (60.0 * 3600.0);
    else {
        error = "unknown field \"" + field + "\"";
        return false;
    }
    std::string op = c.get("op").is_null() ? ">=" : c.get("op").as_string();
    if (!compare(have, op, want, ok)) {
        error = "unknown op \"" + op + "\"";
        return false;
    }
    ok = ok && facts.saved;
    return true;
}

std::string local_timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    // The offset from UTC: the local fields read as if they were UTC, minus the real time.
    int64_t as_utc = days_from_civil(local.tm_year + 1900, unsigned(local.tm_mon + 1), unsigned(local.tm_mday)) * 86400 +
                     local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    int64_t offset = (as_utc - int64_t(t)) / 60;  // minutes
    char date[32], zone[16];
    std::strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &local);
    std::snprintf(zone, sizeof zone, "%c%02d:%02d", offset < 0 ? '-' : '+', int(std::abs(offset) / 60 % 100),
                  int(std::abs(offset) % 60));
    return std::string(date) + zone;
}

bool Achievements::load_definitions(const std::string& root, std::string& error) {
    root_ = root;
    list_.clear();
    fs::path path = fs_path(root.empty() ? "." : root) / "achievements" / "achievements.json";
    std::string text;
    if (!read_text(path, text)) {
        error = "cannot read " + utf8_string(path);
        return false;
    }
    Json doc;
    if (!json_parse(text, doc, error)) {
        error = utf8_string(path) + ": " + error;
        return false;
    }
    for (const Json& item : doc.get("achievements").items()) {
        Achievement a;
        a.id = item.get("id").as_string();
        std::string problem;
        bool ok;
        if (a.id.empty()) problem = "no \"id\"";
        else if (std::any_of(list_.begin(), list_.end(), [&](const Achievement& b) { return b.id == a.id; })) problem = "repeated";
        else if (!achievement_condition(item.get("condition"), GameFacts{}, ok, problem)) {}
        if (!problem.empty()) {
            NTR_LOG("[achievements] %s: \"%s\" left out: %s\n", utf8_string(path).c_str(), a.id.c_str(), problem.c_str());
            continue;
        }
        a.name_en = text_of(item.get("name"), "en");
        a.name_pt = text_of(item.get("name"), "pt");
        a.description_en = text_of(item.get("description"), "en");
        a.description_pt = text_of(item.get("description"), "pt");
        if (a.name_en.empty()) a.name_en = a.id;
        a.icon = item.get("icon").as_string();
        a.hidden = item.get("hidden").as_bool();
        a.condition = item.get("condition");
        list_.push_back(std::move(a));
    }
    return true;
}

std::string Achievements::icon_path(const Achievement& a) const {
    if (a.icon.empty()) return "";
    return utf8_string(fs_path(root_.empty() ? "." : root_) / "achievements" / fs_path(a.icon));
}

std::string Achievements::state_path(const std::string& save_path) {
    fs::path p = fs_path(save_path);
    p.replace_extension(".achievements.json");
    return utf8_string(p);
}

void Achievements::load_state(const std::string& save_path) {
    save_path_ = save_path;
    state_.clear();
    std::string path = state_path(save_path), text, error;
    if (!read_text(fs_path(path), text)) return;
    Json doc;
    if (!json_parse(text, doc, error)) {
        NTR_LOG("[achievements] %s: %s (starting over)\n", path.c_str(), error.c_str());
        return;
    }
    for (const Json& item : doc.get("achievements").items()) {
        AchievementStatus s;
        s.unlocked = item.get("unlocked").as_bool();
        s.unlocked_at = item.get("unlocked_at").as_string();
        const std::string& id = item.get("id").as_string();
        if (!id.empty()) state_.emplace_back(id, s);
    }
}

bool Achievements::save_state(std::string& error) const {
    if (save_path_.empty()) return true;
    Json doc = Json::object();
    doc.set("save", utf8_string(fs_path(save_path_).filename()));
    doc.set("version", 1);
    Json list = Json::array();
    auto add = [&](const std::string& id, const AchievementStatus& s, const std::string& icon) {
        Json item = Json::object();
        item.set("id", id);
        item.set("unlocked", s.unlocked);
        item.set("unlocked_at", s.unlocked ? Json(s.unlocked_at) : Json());
        item.set("icon", icon.empty() ? Json() : Json("achievements/" + icon));
        list.push(std::move(item));
    };
    for (const Achievement& a : list_) add(a.id, status(a.id), a.icon);
    // Unlocked achievements that are no longer defined (an older or newer release) are kept.
    for (const auto& [id, s] : state_) {
        bool defined = std::any_of(list_.begin(), list_.end(), [&](const Achievement& a) { return a.id == id; });
        if (!defined && s.unlocked) add(id, s, "");
    }
    doc.set("achievements", std::move(list));

    fs::path path = fs_path(state_path(save_path_)), tmp = path;
    tmp += ".tmp";
    FILE* f = open_file(tmp, "wb");
    if (!f) {
        error = "cannot write " + utf8_string(tmp);
        return false;
    }
    std::string text = json_write(doc);
    bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    written = std::fclose(f) == 0 && written;
    std::error_code ec;
    if (written) fs::rename(tmp, path, ec);
    if (!written || ec) {
        error = "cannot write " + utf8_string(path) + (ec ? ": " + ec.message() : "");
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

const AchievementStatus& Achievements::status(const std::string& id) const {
    static const AchievementStatus kLocked;
    for (const auto& s : state_) {
        if (s.first == id) return s.second;
    }
    return kLocked;
}

int Achievements::unlocked_count() const {
    return int(std::count_if(list_.begin(), list_.end(), [&](const Achievement& a) { return status(a.id).unlocked; }));
}

std::vector<size_t> Achievements::update(const GameFacts& facts) {
    std::vector<size_t> unlocked;
    std::string now;
    for (size_t i = 0; i < list_.size(); i++) {
        const Achievement& a = list_[i];
        if (status(a.id).unlocked) continue;
        bool ok = false;
        std::string error;
        if (!achievement_condition(a.condition, facts, ok, error) || !ok) continue;
        if (now.empty()) now = local_timestamp();
        auto it = std::find_if(state_.begin(), state_.end(), [&](const auto& s) { return s.first == a.id; });
        if (it == state_.end()) it = state_.insert(state_.end(), {a.id, {}});
        it->second.unlocked = true;
        it->second.unlocked_at = now;
        unlocked.push_back(i);
    }
    return unlocked;
}

}  // namespace ntr
