#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
namespace qol {
inline std::string clip_line(std::string text, std::size_t max = 46) {
    if (text.size() <= max) return text;
    if (max <= 3) return text.substr(0, max);
    return text.substr(0, max - 3) + "...";
}
inline std::string hub_status_line(const std::string& planet, int hp, int bolts) {
    return clip_line(planet + " | HP " + std::to_string(hp) + " | " + std::to_string(bolts) + " bolts");
}
inline std::string hub_session_line(int deaths, bool checkpoint_ready, int mods) {
    return clip_line(
        "Deaths " + std::to_string(deaths) + " | checkpoint "
        + (checkpoint_ready ? "ready" : "none") + " | mods " + std::to_string(mods)
    );
}
inline std::string hub_progress_line(const std::string& progress) {
    return clip_line("Last: " + progress);
}
inline std::string collectibles_line(int total, int gold_here, int info_here, int skill_here) {
    return clip_line(
        "Collectibles G" + std::to_string(total) + "/40 | here G" + std::to_string(gold_here)
        + " I" + std::to_string(info_here) + " S" + std::to_string(skill_here)
    );
}
inline std::string hp_line(int hp){ return "Health "+std::to_string(hp); }
inline std::uint16_t pad_buttons(const std::uint8_t* report) {
 return static_cast<std::uint16_t>((~report[0])&0xffu)
      | static_cast<std::uint16_t>(((~report[1])&0xffu)<<8);
}
inline bool combo_pressed(std::uint16_t now,std::uint16_t before,std::uint16_t mask){
 return (now&mask)==mask && (before&mask)!=mask;
}
inline void consume_buttons(std::uint8_t* report, std::uint16_t mask) {
    report[0] = static_cast<std::uint8_t>(report[0] | (mask & 0xffu));
    report[1] = static_cast<std::uint8_t>(report[1] | ((mask >> 8) & 0xffu));
}
}
