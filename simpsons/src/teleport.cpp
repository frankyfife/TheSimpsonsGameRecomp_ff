// Moving the players' characters, for the free camera's "bring the players
// here".
//
// A level spawns each player's character through sub_823BB230 (r4 = the
// character template's GUID, r5 = its spawn transform), which returns the
// character. Its transform lives in the object at character +0x8C: a 4x4
// matrix at +0x10, the position at +0x40 (Y is up). Writing the position
// moves the character there at once; physics then goes on from there, so a
// character put in the air falls to the ground as after a jump.
//
// Which player a spawned character belongs to comes from the GameFlowManager's
// resolved player slots (a template GUID per slot at +0x354 + slot * 16, the
// GUID the spawn gets). The characters are noted on every spawn: level loads,
// checkpoint restarts, the wardrobe and the bus stops all spawn them anew.

#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ppc.h>

REX_EXTERN(sub_823BB230);
REX_EXTERN(__imp__sub_823BB230);

namespace {

constexpr uint32_t kGameFlowManager = 0x82D08BA8;
constexpr uint32_t kResolvedPlayers = 0x354;
constexpr int kPlayers = 2;
constexpr uint32_t kCharacterTransform = 0x8C;  // object; matrix at +0x10
constexpr float kPartnerOffset = 1.0f;           // player 2 lands this far to the side

uint32_t g_character[kPlayers] = {};  // game thread only

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

float LoadBEFloat(uint8_t* base, uint32_t address) {
  const uint32_t bits = LoadBE32(base, address);
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void StoreBEFloat(uint8_t* base, uint32_t address, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  bits = rex::byte_swap(bits);
  std::memcpy(Host(base, address), &bits, sizeof(bits));
}

bool InGuestHeap(uint32_t address) {
  return address >= 0x40000000u && address < 0xFF000000u;
}

// The character's matrix, or 0 if it does not look like a live character.
uint32_t CharacterMatrix(uint8_t* base, uint32_t character) {
  if (!InGuestHeap(character)) {
    return 0;
  }
  const uint32_t transform = LoadBE32(base, character + kCharacterTransform);
  if (!InGuestHeap(transform)) {
    return 0;
  }
  const uint32_t matrix = transform + 0x10;
  // A rotation row of unit length and a position in the world (the 4th
  // column is not kept as (0, 0, 0, 1)).
  const float x = LoadBEFloat(base, matrix + 0x00), y = LoadBEFloat(base, matrix + 0x04),
              z = LoadBEFloat(base, matrix + 0x08);
  const float row = x * x + y * y + z * z;
  if (!(row > 0.8f && row < 1.25f)) {
    return 0;
  }
  for (uint32_t i = 0; i < 3; ++i) {
    const float v = LoadBEFloat(base, matrix + 0x30 + 4 * i);
    if (!(std::fabs(v) < 100000.0f)) {
      return 0;
    }
  }
  return matrix;
}

}  // namespace

// Moves the players' characters to position (player 2 beside player 1).
// Returns how many it moved. Game thread.
int TeleportPlayers(uint8_t* base, const float position[3]) {
  int moved = 0;
  for (int slot = 0; slot < kPlayers; ++slot) {
    const uint32_t matrix = CharacterMatrix(base, g_character[slot]);
    if (!matrix) {
      continue;
    }
    StoreBEFloat(base, matrix + 0x30, position[0] + (slot ? kPartnerOffset : 0.0f));
    StoreBEFloat(base, matrix + 0x34, position[1]);
    StoreBEFloat(base, matrix + 0x38, position[2]);
    ++moved;
  }
  REXLOG_INFO("[teleport] {} character(s) to ({:.2f}, {:.2f}, {:.2f})", moved, position[0],
              position[1], position[2]);
  return moved;
}

REX_FUNC(sub_823BB230) {
  const uint32_t stream_id = LoadBE32(base, ctx.r4.u32);
  __imp__sub_823BB230(ctx, base);
  const uint32_t game_flow = LoadBE32(base, kGameFlowManager);
  if (!game_flow || !ctx.r3.u32) {
    return;
  }
  for (int slot = 0; slot < kPlayers; ++slot) {
    if (LoadBE32(base, game_flow + kResolvedPlayers + slot * 16) == stream_id) {
      g_character[slot] = ctx.r3.u32;
    }
  }
}
