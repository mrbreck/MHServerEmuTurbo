#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct RarityEntry
{
    uint64_t    protoId;
    std::string name;   // "Common", "Uncommon", "Rare", "Epic", "Cosmic"
    int         tier;
};

// EquipmentInvUISlot values — must match server enum
enum class VapSlot : int
{
    Gear01     = 1,
    Gear02     = 2,
    Gear03     = 3,
    Gear04     = 4,
    Gear05     = 5,
    Artifact01  = 6,   // TeamUp Gear 1
    CostumeCore = 18,  // Catalyst
    Insignia    = 10,
    Artifact02 = 8,   // TeamUp Gear 2
    Ring       = 12,
    Artifact03 = 11,  // TeamUp Gear 3
    Artifact04 = 14,  // TeamUp Gear 4
};

static const VapSlot kAllSlots[] = {
    VapSlot::Gear01, VapSlot::Gear02, VapSlot::Gear03, VapSlot::Gear04, VapSlot::Gear05,
    VapSlot::Ring, VapSlot::Insignia, VapSlot::CostumeCore,
    VapSlot::Artifact01, VapSlot::Artifact02, VapSlot::Artifact03, VapSlot::Artifact04,
};
static constexpr int kNumSlots = 12;

inline const char* SlotDisplayName(VapSlot s) {
    switch (s) {
        case VapSlot::Gear01:     return "Gear 1";
        case VapSlot::Gear02:     return "Gear 2";
        case VapSlot::Gear03:     return "Gear 3";
        case VapSlot::Gear04:     return "Gear 4";
        case VapSlot::Gear05:     return "Gear 5";
        case VapSlot::Ring:       return "Ring";
        case VapSlot::Insignia:   return "Insignia";
        case VapSlot::CostumeCore: return "Catalyst";
        case VapSlot::Artifact01: return "Team-up Gear 1";
        case VapSlot::Artifact02: return "Team-up Gear 2";
        case VapSlot::Artifact03: return "Team-up Gear 3";
        case VapSlot::Artifact04: return "Team-up Gear 4";
        default:                  return "Unknown";
    }
}

struct GameOptions
{
    // slot int -> index into g_rarities (0 = off) — used by overlay UI
    std::unordered_map<int, int>      slotRarityIdx;
    // slot int -> protoId (0 = off) — used for save payload
    std::unordered_map<int, uint64_t> slotProtoId;
};

// GET /api/gameoptions?email=x&token=y
// Fills raritiesOut (sorted by tier) and optionsOut.slotProtoId
bool ApiGetGameOptions(const std::string& email, const std::string& token,
                       std::vector<RarityEntry>& raritiesOut,
                       GameOptions& optionsOut);

// POST /api/gameoptions
bool ApiSetGameOptions(const std::string& email, const std::string& token,
                       const GameOptions& options);

extern std::string g_serverBase;
