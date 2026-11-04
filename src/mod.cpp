#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#endif

#include "openrac/mod.hpp"
#include "openrac/games/rac1.hpp"
#include "logic.hpp"

namespace {
enum class View {
    Hub,
    Collectibles,
    Missions,
    Travel,
    Checkpoints,
    Saves,
    Gameplay,
    Vendor,
    Accessibility,
    Cutscenes,
    NewGamePlus,
    Settings
};

openrac::Mod* g = nullptr;
openrac::UiPage hub;
openrac::UiPage collectibles_page;
View view = View::Hub;

std::vector<int> visited;
std::vector<std::uint8_t> snapshot;
std::uint32_t snapshot_loads = std::numeric_limits<std::uint32_t>::max();
std::uint32_t last_loads = std::numeric_limits<std::uint32_t>::max();
std::uint32_t settled = 0;
bool was_alive = true;
int deaths = 0;
int last_item = -1;
std::string objective = "No active objective";
std::string last_progress = "No recent progress";

std::string plugins_json;
bool auto_checkpoint = true;

bool pending_restart = false;
bool pending_checkpoint_save = false;
bool pending_checkpoint_load = false;
bool pending_autosave = false;
bool pending_open = false;
std::string travel_feedback;
std::uint16_t previous_buttons = 0;

openrac::rac1::Save_RequestAutosave::fn request_autosave = nullptr;

std::vector<std::uint8_t> qol_preview;
openrac::gaddr pause_texts = 0;
std::int16_t qol_pause_text_id = 0;
bool qol_preview_active = false;

struct ScreenRect {
    openrac::gaddr screen = 0;
    std::int32_t x = 0, y = 0, w = 0, h = 0;
};

struct OptionsLayoutState {
    openrac::gaddr page = 0;
    ScreenRect title, hint, image, list, controls;
    ScreenRect bottom_left, bottom_right;
    openrac::gaddr slot_left = 0, slot_right = 0;
    openrac::gaddr old_slot_left = 0, old_slot_right = 0;
    std::vector<std::uint8_t> image_before;
    bool active = false;
};

OptionsLayoutState options_layout;

void collectible_counts(int& gold, int& info, int& skill);

constexpr openrac::gaddr kMenuSystem = 0x001D5F70u;
constexpr openrac::gaddr kPortStatics = 0xE0200000u;
constexpr openrac::gaddr kPortStaticsEnd = 0xE0300000u;
constexpr std::int16_t kPauseItemText = 0x7D00;
constexpr std::int16_t kPauseTitleText = 0x7D04;
constexpr openrac::gaddr kTextBytes = 64u;
constexpr openrac::gaddr kItemBytes = 0x0Cu;


bool playing(const openrac_play_status& s) {
    return s.level >= 0 && s.phase == OPENRAC_PLAY_PLAYING;
}

bool travel_allowed(const openrac_play_status& s) {
    return s.level >= 0
        && (s.phase == OPENRAC_PLAY_PLAYING
            || s.phase == OPENRAC_PLAY_SCENE
            || s.phase == OPENRAC_PLAY_MENU);
}

void refresh_unlocked_planets() {
    const auto available = g->relocate(0x0013DE48u);
    const auto visited_state = g->relocate(0x0013DE60u);
    const auto status = g->play_status();
    for (int level = 0; level < 20; ++level) {
        const bool unlocked = g->memory().read<std::uint8_t>(
            available + static_cast<openrac::gaddr>(level)
        ) != 0;
        const bool was_visited = g->memory().read<std::uint8_t>(
            visited_state + static_cast<openrac::gaddr>(level)
        ) != 0;
        if ((unlocked || was_visited || level == status.level)
            && std::find(visited.begin(), visited.end(), level) == visited.end()) {
            visited.push_back(level);
        }
    }
    std::sort(visited.begin(), visited.end());
}

std::string trim(std::string v) {
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ' || v.back() == '\t')) v.pop_back();
    std::size_t i = 0;
    while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) ++i;
    return v.substr(i);
}

std::filesystem::path plugin_root_path() {
#if defined(__APPLE__) || defined(__linux__)
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&plugin_root_path), &info) != 0 && info.dli_fname != nullptr) {
        std::filesystem::path p(info.dli_fname);
        // .../plugins/better-pause-menu/lib/<platform>/better_pause_menu.*
        for (int i = 0; i < 4; ++i) p = p.parent_path();
        return p;
    }
#endif
    return {};
}

void refresh_plugin_config() {
    plugins_json.clear();
    const auto root = plugin_root_path();
    if (root.empty()) return;
    std::ifstream jf(root / "plugins.json");
    std::stringstream js;
    js << jf.rdbuf();
    plugins_json = js.str();
}

std::size_t plugin_end(const std::string& json, std::size_t pos) {
    const auto next = json.find("\"id\"", pos + 4);
    return next == std::string::npos ? json.size() : next;
}

std::string plugin_value(const std::string& id, const std::string& key) {
    const std::string id1 = "\"id\": \"" + id + "\"";
    const std::string id2 = "\"id\":\"" + id + "\"";
    auto pos = plugins_json.find(id1);
    if (pos == std::string::npos) pos = plugins_json.find(id2);
    if (pos == std::string::npos) return {};
    const auto end = plugin_end(plugins_json, pos);
    const std::string needle = "\"" + key + "\"";
    const auto field = plugins_json.find(needle, pos);
    if (field == std::string::npos || field >= end) return {};
    const auto colon = plugins_json.find(':', field + needle.size());
    if (colon == std::string::npos || colon >= end) return {};
    const auto first = plugins_json.find_first_not_of(" \t\r\n", colon + 1);
    if (first == std::string::npos || first >= end) return {};
    if (plugins_json[first] == '"') {
        const auto last = plugins_json.find('"', first + 1);
        return last == std::string::npos || last >= end ? std::string{} : plugins_json.substr(first + 1, last - first - 1);
    }
    const auto last = plugins_json.find_first_of(",}\r\n", first);
    return trim(plugins_json.substr(first, (last == std::string::npos ? end : std::min(last, end)) - first));
}

bool plugin_is_enabled(const std::string& id) {
    const std::string id1 = "\"id\": \"" + id + "\"";
    const std::string id2 = "\"id\":\"" + id + "\"";
    auto pos = plugins_json.find(id1);
    if (pos == std::string::npos) pos = plugins_json.find(id2);
    if (pos == std::string::npos) return false;
    const auto end = plugin_end(plugins_json, pos);
    const auto field = plugins_json.find("\"enabled\"", pos);
    if (field == std::string::npos || field >= end) return false;
    const auto colon = plugins_json.find(':', field);
    if (colon == std::string::npos || colon >= end) return false;
    const auto value = plugins_json.find_first_not_of(" \t\r\n", colon + 1);
    return value != std::string::npos && value < end && plugins_json.compare(value, 4, "true") == 0;
}

int plugin_int(const std::string& id, const std::string& key, int fallback) {
    const auto raw = plugin_value(id, key);
    if (raw.empty()) return fallback;
    try { return std::stoi(raw); } catch (...) { return fallback; }
}

bool plugin_bool(const std::string& id, const std::string& key, bool fallback) {
    const auto raw = plugin_value(id, key);
    if (raw == "true") return true;
    if (raw == "false") return false;
    return fallback;
}

bool printable_text(openrac::gaddr at) {
    const auto* p = static_cast<const unsigned char*>(g->memory().pointer(at));
    int n = 0;
    while (n < 40 && p[n] != 0) {
        if (p[n] < 0x20 || p[n] > 0x7E) return false;
        ++n;
    }
    return n > 0 && n < 40;
}

void locate_pause_texts() {
    if (pause_texts != 0) return;
    static constexpr char kNeedle[] = "QoL Hub";
    const auto memory = g->memory();
    for (openrac::gaddr at = kPortStatics; at + sizeof(kNeedle) < kPortStaticsEnd; ++at) {
        if (std::memcmp(memory.pointer(at), kNeedle, sizeof(kNeedle)) != 0) continue;
        if (at + kTextBytes < kPortStaticsEnd && printable_text(at + kTextBytes)) {
            pause_texts = at;
            qol_pause_text_id = kPauseItemText;
        } else if (at >= kPortStatics + kTextBytes && printable_text(at - kTextBytes)) {
            pause_texts = at - kTextBytes;
            qol_pause_text_id = kPauseItemText + 1;
        } else {
            pause_texts = at;
            qol_pause_text_id = kPauseItemText;
        }
        g->info("QoL Hub: native Options text table connected");
        return;
    }
}

void load_qol_preview() {
    qol_preview.clear();
    const auto root = plugin_root_path();
    if (root.empty()) return;
    std::ifstream in(root / "better-pause-menu" / "assets" / "qol-preview.clut", std::ios::binary);
    if (!in) return;
    qol_preview.assign(std::istreambuf_iterator<char>(in), {});
    if (qol_preview.size() != 0x10420u) {
        g->warn("QoL Hub: preview asset has an unexpected size");
        qol_preview.clear();
    }
}

ScreenRect capture_rect(openrac::gaddr screen) {
    ScreenRect r;
    r.screen = screen;
    if (screen == 0) return r;
    const auto memory = g->memory();
    r.x = memory.read<std::int32_t>(screen + 0x18);
    r.y = memory.read<std::int32_t>(screen + 0x1C);
    r.w = memory.read<std::int32_t>(screen + 0x20);
    r.h = memory.read<std::int32_t>(screen + 0x24);
    return r;
}

void set_rect(openrac::gaddr screen, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
    if (screen == 0) return;
    auto memory = g->memory();
    memory.write<std::int32_t>(screen + 0x18, x);
    memory.write<std::int32_t>(screen + 0x1C, y);
    memory.write<std::int32_t>(screen + 0x20, w);
    memory.write<std::int32_t>(screen + 0x24, h);
}

void restore_rect(const ScreenRect& r) {
    if (r.screen != 0) set_rect(r.screen, r.x, r.y, r.w, r.h);
}

void put_pause_text(int id, const std::string& raw) {
    if (pause_texts == 0 || id < kPauseItemText || id > 0x7D0E) return;
    const std::string text = qol::clip_line(raw, 63);
    auto* out = static_cast<char*>(g->memory().pointer(
        pause_texts + static_cast<openrac::gaddr>(id - kPauseItemText) * kTextBytes
    ));
    std::memset(out, 0, kTextBytes);
    std::memcpy(out, text.data(), std::min<std::size_t>(text.size(), kTextBytes - 1));
}

int enabled_mod_count() {
    int count = 0;
    std::size_t pos = 0;
    while ((pos = plugins_json.find("\"id\"", pos)) != std::string::npos) {
        const auto colon = plugins_json.find(':', pos + 4);
        const auto q1 = colon == std::string::npos ? std::string::npos : plugins_json.find('"', colon + 1);
        const auto q2 = q1 == std::string::npos ? std::string::npos : plugins_json.find('"', q1 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos) break;
        const std::string id = plugins_json.substr(q1 + 1, q2 - q1 - 1);
        if (plugin_is_enabled(id)) ++count;
        pos = q2 + 1;
    }
    return count;
}

openrac::gaddr find_options_hint_screen(openrac::gaddr page, int count) {
    const auto memory = g->memory();
    for (openrac::gaddr off = 0x44; off < 0x7C; off += 4) {
        const auto screen = memory.read<std::uint32_t>(page + off);
        if (screen == 0) continue;
        const auto list = memory.read<std::uint32_t>(screen + 0x34);
        if (list < 0x100000u || list >= 0x2000000u || (list & 3u) != 0) continue;
        bool ok = true;
        for (int i = 0; i < count; ++i) {
            const auto id = memory.read<std::int32_t>(list + 4u * static_cast<openrac::gaddr>(i));
            if (id <= 0 || id >= 0x8000) { ok = false; break; }
        }
        if (ok && memory.read<std::int32_t>(list + 4u * static_cast<openrac::gaddr>(count)) == 0)
            return screen;
    }
    return 0;
}

openrac::gaddr find_options_image(openrac::gaddr page) {
    const auto memory = g->memory();
    for (openrac::gaddr off = 0x44; off < 0x7C; off += 4) {
        const auto screen = memory.read<std::uint32_t>(page + off);
        if (screen == 0) continue;
        if (memory.read<std::int32_t>(screen + 0x38) == 0x93
            && memory.read<std::int32_t>(screen + 0x3C) == 0x85
            && memory.read<std::uint32_t>(screen + 0x30) != 0) return screen;
    }
    return 0;
}

openrac::gaddr item_page(openrac::gaddr items, int index) {
    if (items == 0 || index < 0) return 0;
    return g->memory().read<std::uint32_t>(
        items + kItemBytes * static_cast<openrac::gaddr>(index) + 4u
    );
}

void configure_status_rows(openrac::gaddr list, int first_text, const std::vector<std::string>& lines) {
    if (list == 0 || pause_texts == 0) return;
    auto memory = g->memory();
    const auto rows = memory.read<std::uint32_t>(list + 0x34);
    if (rows == 0) return;
    constexpr int kEmptyText = 0x7D0E;
    put_pause_text(kEmptyText, "");
    const int n = std::min<int>(3, static_cast<int>(lines.size()));
    for (int i = 0; i < n; ++i) {
        const auto row = rows + 0x18u * static_cast<openrac::gaddr>(i);
        put_pause_text(first_text + i, lines[static_cast<std::size_t>(i)]);
        memory.write<std::int32_t>(row + 0x00, first_text + i);
        const auto value = memory.read<std::uint32_t>(row + 0x04);
        if (value != 0) memory.write<std::int32_t>(value, 1);
        memory.write<std::int32_t>(row + 0x08, kEmptyText);
        memory.write<std::int32_t>(row + 0x0C, kEmptyText);
        memory.write<std::int32_t>(row + 0x10, kEmptyText);
        memory.write<std::int32_t>(row + 0x14, 0);
    }
    memory.write<std::int32_t>(rows + 0x18u * static_cast<openrac::gaddr>(n), 0);
    memory.write<std::int32_t>(list + 0x38, 0);
}

void restore_options_layout() {
    if (!options_layout.active) return;
    auto memory = g->memory();
    restore_rect(options_layout.title);
    restore_rect(options_layout.hint);
    restore_rect(options_layout.image);
    restore_rect(options_layout.list);
    restore_rect(options_layout.controls);
    restore_rect(options_layout.bottom_left);
    restore_rect(options_layout.bottom_right);
    if (options_layout.page != 0) {
        memory.write<std::uint32_t>(options_layout.page + 0x58, options_layout.old_slot_left);
        memory.write<std::uint32_t>(options_layout.page + 0x5C, options_layout.old_slot_right);
    }
    if (options_layout.image.screen != 0) {
        const auto buffer = memory.read<std::uint32_t>(options_layout.image.screen + 0x48);
        if (buffer != 0 && options_layout.image_before.size() == 0x10420u) {
            std::memcpy(memory.pointer(buffer), options_layout.image_before.data(), options_layout.image_before.size());
        }
        // Force the game's stream loader to refresh the newly selected native preview. This is what
        // restores Camera/Kamera's original symbol after leaving the QoL row.
        memory.write<std::int32_t>(options_layout.image.screen + 0x44, 0);
        memory.write<std::int32_t>(options_layout.image.screen + 0x50, -1);
        memory.write<std::int32_t>(options_layout.image.screen + 0x54, -1);
    }
    options_layout = OptionsLayoutState{};
    qol_preview_active = false;
}

void decorate_options_page() {
    locate_pause_texts();
    auto memory = g->memory();
    const auto menu = g->relocate(kMenuSystem);
    const auto page = memory.read<std::uint32_t>(menu + 4);
    if (page == 0 || memory.read<std::int32_t>(page + 0x3C) != 8) {
        restore_options_layout();
        return;
    }
    const auto list = memory.read<std::uint32_t>(page + 0x40);
    if (list == 0) { restore_options_layout(); return; }
    const auto items = memory.read<std::uint32_t>(list + 0x34);
    if (items == 0) { restore_options_layout(); return; }

    int count = 0, qol_index = -1, other_mod_index = -1;
    while (count < 16) {
        const auto id = memory.read<std::int16_t>(items + kItemBytes * static_cast<openrac::gaddr>(count));
        if (id == 0) break;
        if (id == qol_pause_text_id) qol_index = count;
        if (id >= kPauseItemText && id < kPauseTitleText && id != qol_pause_text_id) other_mod_index = count;
        ++count;
    }
    if (qol_index < 0 || count == 0) { restore_options_layout(); return; }

    const int selected = memory.read<std::int32_t>(list + 0x40);
    const auto hint = find_options_hint_screen(page, count);
    const auto image = find_options_image(page);

    // Keep a proper QoL description bound to the row even before it is selected.
    if (pause_texts != 0 && hint != 0) {
        const auto hints = memory.read<std::uint32_t>(hint + 0x34);
        if (hints != 0) {
            put_pause_text(0x7D0D, "QoL Hub - Status, Saves, Travel and Mod tools");
            memory.write<std::int32_t>(hints + 4u * static_cast<openrac::gaddr>(qol_index), 0x7D0D);
        }
    }

    if (selected != qol_index || image == 0 || qol_preview.empty()) {
        restore_options_layout();
        return;
    }

    if (!options_layout.active || options_layout.page != page) {
        restore_options_layout();
        options_layout.page = page;
        options_layout.title = capture_rect(memory.read<std::uint32_t>(page + 0x44));
        options_layout.hint = capture_rect(hint);
        options_layout.image = capture_rect(image);
        options_layout.list = capture_rect(list);
        options_layout.controls = capture_rect(memory.read<std::uint32_t>(page + 0x54));
        options_layout.old_slot_left = memory.read<std::uint32_t>(page + 0x58);
        options_layout.old_slot_right = memory.read<std::uint32_t>(page + 0x5C);

        const auto own_page = item_page(items, qol_index);
        const auto own_list = own_page != 0 ? memory.read<std::uint32_t>(own_page + 0x40) : 0;
        const auto other_page = item_page(items, other_mod_index);
        const auto other_list = other_page != 0 ? memory.read<std::uint32_t>(other_page + 0x40) : 0;
        options_layout.bottom_left = capture_rect(own_list);
        options_layout.bottom_right = capture_rect(other_list);
        options_layout.slot_left = own_list;
        options_layout.slot_right = other_list;

        const auto buffer = memory.read<std::uint32_t>(image + 0x48);
        if (buffer != 0) {
            options_layout.image_before.resize(0x10420u);
            std::memcpy(options_layout.image_before.data(), memory.pointer(buffer), options_layout.image_before.size());
        }
        options_layout.active = true;
        g->info("QoL Hub: first-draft native Options layout active");
    }

    // Exact panel geometry of the supplied 512x448 first draft, mapped onto RAC1's own screens.
    set_rect(options_layout.title.screen, 136, 15, 241, 55);
    set_rect(options_layout.image.screen, 23, 88, 162, 236);       // left QoL artwork/checklist
    set_rect(options_layout.hint.screen, 328, 88, 163, 236);       // right live collectibles
    set_rect(options_layout.list.screen, 195, 82, 122, 245);       // native, fully interactive list
    set_rect(options_layout.controls.screen, 194, 346, 124, 66);   // native A/Back hints

    if (options_layout.slot_left != 0) {
        memory.write<std::uint32_t>(page + 0x58, options_layout.slot_left);
        set_rect(options_layout.slot_left, 23, 346, 162, 66);
    }
    if (options_layout.slot_right != 0 && options_layout.slot_right != options_layout.slot_left) {
        memory.write<std::uint32_t>(page + 0x5C, options_layout.slot_right);
        set_rect(options_layout.slot_right, 328, 346, 163, 66);
    }

    const auto status = g->play_status();
    openrac_pose pose{};
    pose.size = sizeof pose;
    const bool hero = g->hero_pose(pose);
    const bool checkpoint = !snapshot.empty() && snapshot_loads == status.loads;
    const std::string planet = status.level >= 0 ? g->level_name(status.level) : "-";
    const int hp = hero ? pose.health : 0;

    if (options_layout.slot_left != 0) {
        configure_status_rows(options_layout.slot_left, 0x7D05, {
            "Planet: " + planet,
            "HP: " + std::to_string(hp),
            "Bolts: " + std::to_string(g->bolts())
        });
    }
    if (options_layout.slot_right != 0 && options_layout.slot_right != options_layout.slot_left) {
        configure_status_rows(options_layout.slot_right, 0x7D0A, {
            "Tode: " + std::to_string(deaths),
            std::string("Checkpoint: ") + (checkpoint ? "Aktiv" : "Keiner"),
            "Mods: " + std::to_string(enabled_mod_count()) + " Aktiv"
        });
    }

    int gold_here = 0, info_here = 0, skill_here = 0;
    collectible_counts(gold_here, info_here, skill_here);
    auto gold_fn = g->original<openrac::rac1::GoldBolts_CountCollected>();
    const int gold_total = gold_fn != nullptr ? gold_fn() : 0;
    if (hint != 0 && pause_texts != 0) {
        const auto hints = memory.read<std::uint32_t>(hint + 0x34);
        if (hints != 0) {
            put_pause_text(0x7D0D,
                "Collectibles\nBolts " + std::to_string(g->bolts())
                + "\nGold Bolts " + std::to_string(gold_total) + "/40"
                + "\nInfobots hier " + std::to_string(info_here)
                + "\nSkillpunkte hier " + std::to_string(skill_here));
            memory.write<std::int32_t>(hints + 4u * static_cast<openrac::gaddr>(qol_index), 0x7D0D);
        }
    }

    const auto buffer = memory.read<std::uint32_t>(image + 0x48);
    if (buffer != 0) {
        std::memcpy(memory.pointer(buffer), qol_preview.data(), qol_preview.size());
        memory.write<std::int32_t>(image + 0x44, 2);
        memory.write<std::int32_t>(image + 0x50, qol_index);
        memory.write<std::int32_t>(image + 0x54, -1);
        qol_preview_active = true;
    }
}

int save_slot_count() {
    for (int i = 0; i < 16; ++i) {
        openrac_save_info info{};
        if (g->save_slot(i, info) < 0) return i;
    }
    return 16;
}

void collectible_counts(int& gold, int& info, int& skill) {
    gold = info = skill = 0;
    for (const auto& m : g->moby_list()) {
        if ((m.flags & OPENRAC_MOBY_DEAD) != 0) continue;
        if (m.oclass == 1134) ++gold;
        else if (m.oclass == 750) ++info;
        else if (m.oclass == 1559) ++skill;
    }
}

void capture_checkpoint(const openrac_play_status& status) {
    if (!playing(status)) return;
    openrac_pose pose{};
    pose.size = sizeof pose;
    if (!g->hero_pose(pose) || pose.health <= 0) return;
    const auto size = g->savestate_size();
    if (size == 0) return;
    snapshot.resize(size);
    g->savestate_save(snapshot.data(), snapshot.size());
    snapshot_loads = status.loads;
}

void handle_choose(const openrac::UiPage& page, int item) {
    const auto status = g->play_status();

    switch (view) {
        case View::Hub:
            if (item >= 0 && item <= 10) {
                static const View core[] = {
                    View::Collectibles, View::Missions, View::Travel,
                    View::Checkpoints, View::Saves, View::Gameplay, View::Vendor,
                    View::Accessibility, View::Cutscenes, View::NewGamePlus, View::Settings
                };
                view = core[item];
            }
            break;

        case View::Collectibles:
            if (item == 5) view = View::Hub;
            break;

        case View::Missions:
            if (item == 3) view = View::Hub;
            break;

        case View::Travel: {
            const int count = static_cast<int>(visited.size());
            const int back_item = visited.empty() ? 1 : count;
            if (item == back_item) {
                view = View::Hub;
            } else if (!visited.empty() && item >= 0 && item < count
                       && visited[item] != status.level && travel_allowed(status)) {
                const int level = visited[item];
                const int result = g->play_travel(level);
                if (result == OPENRAC_HOOK_OK || result == OPENRAC_HOOK_TAKEN) {
                    travel_feedback.clear();
                    g->info("QoL Hub Fast Travel: accepted level " + std::to_string(level));
                    view = View::Hub;
                    page.back();
                } else {
                    travel_feedback = "Travel unavailable right now";
                    g->info("QoL Hub Fast Travel: refused level " + std::to_string(level));
                }
            }
            break;
        }

        case View::Checkpoints: {
            const bool valid = !snapshot.empty() && snapshot_loads == status.loads;
            if (item == 1 && playing(status)) {
                pending_checkpoint_save = true;
                view = View::Hub;
                page.back();
            } else if (item == 2 && playing(status) && valid) {
                pending_checkpoint_load = true;
                view = View::Hub;
                page.back();
            } else if (item == 3) {
                view = View::Hub;
            }
            break;
        }

        case View::Saves: {
            const int count = save_slot_count();
            const int save_item = count;
            const int back_item = count + 1;
            const bool can_save = g->has_api_11() && request_autosave != nullptr
                               && g->save_slot_current() >= 0;
            if (item == save_item && can_save) {
                pending_autosave = true;
                view = View::Hub;
                page.back();
            } else if (item == back_item) {
                view = View::Hub;
            }
            break;
        }

        case View::Gameplay:
            if (item == 0 && playing(status)) {
                pending_restart = true;
                view = View::Hub;
                page.back();
            } else if (item == 2) {
                view = View::Hub;
            }
            break;

        case View::Vendor:
            if (item == 2) view = View::Hub;
            break;

        case View::Accessibility:
            if (item == 5) view = View::Hub;
            break;

        case View::Cutscenes:
            if (item == 4) view = View::Hub;
            break;

        case View::NewGamePlus:
            if (item == 3) view = View::Hub;
            break;

        case View::Settings:
            if (item == 0) auto_checkpoint = !auto_checkpoint;
            else if (item == 4) auto_checkpoint = true;
            else if (item == 5) view = View::Hub;
            break;
    }
}

openrac::UiContent content_for_view() {
    const auto status = g->play_status();
    openrac::UiContent c;

    switch (view) {
        case View::Hub: {
            openrac_pose pose{};
            pose.size = sizeof pose;
            const bool hero = g->hero_pose(pose);
            const bool checkpoint_ready = !snapshot.empty() && snapshot_loads == status.loads;
            const std::string planet = status.level >= 0 ? g->level_name(status.level) : "-";
            int gold = 0, info = 0, skill = 0;
            collectible_counts(gold, info, skill);
            auto gold_fn = g->original<openrac::rac1::GoldBolts_CountCollected>();
            const int gold_total = gold_fn != nullptr ? gold_fn() : 0;

            c.title = "QoL Hub";
            c.items = {
                openrac::UiItem::button(qol::collectibles_line(gold_total, gold, info, skill), true),
                openrac::UiItem::button("Missions", true),
                openrac::UiItem::button("Fast Travel", true),
                openrac::UiItem::button("Checkpoints", g->has_savestates()),
                openrac::UiItem::button("Save Manager", true),
                openrac::UiItem::button("Gameplay Tools", true),
                openrac::UiItem::button("Vendor Info", true),
                openrac::UiItem::button("Accessibility", true),
                openrac::UiItem::button("Cutscene Manager", true),
                openrac::UiItem::button("New Game Plus", true),
                openrac::UiItem::button("Settings", true)
            };
            c.footer = {
                qol::hub_status_line(planet, hero ? pose.health : 0, g->bolts()),
                qol::hub_session_line(deaths, checkpoint_ready, 12),
                qol::hub_progress_line(last_progress)
            };
            break;
        }

        case View::Collectibles: {
            int gold = 0, info = 0, skill = 0;
            collectible_counts(gold, info, skill);
            auto fn = g->original<openrac::rac1::GoldBolts_CountCollected>();
            const int total = fn != nullptr ? fn() : 0;
            c.title = "Collectibles";
            c.items = {
                openrac::UiItem::text(status.level >= 0 ? "Planet: " + g->level_name(status.level) : "Planet: -"),
                openrac::UiItem::text("Gold Bolts: " + std::to_string(total) + "/40"),
                openrac::UiItem::text("Gold Bolts remaining here: " + std::to_string(gold)),
                openrac::UiItem::text("Infobots remaining here: " + std::to_string(info)),
                openrac::UiItem::text("Skill triggers here: " + std::to_string(skill)),
                openrac::UiItem::button("Back to QoL Hub", true)
            };
            break;
        }

        case View::Missions:
            c.title = "Missions";
            c.items = {
                openrac::UiItem::text(status.level >= 0 ? "Planet: " + g->level_name(status.level) : "Planet: -"),
                openrac::UiItem::text("Objective: " + objective),
                openrac::UiItem::text("Latest progress: " + last_progress),
                openrac::UiItem::button("Back to QoL Hub", true)
            };
            break;

        case View::Travel:
            refresh_unlocked_planets();
            c.title = "Fast Travel";
            if (visited.empty()) {
                c.items = {
                    openrac::UiItem::text("No visited planets yet"),
                    openrac::UiItem::button("Back to QoL Hub", true)
                };
            } else {
                for (int level : visited) {
                    c.items.push_back(openrac::UiItem::button(
                        g->level_name(level) + (level == status.level ? " (current)" : ""),
                        level != status.level && travel_allowed(status)
                    ));
                }
                c.items.push_back(openrac::UiItem::button("Back to QoL Hub", true));
            }
            c.footer = {travel_feedback.empty()
                ? "Integrated here; Fast Travel also remains a standalone package"
                : travel_feedback};
            break;

        case View::Checkpoints: {
            const bool valid = !snapshot.empty() && snapshot_loads == status.loads;
            c.title = "Checkpoints";
            c.items = {
                openrac::UiItem::text(valid ? "Quick checkpoint: ready" : "Quick checkpoint: none"),
                openrac::UiItem::button("Create quick checkpoint", playing(status)),
                openrac::UiItem::button("Restore quick checkpoint", playing(status) && valid),
                openrac::UiItem::button("Back to QoL Hub", true)
            };
            c.footer = {"Checkpoint Plus is absorbed into this Hub module"};
            break;
        }

        case View::Saves: {
            c.title = "Save Manager";
            const int count = save_slot_count();
            for (int i = 0; i < count; ++i) {
                openrac_save_info info{};
                const int state = g->save_slot(i, info);
                std::string text = "Slot " + std::to_string(i + 1) + ": ";
                if (state == 1) {
                    text += g->level_name(info.level) + " | " + std::to_string(info.bolts)
                         + " bolts | " + std::to_string(info.gold_bolts) + " gold";
                } else {
                    text += "empty";
                }
                if (g->has_api_11() && g->save_slot_current() == i) text += " | ACTIVE";
                c.items.push_back(openrac::UiItem::text(text));
            }
            const bool can_save = g->has_api_11() && request_autosave != nullptr
                               && g->save_slot_current() >= 0;
            c.items.push_back(openrac::UiItem::button("Save active slot", can_save));
            c.items.push_back(openrac::UiItem::button("Back to QoL Hub", true));
            c.footer = {
                can_save ? "Integrated here; Save Manager also remains a standalone package"
                         : "Manual save requires an active save and OpenRAC API 1.11+"
            };
            break;
        }

        case View::Gameplay:
            c.title = "Gameplay Tools";
            c.items = {
                openrac::UiItem::button("Restart current planet", playing(status)),
                openrac::UiItem::text("Deaths this session: " + std::to_string(deaths)),
                openrac::UiItem::button("Back to QoL Hub", true)
            };
            c.footer = {"Death Improvements is absorbed; cutscene safety stays separate"};
            break;

        case View::Vendor:
            c.title = "Vendor Info";
            c.items = {
                openrac::UiItem::text("Wallet: " + std::to_string(g->bolts()) + " bolts"),
                openrac::UiItem::text(last_item >= 0
                    ? "Last acquired item: " + std::to_string(last_item)
                    : "Last acquired item: -"),
                openrac::UiItem::button("Back to QoL Hub", true)
            };
            c.footer = {"Improved Vendor is absorbed into this Hub module"};
            break;

        case View::Accessibility: {
            const bool enabled = plugin_is_enabled("accessibility-pack");
            c.title = "Accessibility";
            c.items = {
                openrac::UiItem::text(std::string("Background mod: ") + (enabled ? "ACTIVE" : "OFF")),
                openrac::UiItem::text(std::string("Hold Cross: ") + (plugin_bool("accessibility-pack", "turbo_cross", true) ? "ON" : "OFF")),
                openrac::UiItem::text(std::string("Hold Circle: ") + (plugin_bool("accessibility-pack", "turbo_circle", false) ? "ON" : "OFF")),
                openrac::UiItem::text(std::string("Hold Square: ") + (plugin_bool("accessibility-pack", "turbo_square", false) ? "ON" : "OFF")),
                openrac::UiItem::text("Mash interval: " + std::to_string(plugin_int("accessibility-pack", "turbo_interval", 6)) + " ticks"),
                openrac::UiItem::button("Back to QoL Hub", true)
            };
            c.footer = {"Background behavior stays separate; change values in Launcher > Mods"};
            break;
        }

        case View::Cutscenes: {
            const bool enabled = plugin_is_enabled("cutscene-manager");
            const int mode = plugin_int("cutscene-manager", "mode", 0);
            c.title = "Cutscene Manager";
            c.items = {
                openrac::UiItem::text(std::string("Background mod: ") + (enabled ? "ACTIVE" : "OFF")),
                openrac::UiItem::text(std::string("Safe repeat skipping: ") + (mode == 1 ? "ON" : "OFF")),
                openrac::UiItem::text("Minimum scene frames: " + std::to_string(plugin_int("cutscene-manager", "min_frames", 45))),
                openrac::UiItem::text("Progression/end scenes: NEVER skipped"),
                openrac::UiItem::button("Back to QoL Hub", true)
            };
            c.footer = {"Background safety logic remains separate from the UI hub"};
            break;
        }

        case View::NewGamePlus: {
            const bool enabled = plugin_is_enabled("new-game-plus");
            c.title = "New Game Plus";
            c.items = {
                openrac::UiItem::text(std::string("Background mod: ") + (enabled ? "ACTIVE" : "OFF")),
                openrac::UiItem::text("Starting bolt bonus: " + std::to_string(plugin_int("new-game-plus", "bonus_bolts", 0))),
                openrac::UiItem::text("Cutscenes and progression stay native"),
                openrac::UiItem::button("Back to QoL Hub", true)
            };
            c.footer = {"Challenge Mode background logic remains separate"};
            break;
        }

        case View::Settings:
            c.title = "QoL Hub Settings";
            c.items = {
                openrac::UiItem::button(std::string("Auto checkpoint: ") + (auto_checkpoint ? "ON" : "OFF"), true),
                openrac::UiItem::text("Fast Travel: integrated + standalone package"),
                openrac::UiItem::text("Save Manager: integrated + standalone package"),
                openrac::UiItem::text("Background settings use Launcher > Mods"),
                openrac::UiItem::button("Restore defaults", true),
                openrac::UiItem::button("Back to QoL Hub", true)
            };
            c.footer = {"HUD/Collectibles/Missions/Checkpoint/Death/Vendor are Hub modules"};
            break;
    }

    return c;
}

openrac::UiContent direct_collectibles_content() {
    const auto status = g->play_status();
    int gold_here = 0, info_here = 0, skill_here = 0;
    collectible_counts(gold_here, info_here, skill_here);
    auto fn = g->original<openrac::rac1::GoldBolts_CountCollected>();
    const int total = fn != nullptr ? fn() : 0;
    openrac::UiContent c;
    c.title = "Collectibles";
    c.items = {
        openrac::UiItem::text(status.level >= 0 ? "Planet: " + g->level_name(status.level) : "Planet: -"),
        openrac::UiItem::text("Gold Bolts: " + std::to_string(total) + "/40"),
        openrac::UiItem::text("Gold remaining here: " + std::to_string(gold_here)),
        openrac::UiItem::text("Infobots remaining here: " + std::to_string(info_here)),
        openrac::UiItem::text("Skill triggers here: " + std::to_string(skill_here))
    };
    c.footer = {"Direct Options entry - Triangle returns to Options"};
    return c;
}
}

void load(openrac::Mod& mod) {
    if (mod.game() != "rac1" || !mod.has_play() || !mod.has_world())
        throw openrac::Error("Better Pause Menu requires RAC1 interface 1.9+ play/world APIs");

    g = &mod;

    mod.hook<openrac::rac1::GoldBolts_CountCollected>([](auto original) {
        return original();
    });
    mod.hook<openrac::rac1::Planet_Unlock>([](auto original, std::int32_t level) {
        original(level);
        last_progress = "Destination unlocked: " + g->level_name(level);
    });
    mod.hook<openrac::rac1::Item_Give>([](auto original, std::int32_t id, std::int32_t equip) {
        original(id, equip);
        last_item = id;
        last_progress = "Item acquired: " + std::to_string(id);
    });

    if (mod.has_api_11()) {
        try {
            request_autosave = mod.required_function<openrac::rac1::Save_RequestAutosave::fn>(
                openrac::rac1::Save_RequestAutosave::name
            );
        } catch (const openrac::Error&) {
            request_autosave = nullptr;
        }
    }

    refresh_plugin_config();
    load_qol_preview();

    hub = mod.ui_page([](const openrac::UiPage& page, const openrac::UiEvent& event) {
        if (event.is(OPENRAC_UI_SHOW)) { refresh_plugin_config(); view = View::Hub; }
        if (event.is(OPENRAC_UI_CHOOSE)) handle_choose(page, event.item());
        page.set(content_for_view());
    });
    collectibles_page = mod.ui_page([](const openrac::UiPage& page, const openrac::UiEvent&) {
        page.set(direct_collectibles_content());
    });

    if (mod.has_pause_items()) {
        try {
            mod.ui_pause_item("QoL Hub", hub);
            mod.ui_pause_item("Collectibles", collectibles_page);
        } catch (const openrac::Error&) {
            mod.info("Pause option slots full; QoL Hub remains available through L1+R1+Up");
        }
    }

    mod.on_pad([](int port, std::uint8_t* report, int size) {
        if (port != 0 || report == nullptr || size < 2) return;
        const auto now = qol::pad_buttons(report);
        const auto mask = static_cast<std::uint16_t>(
            OPENRAC_PAD_L1 | OPENRAC_PAD_R1 | OPENRAC_PAD_UP
        );
        if (qol::combo_pressed(now, previous_buttons, mask)) pending_open = true;
        if ((now & mask) == mask) qol::consume_buttons(report, mask);
        previous_buttons = now;
    });

    mod.on_tick_boundary([](std::uint32_t) {
        const auto status = g->play_status();

        if (status.level >= 0
            && std::find(visited.begin(), visited.end(), status.level) == visited.end()) {
            visited.push_back(status.level);
        }

        if (status.loads != last_loads) {
            last_loads = status.loads;
            settled = 0;
            snapshot.clear();
            snapshot_loads = std::numeric_limits<std::uint32_t>::max();
            objective = status.level >= 0 ? "Explore " + g->level_name(status.level)
                                          : "No active objective";
        }
        if (status.phase == OPENRAC_PLAY_PLAYING) ++settled;

        openrac_pose pose{};
        pose.size = sizeof pose;
        if (g->hero_pose(pose)) {
            if (was_alive && pose.health <= 0) ++deaths;
            was_alive = pose.health > 0;
        } else {
            was_alive = true;
        }

        decorate_options_page();

        if (pending_open) {
            pending_open = false;
            view = View::Hub;
            hub.open();
        }


        if (pending_checkpoint_save) {
            pending_checkpoint_save = false;
            capture_checkpoint(status);
        }
        if (pending_checkpoint_load) {
            pending_checkpoint_load = false;
            if (!snapshot.empty() && snapshot_loads == status.loads) {
                auto copy = snapshot;
                g->savestate_load(copy.data(), copy.size());
            }
        }
        if (auto_checkpoint && snapshot.empty() && settled == 120 && g->has_savestates()) {
            capture_checkpoint(status);
        }

        if (pending_restart && playing(status)) {
            pending_restart = false;
            g->play_travel(status.level);
        }
        if (pending_autosave && status.phase == OPENRAC_PLAY_PLAYING
            && request_autosave != nullptr) {
            pending_autosave = false;
            request_autosave(0, -1);
        }
    });

    mod.info("QoL Hub active in one native RAC1 page");
}

OPENRAC_MOD_CPP("better-pause-menu", "0.8.0", load)
