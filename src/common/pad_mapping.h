#pragma once
#include <cstdint>

namespace input {
struct Pad;
}

// Controller setup screen (F9): maps the buttons and axes of any USB pad onto the Xbox controller. The mapping is
// kept per device in controller_mapping.txt beside the executable.
namespace padmap {

void Load();                          // GameInfo::window: applies the saved mappings
void Ui(double fps);                  // GameInfo::ui
void Filter(int port, input::Pad& p);  // GameInfo::pad: the game sees an idle pad while the screen is open

}  // namespace padmap
