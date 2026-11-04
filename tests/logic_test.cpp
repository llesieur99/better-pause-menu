#include <cassert>
#include <string>
#include "logic.hpp"

int main() {
    assert(qol::hp_line(4) == "Health 4");
    assert(qol::hub_status_line("Novalis", 4, 1234) == "Novalis | HP 4 | 1234 bolts");
    assert(qol::hub_session_line(2, true, 5) == "Deaths 2 | checkpoint ready | mods 5");
    assert(qol::hub_progress_line("No recent progress") == "Last: No recent progress");
    assert(qol::hub_progress_line(std::string(100, 'x')).size() <= 46);
    assert(qol::hub_status_line(std::string(100, 'p'), 99, 999999).size() <= 46);
}
