// The game's own save (64 KB EEPROM), read on the host: what the slot card shows and the
// team. Shared by the launcher (the Saves tab) and the game (achievements, checked when the
// game saves).
//
// The save has two copies of the game, one per 32 KB half; the game loads the newer one
// (load at 0x020617d8). Each half starts with "INAZUMA_ELEVEN3" at +4 and is encrypted
// except for bytes 4..0x13 (cipher at 0x0205ffb0, with the key hidden inside the block).
// Decrypted, it is a bit stream written from the type table in /data_iz/bfmt/cnvdat.bin:
// SRecordInfo (0x2E bytes) and then st_data, which starts with SRecordHeader, the summary
// the slot card shows. All the fields read here are whole bytes, so their offsets are fixed
// for the schema version 0x24. Further in st_data, the fields are packed bits (LSB first,
// each with its width from the table): the team (TeamData.team_member[0], the unit numbers)
// and each unit's number and shirt number (UnitData.unit_save[]), at bit offsets worked out
// from cnvdat.bin.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace ntr {

constexpr size_t kSaveDataSize = 0x10000;

struct SaveSummary {
    bool valid = false;    // a save of this game
    bool empty = true;     // valid, but no game saved in it yet
    bool blank = false;    // all 0xFF, like a new cartridge
    bool unknown = false;  // valid, another schema version
    uint32_t play_frames = 0;
    int chapter = 0, level = 0, members = 0;
    uint8_t user[24] = {};               // the player's name, Shift-JIS
    std::vector<uint16_t> team_units;    // unit numbers of the team (team_member[0]), in save order
    std::vector<uint16_t> units;         // unit numbers in UnitData.unit_save[] (0 left out)
    std::map<uint16_t, uint32_t> shirt;  // unit number -> shirt number
};

// `size` bytes of a .sav (anything shorter than kSaveDataSize is not a save).
SaveSummary read_save_data(const uint8_t* data, size_t size);

}  // namespace ntr
