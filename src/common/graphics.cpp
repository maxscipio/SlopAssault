#include "common/graphics.h"

#include "common/enhance.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>

#include <imgui.h>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace graphics {

uint32_t game_fps = 30;
bool original = false;
uint32_t kept[7] = {0, 0, 0, 0, 0, 100, 30};

namespace {

// The engine's frame timer (a global pointer in the game's data): +0x24 is the frame rate limit, +0x08 the
// shortest frame in seconds, +0x20 the time carried over. The game sets 30 for NTSC (25 for PAL) at start and its
// own `maxfps` console command rewrites exactly these fields.
constexpr uint32_t kTimerPtr = 0;  // no override: this engine has no frame limiter to rewrite (see docs\PORTING_NOTES.md)
constexpr uint32_t kMaxFps = 0x24, kFrameTime = 0x08, kCarry = 0x20;
constexpr uint32_t kGuestEnd = 0x04000000;  // the guest's 64 MB

bool g_open = false;

float Scale() { return std::clamp(ImGui::GetIO().DisplaySize.y / 720.0f, 1.0f, 3.0f); }

void ApplyGameFps() {
    if (!kTimerPtr) return;
    uint32_t want = std::clamp<uint32_t>(game_fps, 15, 240);
    uint32_t timer = kernel::R32(kTimerPtr);
    if (timer < 0x10000 || timer >= kGuestEnd - 0x40) return;
    if (kernel::R32(timer + kMaxFps) == want) return;
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOGR("graphics: engine frame limit %u -> %u (timer at %08X)", kernel::R32(timer + kMaxFps), want, timer);
    }
    float frame = 1.0f / float(want);
    uint32_t bits;
    std::memcpy(&bits, &frame, 4);
    kernel::W32(timer + kMaxFps, want);
    kernel::W32(timer + kFrameTime, bits);
    kernel::W32(timer + kCarry, 0);
}

struct Choice {
    uint32_t value;
    const char* label;
};

bool Pick(const char* label, uint32_t& v, const Choice* c, size_t n) {
    char custom[32];
    const char* cur = nullptr;
    for (size_t i = 0; i < n; i++)
        if (c[i].value == v) cur = c[i].label;
    if (!cur) {
        std::snprintf(custom, sizeof custom, "Custom (%u)", v);
        cur = custom;
    }
    bool changed = false;
    if (ImGui::BeginCombo(label, cur)) {
        for (size_t i = 0; i < n; i++) {
            bool selected = c[i].value == v;
            if (ImGui::Selectable(c[i].label, selected)) {
                v = c[i].value;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void Tip(const char* text) {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}

constexpr Choice kModes[] = {{uint32_t(app::DisplayMode::Fullscreen), "Fullscreen"},
                             {uint32_t(app::DisplayMode::Borderless), "Borderless window"},
                             {uint32_t(app::DisplayMode::Windowed), "Window"}};
constexpr Choice kFpsCaps[] = {{0, "Unlimited"}, {app::kFpsCapRefresh, "Monitor refresh rate"}, {30, "30"},
                               {60, "60"},       {90, "90"},                                    {120, "120"},
                               {144, "144"},     {165, "165"},                                  {240, "240"}};
constexpr Choice kAniso[] = {{0, "As the game asks"}, {2, "2x"}, {4, "4x"}, {8, "8x"}, {16, "16x"}};

struct Resolution {
    uint32_t w, h;
    const char* label;
};
constexpr Resolution kResolutions[] = {{0, 0, "Desktop"},        {1280, 720, "1280 x 720"},   {1366, 768, "1366 x 768"},
                                       {1600, 900, "1600 x 900"}, {1920, 1080, "1920 x 1080"}, {2560, 1440, "2560 x 1440"},
                                       {3840, 2160, "3840 x 2160"}};

void SetOriginal(app::Settings& s, bool on) {
    if (on) {
        kept[0] = enhance::aa;
        kept[1] = enhance::ssao;
        kept[2] = enhance::soft_shadows;
        kept[3] = enhance::shadow_scale;
        kept[4] = s.aniso;
        kept[5] = s.render_percent;
        kept[6] = game_fps;
        enhance::aa = enhance::ssao = enhance::soft_shadows = false;
        enhance::shadow_scale = 0;
        s.aniso = 0;
        s.render_percent = 100;
        s.fps_cap = 30;
        game_fps = 30;
    } else {
        enhance::aa = kept[0] != 0;
        enhance::ssao = kept[1] != 0;
        enhance::soft_shadows = kept[2] != 0;
        enhance::shadow_scale = kept[3];
        s.aniso = kept[4];
        s.render_percent = kept[5] ? kept[5] : 100;
        game_fps = kept[6] ? kept[6] : 30;
    }
    original = on;
}

void Defaults(app::Settings& s) {
    original = false;
    s.display_mode = app::DisplayMode::Borderless;
    s.width = s.height = 0;
    s.render_percent = 100;
    s.fps_cap = 30;
    s.vsync = true;
    s.aniso = 0;
    game_fps = 30;
    enhance::aa = enhance::ssao = enhance::soft_shadows = false;
    enhance::ssao_strength = 100;
    enhance::ssao_radius = 1000;
    enhance::shadow_scale = 0;
}

void Panel(double fps) {
    app::Settings& s = app::Get();
    bool save = false, display = false;

    if (ImGui::Checkbox("Original rendering (no enhancements)", &original)) {
        bool on = original;
        original = !on;
        SetOriginal(s, on);
        save = display = true;
    }
    Tip("The game as the console drew it: no anti-aliasing, ambient occlusion, anisotropic filtering or render scale.\n"
        "Your other choices are kept and come back when this is switched off.");

    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
        uint32_t mode = uint32_t(s.display_mode);
        if (Pick("Window mode", mode, kModes, std::size(kModes))) {
            s.display_mode = app::DisplayMode(mode);
            save = display = true;
        }
        const char* res = "Custom";
        char custom[32];
        for (const Resolution& r : kResolutions)
            if (r.w == s.width && r.h == s.height) res = r.label;
        if (res[0] == 'C') {
            std::snprintf(custom, sizeof custom, "Custom (%u x %u)", s.width, s.height);
            res = custom;
        }
        if (ImGui::BeginCombo("Resolution", res)) {
            for (const Resolution& r : kResolutions) {
                bool selected = r.w == s.width && r.h == s.height;
                if (ImGui::Selectable(r.label, selected)) {
                    s.width = r.w;
                    s.height = r.h;
                    save = display = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(original);
        int pct = int(s.render_percent);
        if (ImGui::SliderInt("Render scale", &pct, 25, 200, "%d%%")) s.render_percent = uint32_t(pct);
        if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
        ImGui::EndDisabled();
        Tip("The 3D picture's size against the output. Under 100% is faster, over 100% is supersampled.");
        app::Mode out = app::Output();
        ImGui::TextDisabled("Renders %u lines, shown at %u x %u", unsigned(out.height * s.render_percent / 100), out.width,
                            out.height);
        save |= ImGui::Checkbox("Vertical sync", &s.vsync);
    }

    if (ImGui::CollapsingHeader("Frame rate", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (Pick("Display limit", s.fps_cap, kFpsCaps, std::size(kFpsCaps))) save = true;
        Tip("The most frames shown per second. MechAssault steps its game logic and sound once per frame shown, at 30,\n"
            "so anything above 30 speeds the game up and distorts the sound until the engine's frame timer is found.");
        ImGui::TextDisabled("Now %.0f FPS", fps);
    }

    if (ImGui::CollapsingHeader("Image quality", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginDisabled(original);
        if (Pick("Anisotropic filtering", s.aniso, kAniso, std::size(kAniso))) save = true;
        Tip("Sharper textures at a slant, such as the ground and walls seen from the side.");
        save |= ImGui::Checkbox("Anti-aliasing (FXAA)", &enhance::aa);
        Tip("Smooths jagged edges on the finished picture. Costs little.");
        save |= ImGui::Checkbox("Ambient occlusion (SSAO)", &enhance::ssao);
        Tip("Darkens creases and where objects meet the ground. Costs some speed.");
        if (enhance::ssao) {
            ImGui::Indent();
            int strength = int(enhance::ssao_strength), radius = int(enhance::ssao_radius);
            if (ImGui::SliderInt("Strength", &strength, 25, 300, "%d%%")) enhance::ssao_strength = uint32_t(strength);
            if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
            if (ImGui::SliderInt("Radius", &radius, 200, 2500, "%d")) enhance::ssao_radius = uint32_t(radius);
            if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
            Tip("How far from a surface the occlusion reaches, in 1/25 of a world unit.");
            ImGui::Unindent();
        }
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Other", ImGuiTreeNodeFlags_DefaultOpen)) {
        save |= ImGui::Checkbox("Shader cache (applies at the next start)", &s.shader_cache);
        Tip("Keeps compiled shaders on disk, so the game compiles each one once, not at every start.");
    }

    ImGui::Dummy(ImVec2(0, 4 * Scale()));
    if (ImGui::Button("Defaults")) {
        Defaults(s);
        save = display = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close (F8 / Esc)")) g_open = false;

    if (display) app::ApplyDisplay();
    if (save) app::Save();
}

}  // namespace

bool Open() { return g_open; }

void Ui(double fps) {
    ApplyGameFps();
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false)) g_open = !g_open;
    if (!g_open) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        g_open = false;
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.94f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
    bool open = true;
    if (ImGui::Begin("Graphics", &open, kFlags)) {
        ImGui::SetWindowFontScale(Scale());
        ImGui::PushItemWidth(240 * Scale());
        Panel(fps);
        ImGui::PopItemWidth();
    }
    ImGui::End();
    if (!open) g_open = false;
}

}  // namespace graphics
