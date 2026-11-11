#include "common/pad_mapping.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "app/app.h"
#include "common/overlay.h"
#include "core/input.h"
#include "core/log.h"

namespace padmap {

namespace {

enum class Kind { Button, Trigger, Stick };

struct Target {
    const char* label;
    const char* key;  // SDL's name for it in a mapping
    Kind kind;
};

// in the order they are asked for
constexpr Target kTargets[] = {
    {"A", "a", Kind::Button},
    {"B", "b", Kind::Button},
    {"X", "x", Kind::Button},
    {"Y", "y", Kind::Button},
    {"Black", "rightshoulder", Kind::Button},
    {"White", "leftshoulder", Kind::Button},
    {"Left trigger", "lefttrigger", Kind::Trigger},
    {"Right trigger", "righttrigger", Kind::Trigger},
    {"Start", "start", Kind::Button},
    {"Back", "back", Kind::Button},
    {"Left stick: press it in", "leftstick", Kind::Button},
    {"Right stick: press it in", "rightstick", Kind::Button},
    {"D-pad up", "dpup", Kind::Button},
    {"D-pad down", "dpdown", Kind::Button},
    {"D-pad left", "dpleft", Kind::Button},
    {"D-pad right", "dpright", Kind::Button},
    {"Left stick: push it RIGHT", "leftx", Kind::Stick},
    {"Left stick: push it DOWN", "lefty", Kind::Stick},
    {"Right stick: push it RIGHT", "rightx", Kind::Stick},
    {"Right stick: push it DOWN", "righty", Kind::Stick},
};
constexpr int kTargetCount = int(std::size(kTargets));
constexpr int kAxisThreshold = 16000;  // of 32767, how far an axis moves to count as pressed
constexpr int kQuietFrames = 20;       // the pad must rest this long before the next input is taken

// a physical input of the pad as SDL reads it
struct Source {
    enum class Type { None, Button, Hat, Axis } type = Type::None;
    int index = 0;
    int hat = 0;   // direction bit
    int dir = 0;   // axis: the way it moved, +1 or -1
    int rest = 0;  // axis: where it rested
};

std::string SdlText(const Source& s, Kind kind) {
    char buf[32] = "";
    switch (s.type) {
    case Source::Type::Button: std::snprintf(buf, sizeof buf, "b%d", s.index); break;
    case Source::Type::Hat: std::snprintf(buf, sizeof buf, "h%d.%d", s.index, s.hat); break;
    case Source::Type::Axis:
        if (kind == Kind::Stick)
            std::snprintf(buf, sizeof buf, "a%d%s", s.index, s.dir > 0 ? "" : "~");
        else if (kind == Kind::Trigger && std::abs(s.rest) > kAxisThreshold)
            // rests at an end: the whole range is the trigger
            std::snprintf(buf, sizeof buf, "a%d%s", s.index, s.rest < 0 ? "" : "~");
        else
            std::snprintf(buf, sizeof buf, "%ca%d", s.dir > 0 ? '+' : '-', s.index);
        break;
    default: break;
    }
    return buf;
}

std::string Describe(const Source& s) {
    char buf[48];
    switch (s.type) {
    case Source::Type::Button: std::snprintf(buf, sizeof buf, "button %d", s.index + 1); break;
    case Source::Type::Hat: {
        const char* d = s.hat == SDL_HAT_UP ? "up" : s.hat == SDL_HAT_DOWN ? "down" : s.hat == SDL_HAT_LEFT ? "left" : "right";
        std::snprintf(buf, sizeof buf, "hat %d %s", s.index + 1, d);
        break;
    }
    case Source::Type::Axis: std::snprintf(buf, sizeof buf, "axis %d %c", s.index + 1, s.dir > 0 ? '+' : '-'); break;
    default: return "skipped";
    }
    return buf;
}

// the pad's raw inputs, whether or not SDL knows the pad as a gamepad
struct Raw {
    SDL_Joystick* joy = nullptr;
    SDL_JoystickID id = 0;
    int axes = 0, buttons = 0, hats = 0;
    std::vector<int> rest;

    bool Open(SDL_JoystickID which) {
        Close();
        joy = SDL_OpenJoystick(which);
        if (!joy) return false;
        id = which;
        axes = std::max(SDL_GetNumJoystickAxes(joy), 0);
        buttons = std::max(SDL_GetNumJoystickButtons(joy), 0);
        hats = std::max(SDL_GetNumJoystickHats(joy), 0);
        rest.resize(axes);
        for (int a = 0; a < axes; a++) rest[a] = SDL_GetJoystickAxis(joy, a);
        return true;
    }
    void Close() {
        if (joy) SDL_CloseJoystick(joy);
        joy = nullptr;
    }
    // what the pad is doing now: a button first, then a hat, then the axis that moved furthest
    Source Active(Kind kind) const {
        Source s;
        if (!joy) return s;
        if (kind != Kind::Stick) {
            for (int b = 0; b < buttons; b++)
                if (SDL_GetJoystickButton(joy, b)) return {Source::Type::Button, b};
            for (int h = 0; h < hats; h++) {
                Uint8 v = SDL_GetJoystickHat(joy, h);
                for (int bit : {SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT})
                    if (v & bit) return {Source::Type::Hat, h, bit};
            }
        }
        int best = 0;
        for (int a = 0; a < axes; a++) {
            int v = SDL_GetJoystickAxis(joy, a);
            int d = std::abs(v - rest[a]);
            if (d > kAxisThreshold && d > best) {
                best = d;
                s = {Source::Type::Axis, a, 0, v > rest[a] ? 1 : -1, rest[a]};
            }
        }
        return s;
    }
};

enum class Mode { Main, Wizard, Review };

struct State {
    bool open = false;
    Mode mode = Mode::Main;
    SDL_JoystickID device = 0;
    Raw raw;
    SDL_Gamepad* test = nullptr;  // the gamepad view of the device, for the live test
    Source picked[kTargetCount];
    int step = 0;
    int quiet = 0;  // frames the pad has rested since the last input was taken
    double shown_at = -1;
    std::string note;
};

State g;
std::atomic<bool> g_open{false};

std::map<std::string, std::string> g_saved;     // guid, the full mapping line
std::map<std::string, std::string> g_original;  // guid, SDL's mapping before ours

std::string GuidText(SDL_JoystickID id) {
    char buf[64] = "";
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), buf, sizeof buf);
    return buf;
}

std::string NameOf(SDL_JoystickID id) {
    const char* n = SDL_GetJoystickNameForID(id);
    std::string s = n && *n ? n : "Controller";
    std::replace(s.begin(), s.end(), ',', ' ');
    return s;
}

std::filesystem::path File() { return app::ExeDir() / "controller_mapping.txt"; }

void Store() {
    std::ofstream f(File());
    for (auto& [guid, line] : g_saved) f << line << "\n";
}

void RememberOriginal(SDL_JoystickID id) {
    std::string guid = GuidText(id);
    if (g_original.count(guid) || !SDL_IsGamepad(id)) return;
    if (char* m = SDL_GetGamepadMappingForID(id)) {
        g_original[guid] = m;
        SDL_free(m);
    }
}

std::vector<SDL_JoystickID> Devices() {
    std::vector<SDL_JoystickID> v;
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&count)) {
        v.assign(ids, ids + count);
        SDL_free(ids);
    }
    return v;
}

// "name,a:b0,..."; SDL wants a guid in front of it
std::string MappingBody(SDL_JoystickID id) {
    std::string s = NameOf(id) + ",";
    for (int i = 0; i < kTargetCount; i++) {
        if (g.picked[i].type == Source::Type::None) continue;
        s += std::string(kTargets[i].key) + ":" + SdlText(g.picked[i], kTargets[i].kind) + ",";
    }
    return s + "platform:Windows,";
}

void Apply() {
    std::string guid = GuidText(g.device), body = MappingBody(g.device);
    RememberOriginal(g.device);
    // SDL parses "<guid>,<name>,<mapping>" here as well and ignores the first field
    if (!SDL_SetGamepadMapping(g.device, (guid + "," + body).c_str())) LOGE("padmap: SDL refused the mapping: %s", SDL_GetError());
    g_saved[guid] = guid + "," + body;
    Store();
    LOGR("padmap: saved for %s: %s", guid.c_str(), body.c_str());
}

void Restore() {
    std::string guid = GuidText(g.device);
    g_saved.erase(guid);
    Store();
    auto it = g_original.find(guid);
    if (it != g_original.end()) {
        SDL_AddGamepadMapping(it->second.c_str());
        g.note = "Default mapping restored.";
    } else {
        g.note = "Saved mapping removed. Restart the game to get the default one back.";
    }
    LOGR("padmap: reset %s", guid.c_str());
}

void CloseDevice() {
    g.raw.Close();
    if (g.test) SDL_CloseGamepad(g.test);
    g.test = nullptr;
}

void StartWizard() {
    if (!g.raw.Open(g.device)) {
        g.note = std::string("Cannot read this controller: ") + SDL_GetError();
        return;
    }
    for (Source& s : g.picked) s = {};
    g.mode = Mode::Wizard;
    g.step = 0;
    g.quiet = 0;
    g.note.clear();
}

void Finish() {
    g.raw.Close();
    g.mode = Mode::Main;
}

float Scale() { return std::clamp(ImGui::GetIO().DisplaySize.y / 720.0f, 1.0f, 3.0f); }

void Wizard() {
    const Target& t = kTargets[g.step];
    ImGui::Text("Step %d of %d", g.step + 1, kTargetCount);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8 * Scale()));
    ImGui::SetWindowFontScale(Scale() * 1.5f);
    ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "%s", t.label);
    ImGui::SetWindowFontScale(Scale());
    ImGui::Dummy(ImVec2(0, 8 * Scale()));
    ImGui::TextWrapped(t.kind == Kind::Stick ? "Push the stick all the way in that direction on your controller."
                                              : "Press it on your controller.");
    if (g.picked[g.step].type != Source::Type::None)
        ImGui::Text("Got: %s", Describe(g.picked[g.step]).c_str());
    ImGui::Dummy(ImVec2(0, 8 * Scale()));
    ImGui::TextDisabled("Space: skip    Backspace: back    Esc: cancel");

    Source now = g.raw.Active(t.kind);
    if (now.type == Source::Type::None) {
        g.quiet++;
        return;
    }
    if (g.quiet < kQuietFrames) return;  // still the last input
    g.picked[g.step] = now;
    g.quiet = 0;
    if (++g.step == kTargetCount) g.mode = Mode::Review;
}

void Review() {
    ImGui::Text("All set. This is what you pressed:");
    ImGui::Separator();
    if (ImGui::BeginTable("map", 2)) {
        for (int i = 0; i < kTargetCount; i++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(kTargets[i].label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(Describe(g.picked[i]).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Enter: save    Backspace: go back over the last step    Esc: discard");
}

void Bar(const char* label, float v) {  // v from -1 to 1
    ImGui::ProgressBar(v * 0.5f + 0.5f, ImVec2(160 * Scale(), 0), "");
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
}

void Test() {
    if (!g.test && SDL_IsGamepad(g.device)) g.test = SDL_OpenGamepad(g.device);
    if (g.test && !SDL_GamepadConnected(g.test)) {
        SDL_CloseGamepad(g.test);
        g.test = nullptr;
    }
    if (!g.test) {
        ImGui::TextWrapped("The game does not see this controller yet. Press M to map it.");
        return;
    }
    ImGui::TextDisabled("What the game sees (press things to test):");
    constexpr struct {
        const char* name;
        SDL_GamepadButton button;
    } kButtons[] = {{"A", SDL_GAMEPAD_BUTTON_SOUTH},          {"B", SDL_GAMEPAD_BUTTON_EAST},
                    {"X", SDL_GAMEPAD_BUTTON_WEST},           {"Y", SDL_GAMEPAD_BUTTON_NORTH},
                    {"Black", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {"White", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
                    {"Start", SDL_GAMEPAD_BUTTON_START},      {"Back", SDL_GAMEPAD_BUTTON_BACK},
                    {"L-stick", SDL_GAMEPAD_BUTTON_LEFT_STICK}, {"R-stick", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
                    {"Up", SDL_GAMEPAD_BUTTON_DPAD_UP},       {"Down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
                    {"Left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},   {"Right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT}};
    int n = 0;
    for (auto& b : kButtons) {
        bool down = SDL_GetGamepadButton(g.test, b.button);
        ImGui::TextColored(down ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1), "[%s]", b.name);
        if (++n % 7) ImGui::SameLine();
    }
    auto axis = [&](SDL_GamepadAxis a) { return std::clamp(SDL_GetGamepadAxis(g.test, a) / 32767.0f, -1.0f, 1.0f); };
    Bar("Left trigger", axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) * 2 - 1);
    Bar("Right trigger", axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) * 2 - 1);
    Bar("Left stick X", axis(SDL_GAMEPAD_AXIS_LEFTX));
    Bar("Left stick Y", axis(SDL_GAMEPAD_AXIS_LEFTY));
    Bar("Right stick X", axis(SDL_GAMEPAD_AXIS_RIGHTX));
    Bar("Right stick Y", axis(SDL_GAMEPAD_AXIS_RIGHTY));
}

void Main() {
    std::vector<SDL_JoystickID> devices = Devices();
    if (devices.empty()) {
        ImGui::TextWrapped("No controller found. Plug in a USB controller.");
        g.device = 0;
        CloseDevice();
        return;
    }
    if (std::find(devices.begin(), devices.end(), g.device) == devices.end()) {
        CloseDevice();
        g.device = devices[0];
    }
    RememberOriginal(g.device);

    std::string current = NameOf(g.device) + (SDL_IsGamepad(g.device) ? "" : "  (not recognized)");
    if (ImGui::BeginCombo("Controller", current.c_str())) {
        for (SDL_JoystickID id : devices)
            if (ImGui::Selectable((NameOf(id) + "##" + std::to_string(id)).c_str(), id == g.device) && id != g.device) {
                CloseDevice();
                g.device = id;
                g.note.clear();
            }
        ImGui::EndCombo();
    }
    ImGui::Dummy(ImVec2(0, 4 * Scale()));
    if (ImGui::Button("Map this controller (M)")) StartWizard();
    ImGui::SameLine();
    if (ImGui::Button("Restore default (R)")) Restore();
    ImGui::SameLine();
    if (ImGui::Button("Close (Esc)")) g.open = false;
    ImGui::SameLine();
    if (ImGui::Button("Quit game")) {
        g.open = false;
        overlay::RequestQuit();
    }
    if (!g.note.empty()) ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "%s", g.note.c_str());
    ImGui::Separator();
    Test();

    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) StartWizard();
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) Restore();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) g.open = false;
}

}  // namespace

void Load() {
    std::ifstream f(File());
    for (std::string line; std::getline(f, line);) {
        size_t comma = line.find(',');
        if (comma == std::string::npos) continue;
        g_saved[line.substr(0, comma)] = line;
    }
    // before ours are applied, so Restore can put the default back
    for (SDL_JoystickID id : Devices()) {
        RememberOriginal(id);
        char* m = SDL_IsGamepad(id) ? SDL_GetGamepadMappingForID(id) : nullptr;
        LOGR("padmap: controller \"%s\" guid %s, %s", NameOf(id).c_str(), GuidText(id).c_str(),
             m ? m : "not recognized as a gamepad");
        SDL_free(m);
    }
    for (auto& [guid, line] : g_saved) {
        int r = SDL_AddGamepadMapping(line.c_str());
        LOGR("padmap: mapping for %s %s", guid.c_str(), r >= 0 ? "applied" : SDL_GetError());
    }
}

void Ui(double) {
    ImGuiIO& io = ImGui::GetIO();
    if (g.shown_at < 0) g.shown_at = ImGui::GetTime();
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) {
        g.open = !g.open;
        g.mode = Mode::Main;
        g.note.clear();
    }
    if (!g.open) {
        if (g_open) {
            CloseDevice();
            g_open = false;
        }
        if (ImGui::GetTime() - g.shown_at < 8)
            ImGui::GetForegroundDrawList()->AddText(ImVec2(12, 8), IM_COL32(255, 255, 255, 200),
                                                    "F8: graphics    F9: controller setup    F10: FPS    F12: quit");
        return;
    }
    g_open = true;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.94f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
    bool open = true;
    if (ImGui::Begin("Controller setup", &open, kFlags)) {
        ImGui::SetWindowFontScale(Scale());
        ImGui::PushItemWidth(260 * Scale());
        switch (g.mode) {
        case Mode::Main: Main(); break;
        case Mode::Wizard: Wizard(); break;
        case Mode::Review: Review(); break;
        }
        ImGui::PopItemWidth();

        if (g.mode == Mode::Wizard) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) Finish();
            else if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
                g.picked[g.step] = {};
                g.quiet = 0;
                if (++g.step == kTargetCount) g.mode = Mode::Review;
            } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && g.step > 0) {
                g.step--;
                g.quiet = 0;
            }
        } else if (g.mode == Mode::Review) {
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
                Apply();
                g.note = "Saved.";
                g.mode = Mode::Main;
            } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
                g.step = kTargetCount - 1;
                g.mode = Mode::Wizard;
                g.quiet = 0;
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                Finish();
            }
        }
    }
    ImGui::End();
    if (!open) g.open = false;
}

void Filter(int, input::Pad& p) {
    if (!g_open) return;
    p.buttons = 0;
    std::fill(std::begin(p.analog), std::end(p.analog), uint8_t(0));
    p.lx = p.ly = p.rx = p.ry = 0;
}

}  // namespace padmap

