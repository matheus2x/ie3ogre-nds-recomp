// ARM7 card service (PXI tag 11), high level: backup (save) memory backed by a file.
//
// Protocol (NitroSDK card_common): the ARM9 first sends request 0 followed by the
// address of a shared CARDiCommandArg (0x60 bytes). Each later word is a request id;
// its parameters are in the shared block (+0x0C src, +0x10 dst, +0x14 len) and the
// ARM7 writes the result to +0x00, then replies on tag 11 with the error bit set.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include "pxi.h"
#include "runtime.h"

namespace ntr {

namespace {

enum Request : uint32_t {
    REQ_INIT = 0,
    REQ_IDENTIFY = 2,
    REQ_READ_ID = 3,
    REQ_READ_BACKUP = 6,
    REQ_WRITE_BACKUP = 7,
    REQ_PROGRAM_BACKUP = 8,
    REQ_VERIFY_BACKUP = 9,
    REQ_ERASE_PAGE = 10,
    REQ_ERASE_SECTOR = 11,
    REQ_ERASE_CHIP = 12,
    REQ_READ_STATUS = 13,
    REQ_WRITE_STATUS = 14,
};

enum Result : uint32_t { RESULT_SUCCESS = 0, RESULT_UNSUPPORTED = 3 };

constexpr uint32_t kSaveSize = 64u << 10;  // EEPROM 512 Kbit (CARD_IdentifyBackup(0x1001))

uint32_t g_shared = 0;
bool g_expect_pointer = false;
std::vector<uint8_t> g_save(kSaveSize, 0xFF);
std::filesystem::path g_save_path;

void save_flush() {
    if (g_save_path.empty()) return;
    std::filesystem::create_directories(g_save_path.parent_path());
    // Write to a temporary file and rename, so a crash never leaves a torn save.
    auto tmp = g_save_path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(g_save.data()), g_save.size());
    }
    std::filesystem::rename(tmp, g_save_path);
}

uint32_t handle(uint32_t req) {
    uint32_t src = rd32(g_shared + 0x0C), dst = rd32(g_shared + 0x10), len = rd32(g_shared + 0x14);
    switch (req) {
        case REQ_IDENTIFY:
        case REQ_READ_STATUS:
        case REQ_WRITE_STATUS:
            return RESULT_SUCCESS;
        case REQ_READ_BACKUP:
            for (uint32_t i = 0; i < len; i++) wr8(dst + i, g_save[(src + i) % kSaveSize]);
            return RESULT_SUCCESS;
        case REQ_WRITE_BACKUP:
        case REQ_PROGRAM_BACKUP:
            for (uint32_t i = 0; i < len; i++) g_save[(dst + i) % kSaveSize] = rd8(src + i);
            save_flush();
            return RESULT_SUCCESS;
        case REQ_VERIFY_BACKUP:
            for (uint32_t i = 0; i < len; i++) {
                if (g_save[(dst + i) % kSaveSize] != rd8(src + i)) return 5;  // CARD_RESULT_ERROR
            }
            return RESULT_SUCCESS;
        case REQ_ERASE_PAGE:
        case REQ_ERASE_SECTOR:
            for (uint32_t i = 0; i < len; i++) g_save[(dst + i) % kSaveSize] = 0xFF;
            save_flush();
            return RESULT_SUCCESS;
        case REQ_ERASE_CHIP:
            std::fill(g_save.begin(), g_save.end(), 0xFF);
            save_flush();
            return RESULT_SUCCESS;
        default:
            NTR_LOG("[card] unsupported request %u\n", req);
            return RESULT_UNSUPPORTED;
    }
}

void on_word(uint32_t data, bool) {
    if (g_expect_pointer) {
        g_shared = data;
        g_expect_pointer = false;
        wr32(g_shared, RESULT_SUCCESS);
        pxi_reply(11, 0, true);
        return;
    }
    if (data == REQ_INIT) {
        g_expect_pointer = true;
        return;
    }
    uint32_t result = handle(data);
    if (std::getenv("NTR_LOG_CARD")) {
        NTR_LOG("[card] request %u src %08X dst %08X len %X -> %u\n", data, rd32(g_shared + 0x0C),
                rd32(g_shared + 0x10), rd32(g_shared + 0x14), result);
    }
    wr32(g_shared, result);
    pxi_reply(11, data, true);
}

}  // namespace

void arm7_card_init(const std::string& save_path) {
    g_save_path = save_path;
    std::ifstream in(save_path, std::ios::binary);
    if (in) {
        in.read(reinterpret_cast<char*>(g_save.data()), g_save.size());
        NTR_LOG("[card] loaded save %s\n", save_path.c_str());
    } else {
        NTR_LOG("[card] no save at %s: new game\n", save_path.c_str());
    }
    replay_initial_save(g_save, bool(in));
    pxi_register_service(11, on_word);
}

}  // namespace ntr
