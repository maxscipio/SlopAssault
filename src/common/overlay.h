#pragma once

namespace input {
struct Pad;
}

// FPS counter (F10) and the quit dialog (F12, or Start + Back held on a controller)
namespace overlay {

extern bool show_fps;  // persisted in the settings file

void Ui(double fps);                   // inside the ImGui frame
void Filter(int port, input::Pad& p);  // guest threads: reads the quit combo, silences the pad while the dialog is up
void RequestQuit();                    // opens the quit dialog

}  // namespace overlay
