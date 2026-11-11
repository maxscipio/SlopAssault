#pragma once

#include <cstdint>

// graphics settings panel (F8) and the engine's frame rate limit
namespace graphics {

extern uint32_t game_fps;  // the engine's own frame limit, 30 as the console ran it; persisted in the settings file

// "Original" (F8): every quality enhancement off, the choices made before kept beside it and brought back when it is
// switched off; persisted in the settings file
extern bool original;
extern uint32_t kept[7];  // anti-aliasing, SSAO, soft shadows, shadow map size, anisotropic filtering, render %, game fps

void Ui(double fps);  // inside the ImGui frame
bool Open();

}  // namespace graphics
