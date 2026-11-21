#include "app/app.h"
#include <filesystem>
#include "common/overlay.h"

// MechAssault presents every second frame (interval 2) and advances its logic and audio per frame: 30 is the only correct cap
// "Fill the screen" is ultrawide: the picture is 21:9, with bars above and below on a screen that is narrower (16:9),
// and fills a screen that is 21:9 or wider. Picking the shape here keeps the SDK from stretching the targets, and the
// camera hook (src\game\hooks.cpp) widens the 3D view to app::Aspect().
static void OnInit() {
    if (!std::filesystem::exists(app::ExeDir() / "settings.toml")) app::Get().fps_cap = 30;
    constexpr float kUltrawide = 21.0f / 9.0f;
    const app::Settings& s = app::Get();
    if (s.aspect == 3 && !s.console_look) {
        app::Mode out = app::CachedOutput();
        if (!out.height || float(out.width) / float(out.height) < kUltrawide) app::g_aspect = kUltrawide;
    }
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
    .options = {{"show_fps", &overlay::show_fps, nullptr, "FPS counter", "Also F10."}},
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = OnInit,                       // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = overlay::Ui,                    // ImGui drawing each frame: the FPS counter (F10)
    .pad = nullptr,                       // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
    .settings_window = true,              // F1: the SDK's settings window (display, graphics, audio, controller, quit)
};
