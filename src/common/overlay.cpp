#include "common/overlay.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <iterator>

#include <imgui.h>
#include <windows.h>

#include "app/app.h"
#include "core/input.h"

namespace overlay {

bool show_fps = false;

namespace {

constexpr uint16_t kStart = 0x0010, kBack = 0x0020;  // XINPUT_GAMEPAD_START / BACK
constexpr uint8_t kA = 0, kB = 1;                    // indexes into Pad::analog
constexpr auto kHold = std::chrono::milliseconds(1500);

std::atomic<bool> g_dialog{false};
std::atomic<bool> g_pad_a{false}, g_pad_b{false};
std::atomic<long long> g_hold_since{0};  // ms of the steady clock, 0 when the combo is not held
std::atomic<bool> g_combo{false};        // held long enough, waiting for the UI to open the dialog

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

float Scale() { return std::clamp(ImGui::GetIO().DisplaySize.y / 720.0f, 1.0f, 3.0f); }

void ShowFps(double fps) {
    char text[32];
    std::snprintf(text, sizeof text, "%.0f FPS", fps);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    float size = ImGui::GetFontSize() * Scale() * 1.3f;
    ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    ImVec2 pos(ImGui::GetIO().DisplaySize.x - extent.x - 16 * Scale(), 10 * Scale());
    dl->AddText(font, size, ImVec2(pos.x + 1.5f, pos.y + 1.5f), IM_COL32(0, 0, 0, 220), text);
    dl->AddText(font, size, pos, IM_COL32(120, 255, 120, 240), text);
}

void Quit() { PostMessageW(static_cast<HWND>(app::Window()), WM_CLOSE, 0, 0); }

void Dialog() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.94f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoTitleBar;
    bool yes = false, no = false;
    if (ImGui::Begin("Quit game", nullptr, kFlags)) {
        ImGui::SetWindowFontScale(Scale() * 1.2f);
        ImGui::Text("Quit the game?");
        ImGui::TextDisabled("Unsaved progress is lost.");
        ImGui::Dummy(ImVec2(0, 6 * Scale()));
        yes = ImGui::Button("Quit  (Enter / Y / pad A)");
        ImGui::SameLine();
        no = ImGui::Button("Stay  (Esc / N / pad B)");
        yes |= ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
               ImGui::IsKeyPressed(ImGuiKey_Y, false) || g_pad_a.exchange(false);
        no |= ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_N, false) ||
              g_pad_b.exchange(false);
    }
    ImGui::End();
    if (yes) Quit();
    if (yes || no) g_dialog = false;
}

}  // namespace

void RequestQuit() {
    g_pad_a = g_pad_b = false;
    g_dialog = true;
}

void Ui(double fps) {
    if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) {
        show_fps = !show_fps;
        app::Save();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false) && !g_dialog) RequestQuit();
    if (g_combo.exchange(false) && !g_dialog) RequestQuit();
    if (show_fps) ShowFps(fps);
    if (g_dialog) Dialog();
}

void Filter(int, input::Pad& p) {
    bool combo = (p.buttons & (kStart | kBack)) == (kStart | kBack);
    if (g_dialog) {
        // edge of A and B for the dialog, the game sees nothing
        if (p.analog[kA] > 128) g_pad_a = true;
        if (p.analog[kB] > 128) g_pad_b = true;
        p.buttons = 0;
        std::fill(std::begin(p.analog), std::end(p.analog), uint8_t(0));
        p.lx = p.ly = p.rx = p.ry = 0;
        g_hold_since = 0;
        return;
    }
    if (!combo) {
        g_hold_since = 0;
        return;
    }
    long long now = NowMs(), since = g_hold_since.load();
    if (!since) g_hold_since = now;
    else if (now - since >= kHold.count()) {
        g_hold_since = 0;
        g_combo = true;
    }
}

}  // namespace overlay
