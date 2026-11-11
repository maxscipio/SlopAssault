#include "app/app.h"
#include <filesystem>
#include "common/enhance.h"
#include "common/graphics.h"
#include "common/overlay.h"
#include "common/pad_mapping.h"

static void OnWindow(void*) { padmap::Load(); }

static void OnUi(double fps) {
    graphics::Ui(fps);
    padmap::Ui(fps);
    overlay::Ui(fps);
}

static void OnPad(int port, input::Pad& p) {
    overlay::Filter(port, p);
    padmap::Filter(port, p);
}

// MechAssault presents every second frame (interval 2) and advances its logic and audio per frame: 30 is the only correct cap
static void OnInit() {
    if (!std::filesystem::exists(app::ExeDir() / "settings.toml")) app::Get().fps_cap = 30;
}

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"MechAssault",              // window and splash
    .short_title = L"MechAssault",        // message boxes
    .name = L"MechAssault",               // window classes, crash dumps, handover event
    .log = "ma",                             // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "MechAssault"}},  // each recompiled xbe and its exe, for title launches
    .options = {{"show_fps", &overlay::show_fps, nullptr},        // F10 toggles the FPS counter
                {"game_fps", nullptr, &graphics::game_fps},       // the engine's frame limit (F8 panel)
                {"ssao", &enhance::ssao, nullptr},
                {"ssao_strength", nullptr, &enhance::ssao_strength},
                {"ssao_radius", nullptr, &enhance::ssao_radius},
                {"ssao_debug", nullptr, &enhance::ssao_debug},
                {"anti_aliasing", &enhance::aa, nullptr},
                {"soft_shadows", &enhance::soft_shadows, nullptr},
                {"shadow_scale", nullptr, &enhance::shadow_scale},
                {"original_rendering", &graphics::original, nullptr},
                {"kept_anti_aliasing", nullptr, &graphics::kept[0]},
                {"kept_ssao", nullptr, &graphics::kept[1]},
                {"kept_soft_shadows", nullptr, &graphics::kept[2]},
                {"kept_shadow_scale", nullptr, &graphics::kept[3]},
                {"kept_anisotropic", nullptr, &graphics::kept[4]},
                {"kept_render_percent", nullptr, &graphics::kept[5]},
                {"kept_game_fps", nullptr, &graphics::kept[6]}},
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = OnInit,                       // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = OnWindow,                   // the game window is up: applies the saved controller mappings
    .message = nullptr,                   // window messages first; true takes one
    .ui = OnUi,                           // ImGui drawing each frame: graphics (F8), controller setup (F9), FPS (F10), quit (F12)
    .pad = OnPad,                         // changes a polled pad: quit combo, silent while a screen is open
    .rumble = nullptr,                    // changes a rumble before it is set
};
