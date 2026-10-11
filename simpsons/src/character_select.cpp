// Character select: other characters for players 1 and 2, chosen in the
// launcher (character_player1/2) or in game, from where the level restarts at
// the last checkpoint with them.
//
// A level spawns its playable characters from four player slots. When a map
// is entered, the GameFlowManager (singleton at 0x82D08BA8) collects the
// slots' overrides (Lua map:SetPlayerOverride or level data); every time the
// level loads, including a restart from a checkpoint, which reloads the level
// without entering the map again, sub_8289E428 resolves them into one
// character template GUID per slot at GameFlowManager+0x354 + slot * 16. An
// invalid GUID there means the checkpoint locator's default character. The
// level loader then streams each slot's template by the GUID's first word,
// the template's stream ID in the character StreamTOC
// (simpsons_chars_global.str), and spawns it at the slot's locator. After
// sub_8289E428 the chosen template's GUID replaces the resolved one; the
// game's own lists (the overrides, the hub's controlled-player array that
// goes into the savegame) are left alone, so "the level's character" needs no
// undo, only the next load.
//
// The template GUIDs come from the character streams in
// gamedata/simpsons_chars/globalfolder/chars: each stream's object table holds
// entries {stream ID, three GUID words}, and the template's own entry is the
// second object after one all templates share (4CF93835 D99B1987 500A10FE),
// which matches the GUIDs the game's Lua uses (Lisa in Grand Theft Scratchy,
// Bart and Homer in Medal of Homer).
//
// The in-game menu opens with F7, or with Y while the game's pause menu is
// open (the controller is read in freecam.cpp's XInputGetState hook). It
// pauses the level while open. Confirming closes the pause menu (an injected
// B) and restarts from the last checkpoint once the level runs again, through
// sub_8289BEA8, which the pause menu's restart and a death also end in, with
// the GameFlowManager's restart-pending flag set first as they do. The restart
// restores the snapshot the last checkpoint took, so progress since then is
// lost, as after a death.
//
// Levels are scripted for their own characters: with others, triggers that
// wait for a particular character or power, objectives and cutscenes may not
// fire (reported in play), so a level can get stuck. That is in each level's
// data, not fixable here; the menu and the launcher say so.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ppc.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/overlay_input.h>

REXCVAR_DEFINE_STRING(character_player1, "", "Game",
                      "Character template for player 1 (e.g. homer_fh1_gh1_hh1_h2), empty = the "
                      "level's own; the in-game menu (F7, or Y in the pause menu) changes it");
REXCVAR_DEFINE_STRING(character_player2, "", "Game",
                      "Character template for player 2, empty = the level's own");

// ResolvePlayers: r3 = the GameFlowManager.
REX_EXTERN(sub_8289E428);
REX_EXTERN(__imp__sub_8289E428);
// Restarts the main loop from the last checkpoint snapshot: r3 = the
// GameFlowManager, r4 = 1.
REX_EXTERN(sub_8289BEA8);
// The game's pause with a reason bit in r3, and its release.
REX_EXTERN(sub_826913A0);
REX_EXTERN(sub_82691368);

struct CharacterTemplate {
  const char* name;
  uint32_t stream_id;
  uint32_t words[3];
};

namespace {

constexpr CharacterTemplate kTemplates[] = {
    {"bart_bc0_grp0_ss1_h0", 0x41927909, {0x4B8036DD, 0xB12AD298, 0xE1EA669F}},
    {"bart_bc1_grp0_ss1_h0", 0xE068E7B3, {0x4FBD4A07, 0x21660D9A, 0x2C34855A}},
    {"bart_bc1_grp1_ss1_h0", 0xF9296E99, {0x4C9AC4D9, 0xD871579B, 0xA0B22EF5}},
    {"bart_bc1_grp1_ss1_h1", 0x83FDB761, {0x4A48D75F, 0xA4F93AB9, 0x92349FE0}},
    {"bart_bc1_grp1_ss1_h2", 0x8E0BCC61, {0x42243136, 0x6558E7AB, 0x8A7C0B19}},
    {"bart_bc2_grp1_ss2_h2", 0xE663B19D, {0x4EC14F65, 0x74F798BA, 0x90438F13}},
    {"bart_bc2_grp1_ss2_h2_moh", 0xEA5831BB, {0x439964FC, 0x32BC6CAA, 0xAE0EE5B6}},
    {"bart_bc2_grp1_ss2_h2_moh_drk", 0xFE561E14, {0x45816CE5, 0x53026B99, 0x64693FB1}},
    {"bart_bc2_grp1_ss2_h2_mtp", 0xA0AF582E, {0x4AAC4CFC, 0x1A517DB0, 0x128E6219}},
    {"homer_fh0_gh0_hh0_h0", 0xDA4E8185, {0x446C0842, 0x240343A5, 0x00085096}},
    {"homer_fh1_gh0_hh0_h0", 0x9CF7AFAD, {0x46135447, 0x91CA0CB9, 0x5EA46840}},
    {"homer_fh1_gh0_hh1_h1", 0xD9B67B21, {0x411743DB, 0x4E311C80, 0x6BFF16C1}},
    {"homer_fh1_gh0_hh1_h1_sotcd", 0xB78227F4, {0x4F697FFE, 0x6C8C67A0, 0xAF8F5E47}},
    {"homer_fh1_gh0_hh1_h2", 0x41E4761E, {0x409E1D32, 0x4B652BAE, 0x648CD1F4}},
    {"homer_fh1_gh1_hh1_h2", 0x08010E79, {0x4E93A21D, 0x716EE39F, 0x09D7A091}},
    {"homer_fh1_gh1_hh1_h2_bshff", 0x70194DB4, {0x453E6B97, 0x085B0089, 0xF748F9A9}},
    {"homer_fh1_gh1_hh1_h2_moh", 0xE4B61F47, {0x4E9B3042, 0xF6553D8D, 0x411F2AE1}},
    {"homer_fh1_gh1_hh1_h2_moh_drk", 0xFC230B54, {0x4C10227B, 0xCDE9E981, 0x1B7D7B38}},
    {"homer_fh1_gh1_hh1_h2_mtp", 0x2B042AED, {0x401D9209, 0x12F48686, 0xBC6602EB}},
    {"homer_fh1_gh1_hh1_h2_nq", 0x1AD3CC79, {0x46DFC91B, 0xAC2FCAB9, 0xD50A45D1}},
    {"lisa_sx0_hog0_h0", 0x6A0511F7, {0x4EC9F49E, 0x5E3342BC, 0x3FF10592}},
    {"lisa_sx1_hog1_h0", 0x7A2C63B2, {0x4AF53FF1, 0xB01EDDB0, 0x0960AD47}},
    {"lisa_sx2_hog1_h0", 0xE5C1CE15, {0x4605E810, 0x1A01DDB0, 0xAEE0D021}},
    {"lisa_sx2_hog2_h0", 0x206EF848, {0x4B13969A, 0x69280482, 0x1B4D3600}},
    {"lisa_sx2_hog2_h1", 0x109897D5, {0x4283F857, 0x98BC1ABC, 0x4031529B}},
    {"lisa_sx2_hog2_h2", 0xCCF0B7F8, {0x4399973B, 0x7E0FC79C, 0x6E74B86B}},
    {"lisa_sx2_hog3_h2", 0x341862F1, {0x4D2D5318, 0x0C5F69AE, 0x8A7525ED}},
    {"lisa_sx2_hog3_h2_bshff", 0xCD1BCDD9, {0x4991877C, 0x9E7C25B5, 0xF8D29B1A}},
    {"lisa_sx2_hog3_h2_mtp", 0x0560B1DD, {0x44E45C30, 0x5A713DBA, 0xAB80319C}},
    {"marge_mm0_h0", 0x1A9F39F8, {0x4C688BEC, 0xFAE3EFA2, 0x8EB67053}},
    {"marge_mm1_h0", 0xC0CAC4A2, {0x4C775154, 0x1EB8089D, 0x12BCC752}},
    {"marge_mm1_h1", 0xEBB0DDE3, {0x463DE9E5, 0x27A5E68E, 0xE02B8370}},
    {"marge_mm1_h2", 0x67A63D86, {0x469F9E30, 0x560CE5AE, 0x23708863}},
    {"marge_mm2_h2", 0xCFC173AC, {0x4463A2AC, 0xA6C646BD, 0x9F506FE5}},
    {"marge_mm2_h2_gts", 0x3ADD3EF5, {0x4F7A1D79, 0x1838CA89, 0x2BE350F6}},
    {"marge_mm2_h2_mtp", 0x1DBE1259, {0x4B254C87, 0xC520D581, 0x7BA7FDDB}},
    {"marge_mm2_h2_nq", 0xEBA72200, {0x44A79575, 0xC786AA81, 0x391BFDED}},
};

// The menu's choices, in the launcher's order: each character with all
// powers, the episode costumes, then the earlier power stages of the story.
struct Choice {
  const char* name;  // template, "" = the level's character
  const char* label;
};
constexpr Choice kChoices[] = {
    {"", "The level's character"},
    {"homer_fh1_gh1_hh1_h2", "Homer"},
    {"homer_fh1_gh1_hh1_h2_bshff", "Homer - Big Super Happy Fun Fun"},
    {"homer_fh1_gh1_hh1_h2_moh", "Homer - Medal of Homer"},
    {"homer_fh1_gh1_hh1_h2_moh_drk", "Homer - Medal of Homer (dark)"},
    {"homer_fh1_gh1_hh1_h2_nq", "Homer - NeverQuest"},
    {"homer_fh1_gh1_hh1_h2_mtp", "Homer - Meet Thy Player"},
    {"homer_fh1_gh0_hh1_h1_sotcd", "Homer - The Colossal Donut"},
    {"homer_fh0_gh0_hh0_h0", "Homer, stage 1 of 5"},
    {"homer_fh1_gh0_hh0_h0", "Homer, stage 2 of 5"},
    {"homer_fh1_gh0_hh1_h1", "Homer, stage 3 of 5"},
    {"homer_fh1_gh0_hh1_h2", "Homer, stage 4 of 5"},
    {"marge_mm2_h2", "Marge"},
    {"marge_mm2_h2_gts", "Marge - Grand Theft Scratchy"},
    {"marge_mm2_h2_nq", "Marge - NeverQuest"},
    {"marge_mm2_h2_mtp", "Marge - Meet Thy Player"},
    {"marge_mm0_h0", "Marge, stage 1 of 5"},
    {"marge_mm1_h0", "Marge, stage 2 of 5"},
    {"marge_mm1_h1", "Marge, stage 3 of 5"},
    {"marge_mm1_h2", "Marge, stage 4 of 5"},
    {"bart_bc2_grp1_ss2_h2", "Bart"},
    {"bart_bc2_grp1_ss2_h2_moh", "Bart - Medal of Homer"},
    {"bart_bc2_grp1_ss2_h2_moh_drk", "Bart - Medal of Homer (dark)"},
    {"bart_bc2_grp1_ss2_h2_mtp", "Bart - Meet Thy Player"},
    {"bart_bc0_grp0_ss1_h0", "Bart, stage 1 of 6"},
    {"bart_bc1_grp0_ss1_h0", "Bart, stage 2 of 6"},
    {"bart_bc1_grp1_ss1_h0", "Bart, stage 3 of 6"},
    {"bart_bc1_grp1_ss1_h1", "Bart, stage 4 of 6"},
    {"bart_bc1_grp1_ss1_h2", "Bart, stage 5 of 6"},
    {"lisa_sx2_hog3_h2", "Lisa"},
    {"lisa_sx2_hog3_h2_bshff", "Lisa - Big Super Happy Fun Fun"},
    {"lisa_sx2_hog3_h2_mtp", "Lisa - Meet Thy Player"},
    {"lisa_sx0_hog0_h0", "Lisa, stage 1 of 7"},
    {"lisa_sx1_hog1_h0", "Lisa, stage 2 of 7"},
    {"lisa_sx2_hog1_h0", "Lisa, stage 3 of 7"},
    {"lisa_sx2_hog2_h0", "Lisa, stage 4 of 7"},
    {"lisa_sx2_hog2_h1", "Lisa, stage 5 of 7"},
    {"lisa_sx2_hog2_h2", "Lisa, stage 6 of 7"},
};
constexpr int kChoiceCount = int(std::size(kChoices));

constexpr uint32_t kResolvedPlayers = 0x354;  // 4 slots x 16-byte GUID
constexpr int kAllSlots = 4;
constexpr int kSlots = 2;  // players 1 and 2

constexpr uint32_t kGameFlowManager = 0x82D08BA8;
constexpr uint32_t kFlowEpisode = 160;
constexpr uint32_t kFlowMap = 172;
constexpr uint32_t kFlowFlags = 1200;
constexpr uint32_t kFlowRestartPending = 0x1000;
constexpr uint32_t kFlowBusy = 0x1000 | 0x4000 | 0x10000;  // restart, map reload, delayed restart
constexpr uint32_t kLevelState = 0x82D08C34;  // state at +12 (0 = running), started at +17
constexpr uint32_t kSimEnabled = 0x82CED760;
constexpr uint32_t kPauseReasons = 0x82D572E4;  // 1 load, 2 movie, 4 restart, 8 FMV
constexpr uint32_t kPhotoPauseBit = 0x80000000;  // freecam.cpp's photo mode
constexpr uint32_t kMenuPauseBit = 0x40000000;   // this menu
constexpr uint32_t kUiPauseCount = 0x82D57368;   // the pause menu and other UI screens

// XInput buttons.
constexpr uint16_t kPadUp = 0x0001, kPadDown = 0x0002, kPadLeft = 0x0004, kPadRight = 0x0008;
constexpr uint16_t kPadA = 0x1000, kPadB = 0x2000, kPadY = 0x8000;

// Host address of guest memory as the generated code computes it
// (REX_PHYS_HOST_OFFSET): on Windows the physical heaps from 0xE0000000,
// where the game's heap objects live, sit 0x1000 further on in host memory.
uint8_t* Host(uint8_t* base, uint32_t address) {
#if REX_PLATFORM_WIN32
  return base + address + (address >= 0xE0000000u ? 0x1000u : 0u);
#else
  return base + address;
#endif
}

uint32_t LoadBE32(uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, Host(base, address), sizeof(value));
  return rex::byte_swap(value);
}

void StoreBE32(uint8_t* base, uint32_t address, uint32_t value) {
  value = rex::byte_swap(value);
  std::memcpy(Host(base, address), &value, sizeof(value));
}

const CharacterTemplate* FindTemplate(std::string_view name) {
  for (const CharacterTemplate& t : kTemplates) {
    if (name == t.name) {
      return &t;
    }
  }
  return nullptr;
}

const CharacterTemplate* TemplateByStream(uint32_t stream_id) {
  for (const CharacterTemplate& t : kTemplates) {
    if (t.stream_id == stream_id) {
      return &t;
    }
  }
  return nullptr;
}

int ChoiceIndex(std::string_view name) {
  for (int i = 0; i < kChoiceCount; ++i) {
    if (name == kChoices[i].name) {
      return i;
    }
  }
  return 0;
}

const char* ChoiceLabel(std::string_view name) {
  for (const Choice& c : kChoices) {
    if (*c.name && name == c.name) {
      return c.label;
    }
  }
  return nullptr;
}

// State shared by the game thread (input, tick, level loads) and the UI
// thread (drawing, keyboard).
std::mutex g_mutex;
bool g_choices_loaded = false;
std::string g_choice[kSlots];        // the templates in effect, "" = the level's
std::string g_level_choice[kSlots];  // what the level itself spawned last
struct Menu {
  bool open = false;
  bool paused_level = false;  // opened during play, so the level is held
  int row = 0;                // 0, 1 = players, 2 = restart, 3 = close
  int pick[kSlots] = {};
};
Menu g_menu;

std::atomic<bool> g_restart_requested{false};
std::atomic<int> g_inject_b{0};  // polls left to send B to the game's pause menu
std::atomic<bool> g_level_ready{false};
std::atomic<bool> g_pause_menu_open{false};
bool g_menu_pause_held = false;  // game thread only

void LoadChoicesLocked() {
  if (!g_choices_loaded) {
    g_choices_loaded = true;
    g_choice[0] = REXCVAR_GET(character_player1);
    g_choice[1] = REXCVAR_GET(character_player2);
  }
}

void ApplyCharacterChoices(uint8_t* base, uint32_t game_flow) {
  std::string choice[kSlots];
  {
    std::lock_guard lock(g_mutex);
    LoadChoicesLocked();
    choice[0] = g_choice[0];
    choice[1] = g_choice[1];
  }
  const CharacterTemplate* level[kAllSlots] = {};
  for (int slot = 0; slot < kAllSlots; ++slot) {
    level[slot] = TemplateByStream(LoadBE32(base, game_flow + kResolvedPlayers + slot * 16));
  }
  const CharacterTemplate* result[kAllSlots] = {level[0], level[1], level[2], level[3]};
  for (int slot = 0; slot < kSlots; ++slot) {
    if (choice[slot].empty()) {
      continue;
    }
    const CharacterTemplate* chosen = FindTemplate(choice[slot]);
    if (!chosen) {
      REXLOG_WARN("[charsel] player {}: no character template \"{}\"", slot + 1, choice[slot]);
      continue;
    }
    // One template spawns one character: a second slot would get the same one.
    bool taken = false;
    for (int other = 0; other < kAllSlots; ++other) {
      taken |= other != slot && result[other] == chosen;
    }
    if (taken) {
      REXLOG_WARN("[charsel] player {}: {} is already in the level, keeping the level's", slot + 1,
                  chosen->name);
      continue;
    }
    result[slot] = chosen;
  }
  for (int slot = 0; slot < kSlots; ++slot) {
    const uint32_t guid = game_flow + kResolvedPlayers + slot * 16;
    if (result[slot] != level[slot] && result[slot]) {
      StoreBE32(base, guid + 0, result[slot]->stream_id);
      StoreBE32(base, guid + 4, result[slot]->words[0]);
      StoreBE32(base, guid + 8, result[slot]->words[1]);
      StoreBE32(base, guid + 12, result[slot]->words[2]);
      REXLOG_INFO("[charsel] player {}: {} instead of {}", slot + 1, result[slot]->name,
                  level[slot] ? level[slot]->name : "none");
    } else {
      REXLOG_INFO("[charsel] player {}: {} (the level's)", slot + 1,
                  level[slot] ? level[slot]->name : "none");
    }
  }
  std::lock_guard lock(g_mutex);
  for (int slot = 0; slot < kSlots; ++slot) {
    g_level_choice[slot] = level[slot] ? level[slot]->name : "";
  }
}

// Whether the level is running with nothing in front of it; ignore_bits are
// pause reasons that do not count (this menu's own, the photo mode's).
bool LevelRunning(uint8_t* base, uint32_t game_flow, uint32_t ignore_bits, bool allow_ui) {
  const uint32_t level = LoadBE32(base, kLevelState);
  if (!game_flow || !level) {
    return false;
  }
  return LoadBE32(base, level + 12) == 0 && *Host(base, level + 17) == 1 &&
         LoadBE32(base, game_flow + kFlowMap) != 0 &&
         LoadBE32(base, game_flow + kFlowEpisode) != 0 &&
         (LoadBE32(base, game_flow + kFlowFlags) & kFlowBusy) == 0 &&
         LoadBE32(base, kSimEnabled) != 0 &&
         (LoadBE32(base, kPauseReasons) & ~ignore_bits) == 0 &&
         (allow_ui || LoadBE32(base, kUiPauseCount) == 0);
}

// Menu actions, with g_mutex held.
void OpenMenuLocked() {
  if (g_menu.open || !g_level_ready.load()) {
    return;
  }
  LoadChoicesLocked();
  g_menu.open = true;
  g_menu.paused_level = !g_pause_menu_open.load();
  g_menu.row = 0;
  for (int slot = 0; slot < kSlots; ++slot) {
    g_menu.pick[slot] = ChoiceIndex(g_choice[slot]);
  }
}

void CloseMenuLocked() {
  g_menu.open = false;
}

void MoveRowLocked(int delta) {
  g_menu.row = (g_menu.row + delta + 4) % 4;
}

// Steps a player's choice, skipping the other player's (one template, one
// character).
void ChangePickLocked(int delta) {
  if (g_menu.row >= kSlots) {
    return;
  }
  int& pick = g_menu.pick[g_menu.row];
  const int other = g_menu.pick[1 - g_menu.row];
  for (int i = 0; i < kChoiceCount; ++i) {
    pick = (pick + delta + kChoiceCount) % kChoiceCount;
    if (pick == 0 || pick != other) {
      break;
    }
  }
}

void ConfirmLocked() {
  if (g_menu.row < kSlots) {
    g_menu.row = 2;
    return;
  }
  if (g_menu.row == 3) {
    CloseMenuLocked();
    return;
  }
  bool changed = false;
  for (int slot = 0; slot < kSlots; ++slot) {
    std::string name = kChoices[g_menu.pick[slot]].name;
    changed |= name != g_choice[slot];
    g_choice[slot] = name;
  }
  REXCVAR_SET(character_player1, g_choice[0]);
  REXCVAR_SET(character_player2, g_choice[1]);
  REXLOG_INFO("[charsel] chosen: player 1 {}, player 2 {}{}",
              g_choice[0].empty() ? "the level's" : g_choice[0],
              g_choice[1].empty() ? "the level's" : g_choice[1],
              changed ? ", restarting from the last checkpoint" : ", restarting anyway");
  CloseMenuLocked();
  if (!g_menu.paused_level) {
    g_inject_b = 6;  // leave the game's pause menu: B down, then up
  }
  g_restart_requested = true;
}

std::string LevelLabel(int slot) {
  const char* label = ChoiceLabel(g_level_choice[slot]);
  return label ? std::string(label) : std::string("unknown");
}

class CharacterMenuDialog : public rex::ui::ImGuiDialog {
 public:
  explicit CharacterMenuDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}
  ~CharacterMenuDialog() {
    if (input_held_) {
      rex::ui::ReleaseOverlayInput();
    }
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::lock_guard lock(g_mutex);
    // Keyboard: the menu takes it while open.
    if (g_menu.open != input_held_) {
      input_held_ = g_menu.open;
      if (input_held_) {
        rex::ui::AcquireOverlayInput();
      } else {
        rex::ui::ReleaseOverlayInput();
      }
    }
    const float scale = std::max(1.0f, io.DisplaySize.y / 720.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize;
    if (!g_menu.open) {
      if (g_pause_menu_open.load() && g_level_ready.load()) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.04f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.6f);
        if (ImGui::Begin("##charsel_hint", nullptr, flags)) {
          ImGui::SetWindowFontScale(scale);
          ImGui::TextUnformatted("Y: choose characters");
        }
        ImGui::End();
      }
      return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) MoveRowLocked(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) MoveRowLocked(1);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) ChangePickLocked(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) ChangePickLocked(1);
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
      ConfirmLocked();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      CloseMenuLocked();
    }
    if (!g_menu.open) {
      return;
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    if (ImGui::Begin("##charsel", nullptr, flags)) {
      ImGui::SetWindowFontScale(scale);
      const ImVec4 dim(0.65f, 0.65f, 0.65f, 1.0f);
      const ImVec4 hot(1.0f, 0.85f, 0.2f, 1.0f);
      ImGui::TextUnformatted("CHARACTERS");
      ImGui::Separator();
      for (int slot = 0; slot < kSlots; ++slot) {
        const bool focus = g_menu.row == slot;
        std::string text = std::string(focus ? "> " : "  ") + "Player " + std::to_string(slot + 1) +
                           ":   < " + kChoices[g_menu.pick[slot]].label + " >";
        ImGui::TextColored(focus ? hot : ImVec4(1, 1, 1, 1), "%s", text.c_str());
        if (g_menu.pick[slot] == 0) {
          ImGui::TextColored(dim, "              now: %s", LevelLabel(slot).c_str());
        }
      }
      ImGui::Spacing();
      ImGui::TextColored(g_menu.row == 2 ? hot : ImVec4(1, 1, 1, 1), "%s",
                         g_menu.row == 2 ? "> Restart from the last checkpoint"
                                         : "  Restart from the last checkpoint");
      ImGui::TextColored(g_menu.row == 3 ? hot : ImVec4(1, 1, 1, 1), "%s",
                         g_menu.row == 3 ? "> Close" : "  Close");
      ImGui::Separator();
      ImGui::TextColored(dim, "D-pad / arrows: choose    A / Enter: confirm    B / Esc: close");
      ImGui::TextColored(dim, "For fun and exploring: levels are built for their own characters.");
      ImGui::TextColored(dim, "With others, story triggers, objectives and cutscenes may not fire");
      ImGui::TextColored(dim, "and you can get stuck. \"The level's character\" brings them back.");
      ImGui::TextColored(dim, "The restart loses the progress since the last checkpoint.");
    }
    ImGui::End();
  }

 private:
  bool input_held_ = false;
};

std::unique_ptr<CharacterMenuDialog> g_dialog;

}  // namespace

void InitCharacterMenu(rex::ui::ImGuiDrawer* drawer) {
  if (drawer) {
    g_dialog = std::make_unique<CharacterMenuDialog>(drawer);
  }
  rex::ui::RegisterBind("bind_characters", "F7", "Choose characters (restarts at the checkpoint)",
                        [] {
                          std::lock_guard lock(g_mutex);
                          if (g_menu.open) {
                            CloseMenuLocked();
                          } else {
                            OpenMenuLocked();
                          }
                        });
}

void ShutdownCharacterMenu() {
  g_dialog.reset();
}

// Called by freecam.cpp's XInputGetState hook for player 1. Returns true when
// the menu takes the pad; *game_buttons is then what the game gets (sticks
// and triggers at rest).
bool CharacterMenuPad(uint16_t buttons, uint16_t* game_buttons) {
  static uint16_t last = 0;
  const uint16_t pressed = buttons & ~last;
  last = buttons;
  if (const int inject = g_inject_b.load(); inject > 0) {
    g_inject_b = inject - 1;
    *game_buttons = inject > 3 ? kPadB : 0;
    return true;
  }
  std::lock_guard lock(g_mutex);
  if (!g_menu.open) {
    if ((pressed & kPadY) && g_pause_menu_open.load() && g_level_ready.load()) {
      OpenMenuLocked();
      *game_buttons = buttons & ~kPadY;
      return true;
    }
    return false;
  }
  if (pressed & kPadUp) MoveRowLocked(-1);
  if (pressed & kPadDown) MoveRowLocked(1);
  if (pressed & kPadLeft) ChangePickLocked(-1);
  if (pressed & kPadRight) ChangePickLocked(1);
  if (pressed & kPadA) ConfirmLocked();
  if (pressed & kPadB) CloseMenuLocked();
  *game_buttons = 0;
  return true;
}

// Asks for a restart from the last checkpoint with the current choices, as
// soon as the level can take one.
void RequestCharacterRestart() {
  g_restart_requested = true;
}

// Called by freecam.cpp's override of the simulation tick (sub_82691540) on
// the game thread, before the tick runs.
void CharacterSelectTick(PPCContext& ctx, uint8_t* base) {
  const uint32_t game_flow = LoadBE32(base, kGameFlowManager);
  const bool level = LevelRunning(base, game_flow, kMenuPauseBit | kPhotoPauseBit, true);
  g_level_ready = level && !(LoadBE32(base, kPauseReasons) & kPhotoPauseBit);
  g_pause_menu_open = level && LoadBE32(base, kUiPauseCount) != 0;

  bool hold;
  {
    std::lock_guard lock(g_mutex);
    if (g_menu.open && !level) {
      CloseMenuLocked();  // the level ended or a movie started
    }
    hold = g_menu.open && g_menu.paused_level;
  }
  if (hold != g_menu_pause_held) {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = kMenuPauseBit;
    if (hold) {
      sub_826913A0(frame, base);
    } else {
      sub_82691368(frame, base);
    }
    g_menu_pause_held = hold;
  }

  if (!g_restart_requested.load() || g_menu_pause_held ||
      !LevelRunning(base, game_flow, 0, false)) {
    return;
  }
  g_restart_requested = false;
  StoreBE32(base, game_flow + kFlowFlags,
            LoadBE32(base, game_flow + kFlowFlags) | kFlowRestartPending);
  REXLOG_INFO("[charsel] restarting from the last checkpoint");
  rex::CallFrame frame(ctx);
  frame.ctx.r3.u64 = game_flow;
  frame.ctx.r4.u64 = 1;
  sub_8289BEA8(frame, base);
}

REX_FUNC(sub_8289E428) {
  const uint32_t game_flow = ctx.r3.u32;
  __imp__sub_8289E428(ctx, base);
  if (game_flow) {
    ApplyCharacterChoices(base, game_flow);
  }
}
