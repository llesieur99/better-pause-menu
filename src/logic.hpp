#pragma once
#include <cstdint>
#include <string>
namespace qol {
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
