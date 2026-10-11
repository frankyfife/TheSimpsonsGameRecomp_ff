// simpsons - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

namespace rex::ui {
class ImGuiDrawer;
class Window;
}

// freecam.cpp: the free camera's key binds and keyboard listener.
void InitFreecam(rex::ui::Window* window);
void ShutdownFreecam(rex::ui::Window* window);

// character_select.cpp: the in-game character menu.
void InitCharacterMenu(rex::ui::ImGuiDrawer* drawer);
void ShutdownCharacterMenu();

class SimpsonsApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SimpsonsApp>(new SimpsonsApp(ctx, "simpsons",
        PPCImageConfig));
  }

  void OnPostSetup() override { InitFreecam(window()); }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override { InitCharacterMenu(drawer); }
  void OnShutdown() override {
    ShutdownCharacterMenu();
    ShutdownFreecam(window());
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
