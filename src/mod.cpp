#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "openrac/mod.hpp"
#include "openrac/games/rac1.hpp"
#include "logic.hpp"

namespace {
openrac::Mod* g = nullptr;

openrac::UiPage hub;
openrac::UiPage status_page;
openrac::UiPage collectibles_page;
openrac::UiPage missions_page;
openrac::UiPage travel_page;
openrac::UiPage checkpoint_page;
openrac::UiPage saves_page;
openrac::UiPage gameplay_page;
openrac::UiPage vendor_page;

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

int pending_level = -1;
bool pending_restart = false;
bool pending_checkpoint_save = false;
bool pending_checkpoint_load = false;
bool pending_autosave = false;

openrac::rac1::Save_RequestAutosave::fn request_autosave = nullptr;

bool playing(const openrac_play_status& s) {
    return s.level >= 0 && s.phase == OPENRAC_PLAY_PLAYING;
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

void setup_pages(openrac::Mod& mod) {
    status_page = mod.ui_page([&mod](const openrac::UiPage& p, const openrac::UiEvent&) {
        const auto status = mod.play_status();
        openrac_pose pose{};
        pose.size = sizeof pose;
        const bool hero = mod.hero_pose(pose);

        openrac::UiContent c;
        c.title = "Status";
        c.items = {
            openrac::UiItem::text(status.level >= 0 ? "Planet: " + mod.level_name(status.level) : "Planet: -"),
            openrac::UiItem::text("Health: " + std::to_string(hero ? pose.health : 0)),
            openrac::UiItem::text("Deaths this session: " + std::to_string(deaths)),
            openrac::UiItem::text("Progress: " + last_progress)
        };
        c.footer = {"Game UI status - no controller shortcut required"};
        p.set(c);
    });

    collectibles_page = mod.ui_page([&mod](const openrac::UiPage& p, const openrac::UiEvent&) {
        int gold = 0, info = 0, skill = 0;
        collectible_counts(gold, info, skill);
        auto fn = mod.original<openrac::rac1::GoldBolts_CountCollected>();
        const int total = fn != nullptr ? fn() : 0;
        const auto status = mod.play_status();

        openrac::UiContent c;
        c.title = "Collectibles";
        c.items = {
            openrac::UiItem::text(status.level >= 0 ? "Planet: " + mod.level_name(status.level) : "Planet: -"),
            openrac::UiItem::text("Gold Bolts: " + std::to_string(total) + "/40"),
            openrac::UiItem::text("Gold Bolts remaining here: " + std::to_string(gold)),
            openrac::UiItem::text("Infobots remaining here: " + std::to_string(info)),
            openrac::UiItem::text("Skill triggers here: " + std::to_string(skill))
        };
        c.footer = {"All collectible information is now inside the pause menu"};
        p.set(c);
    });

    missions_page = mod.ui_page([&mod](const openrac::UiPage& p, const openrac::UiEvent&) {
        const auto status = mod.play_status();
        openrac::UiContent c;
        c.title = "Missions";
        c.items = {
            openrac::UiItem::text(status.level >= 0 ? "Planet: " + mod.level_name(status.level) : "Planet: -"),
            openrac::UiItem::text("Objective: " + objective),
            openrac::UiItem::text("Latest progress: " + last_progress)
        };
        c.footer = {"Campaign information using stable OpenRAC hooks"};
        p.set(c);
    });

    travel_page = mod.ui_page([&mod](const openrac::UiPage& p, const openrac::UiEvent& event) {
        const auto status = mod.play_status();
        openrac::UiContent c;
        c.title = "Fast Travel";
        if (visited.empty()) {
            c.items = {openrac::UiItem::text("No visited planets yet")};
        } else {
            for (int level : visited) {
                c.items.push_back(openrac::UiItem::button(
                    mod.level_name(level) + (level == status.level ? " (current)" : ""),
                    level != status.level && status.phase == OPENRAC_PLAY_PLAYING
                ));
            }
        }
        c.footer = {"Only planets visited in this session are available"};
        p.set(c);

        if (event.is(OPENRAC_UI_CHOOSE)) {
            const int index = event.item();
            if (index >= 0 && index < static_cast<int>(visited.size())
                && visited[index] != status.level && status.phase == OPENRAC_PLAY_PLAYING) {
                pending_level = visited[index];
                p.back();
            }
        }
    });

    checkpoint_page = mod.ui_page([](const openrac::UiPage& p, const openrac::UiEvent& event) {
        const auto status = g->play_status();
        const bool valid = !snapshot.empty() && snapshot_loads == status.loads;

        openrac::UiContent c;
        c.title = "Checkpoints";
        c.items = {
            openrac::UiItem::text(valid ? "Quick checkpoint: ready" : "Quick checkpoint: none"),
            openrac::UiItem::button("Create quick checkpoint", playing(status)),
            openrac::UiItem::button("Restore quick checkpoint", playing(status) && valid)
        };
        c.footer = {"Quick checkpoints last only for the current level load"};
        p.set(c);

        if (event.is(OPENRAC_UI_CHOOSE)) {
            if (event.item() == 1 && playing(status)) {
                pending_checkpoint_save = true;
                p.back();
            } else if (event.item() == 2 && playing(status) && valid) {
                pending_checkpoint_load = true;
                p.back();
            }
        }
    });

    saves_page = mod.ui_page([&mod](const openrac::UiPage& p, const openrac::UiEvent& event) {
        openrac::UiContent c;
        c.title = "Save Manager";
        const int count = save_slot_count();
        for (int i = 0; i < count; ++i) {
            openrac_save_info info{};
            const int state = mod.save_slot(i, info);
            std::string text = "Slot " + std::to_string(i + 1) + ": ";
            if (state == 1) {
                text += mod.level_name(info.level) + " | " + std::to_string(info.bolts)
                     + " bolts | " + std::to_string(info.gold_bolts) + " gold";
            } else {
                text += "empty";
            }
            if (mod.has_api_11() && mod.save_slot_current() == i) text += " | ACTIVE";
            c.items.push_back(openrac::UiItem::text(text));
        }

        const bool can_save = mod.has_api_11() && request_autosave != nullptr
                           && mod.save_slot_current() >= 0;
        const int save_item = static_cast<int>(c.items.size());
        c.items.push_back(openrac::UiItem::button("Save active slot", can_save));
        c.footer = {can_save ? "Writes only to the game's current active save"
                             : "Manual save requires an active save and OpenRAC API 1.11+"};
        p.set(c);

        if (event.is(OPENRAC_UI_CHOOSE) && event.item() == save_item && can_save) {
            pending_autosave = true;
            p.back();
        }
    });

    gameplay_page = mod.ui_page([&mod](const openrac::UiPage& p, const openrac::UiEvent& event) {
        const auto status = mod.play_status();
        openrac::UiContent c;
        c.title = "Gameplay Tools";
        c.items = {
            openrac::UiItem::button("Restart current planet", playing(status)),
            openrac::UiItem::text("Deaths this session: " + std::to_string(deaths))
        };
        c.footer = {"Gameplay actions use the game's native pause UI"};
        p.set(c);

        if (event.is(OPENRAC_UI_CHOOSE)) {
            if (event.item() == 0 && playing(status)) {
                pending_restart = true;
                p.back();
            }
        }
    });

    vendor_page = mod.ui_page([&mod](const openrac::UiPage& p, const openrac::UiEvent&) {
        openrac::UiContent c;
        c.title = "Vendor Info";
        c.items = {
            openrac::UiItem::text("Wallet: " + std::to_string(mod.bolts()) + " bolts"),
            openrac::UiItem::text(last_item >= 0
                ? "Last acquired item: " + std::to_string(last_item)
                : "Last acquired item: -")
        };
        c.footer = {"Vendor information without a separate shortcut"};
        p.set(c);
    });

    hub = mod.ui_page([](const openrac::UiPage& p, const openrac::UiEvent& event) {
        const auto status = g->play_status();
        openrac_pose pose{};
        pose.size = sizeof pose;
        const bool hero = g->hero_pose(pose);

        openrac::UiContent c;
        c.title = "QoL Hub";
        c.items = {
            openrac::UiItem::text(status.level >= 0 ? "Planet: " + g->level_name(status.level) : "Planet: -"),
            openrac::UiItem::text("Health: " + std::to_string(hero ? pose.health : 0)),
            openrac::UiItem::button("Status", true),
            openrac::UiItem::button("Collectibles", true),
            openrac::UiItem::button("Missions", true),
            openrac::UiItem::button("Fast Travel", true),
            openrac::UiItem::button("Checkpoints", g->has_savestates()),
            openrac::UiItem::button("Save Manager", true),
            openrac::UiItem::button("Gameplay Tools", true),
            openrac::UiItem::button("Vendor Info", true)
        };
        c.footer = {"All QoL tools are collected here - no hotkey combinations"};
        p.set(c);

        if (!event.is(OPENRAC_UI_CHOOSE)) return;
        switch (event.item()) {
            case 2: status_page.open(); break;
            case 3: collectibles_page.open(); break;
            case 4: missions_page.open(); break;
            case 5: travel_page.open(); break;
            case 6: if (g->has_savestates()) checkpoint_page.open(); break;
            case 7: saves_page.open(); break;
            case 8: gameplay_page.open(); break;
            case 9: vendor_page.open(); break;
            default: break;
        }
    });
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

    setup_pages(mod);

    if (!mod.has_pause_items())
        throw openrac::Error("Better Pause Menu requires native pause menu items");

    mod.ui_pause_item("QoL Hub", hub);

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
        if (snapshot.empty() && settled == 120 && g->has_savestates()) {
            capture_checkpoint(status);
        }

        if (pending_level >= 0 && status.phase == OPENRAC_PLAY_PLAYING) {
            const int level = pending_level;
            pending_level = -1;
            g->play_travel(level);
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

    mod.info("QoL Hub active: collectibles, missions, travel, checkpoints, saves and gameplay tools are in Pause > Options");
}

OPENRAC_MOD_CPP("better-pause-menu", "0.3.1", load)
