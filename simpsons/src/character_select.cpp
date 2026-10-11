// Character select.
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
// (simpsons_chars_global.str), and spawns it at the slot's locator.
//
// character_player1 and character_player2 name a template for slots 0 and 1;
// after sub_8289E428 its GUID replaces the resolved one. Empty keeps the
// game's choice. A change takes effect at the next level load or checkpoint
// restart. The game's own lists (the overrides, the hub's controlled-player
// array that goes into the savegame) are left alone.
//
// The template GUIDs come from the character streams in
// gamedata/simpsons_chars/globalfolder/chars: each stream's object table holds
// entries {stream ID, three GUID words}, and the template's own entry is the
// second object after one all templates share (4CF93835 D99B1987 500A10FE),
// which matches the GUIDs the game's Lua uses (Lisa in Grand Theft Scratchy,
// Bart and Homer in Medal of Homer).

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_STRING(character_player1, "", "Game",
                      "Character template for player 1 (e.g. homer_fh1_gh1_hh1_h2), empty = the "
                      "level's own; takes effect at the next level load or checkpoint restart");
REXCVAR_DEFINE_STRING(character_player2, "", "Game",
                      "Character template for player 2, empty = the level's own; takes effect at "
                      "the next level load or checkpoint restart");

// ResolvePlayers: r3 = the GameFlowManager.
REX_EXTERN(sub_8289E428);
REX_EXTERN(__imp__sub_8289E428);

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

constexpr uint32_t kResolvedPlayers = 0x354;  // 4 slots x 16-byte GUID
constexpr int kSlots = 2;

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

const char* TemplateName(uint32_t stream_id) {
  for (const CharacterTemplate& t : kTemplates) {
    if (t.stream_id == stream_id) {
      return t.name;
    }
  }
  return nullptr;
}

std::string SlotChoice(int slot) {
  return slot == 0 ? REXCVAR_GET(character_player1) : REXCVAR_GET(character_player2);
}

void ApplyCharacterChoices(uint8_t* base, uint32_t game_flow) {
  const CharacterTemplate* chosen[kSlots] = {};
  for (int slot = 0; slot < kSlots; ++slot) {
    const std::string name = SlotChoice(slot);
    if (name.empty()) {
      continue;
    }
    chosen[slot] = FindTemplate(name);
    if (!chosen[slot]) {
      REXLOG_WARN("[charsel] player {}: no character template \"{}\"", slot + 1, name);
    }
  }
  // One template spawns one character: a second slot would get the same one.
  if (chosen[0] && chosen[0] == chosen[1]) {
    REXLOG_WARN("[charsel] both players chose {}; player 2 keeps the level's character",
                chosen[1]->name);
    chosen[1] = nullptr;
  }
  for (int slot = 0; slot < kSlots; ++slot) {
    const uint32_t guid = game_flow + kResolvedPlayers + slot * 16;
    const uint32_t level_stream = LoadBE32(base, guid);
    const char* level_name = TemplateName(level_stream);
    if (!chosen[slot]) {
      REXLOG_INFO("[charsel] player {}: {} (the level's)", slot + 1,
                  level_name ? level_name : (level_stream ? "unknown" : "none"));
      continue;
    }
    StoreBE32(base, guid + 0, chosen[slot]->stream_id);
    StoreBE32(base, guid + 4, chosen[slot]->words[0]);
    StoreBE32(base, guid + 8, chosen[slot]->words[1]);
    StoreBE32(base, guid + 12, chosen[slot]->words[2]);
    REXLOG_INFO("[charsel] player {}: {} instead of {}", slot + 1, chosen[slot]->name,
                level_name ? level_name : "the level's");
  }
}

}  // namespace

REX_FUNC(sub_8289E428) {
  const uint32_t game_flow = ctx.r3.u32;
  __imp__sub_8289E428(ctx, base);
  if (game_flow) {
    ApplyCharacterChoices(base, game_flow);
  }
}
