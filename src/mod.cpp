#include <algorithm>
#include <cstdint>
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

    hub = mod.ui_page([](const openrac::UiPage& page, const openrac::UiEvent& event) {
        if (event.is(OPENRAC_UI_SHOW)) { refresh_plugin_config(); view = View::Hub; }
        if (event.is(OPENRAC_UI_CHOOSE)) handle_choose(page, event.item());
        page.set(content_for_view());
    });

    if (mod.has_pause_items()) {
        try {
            mod.ui_pause_item("QoL Hub", hub);
        } catch (const openrac::Error&) {
            mod.info("Pause option slots full; use L1+R1+Up for the native QoL Hub");
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

OPENRAC_MOD_CPP("better-pause-menu", "0.7.0", load)
