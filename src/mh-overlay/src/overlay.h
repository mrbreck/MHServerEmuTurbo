#pragma once

// Install the D3D11 Present hook and initialise ImGui.
// Call once from the DLL worker thread after the game window is up.
bool InstallOverlay();

// Tear everything down cleanly on DLL detach.
void RemoveOverlay();
