// File helpers that take UTF-8 paths: memory-mapped reads and fopen. On Windows a
// std::string path would be read in the ANSI code page, so these go through UTF-16.
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <ntr/common.h>

namespace ntr {

const uint8_t* map_file(const std::string& path, uint64_t& size) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
    HANDLE file = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(file, &li) || li.QuadPart == 0) {
        CloseHandle(file);
        return nullptr;
    }
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(file);
    if (!mapping) return nullptr;
    void* p = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);  // the view keeps the mapping alive
    size = uint64_t(li.QuadPart);
    return static_cast<const uint8_t*>(p);
#else
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return nullptr;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size == 0) {
        close(fd);
        return nullptr;
    }
    void* p = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return nullptr;
    size = uint64_t(st.st_size);
    return static_cast<const uint8_t*>(p);
#endif
}

void unmap_file(const uint8_t* data, uint64_t size) {
#ifdef _WIN32
    (void)size;
    UnmapViewOfFile(data);
#else
    munmap(const_cast<uint8_t*>(data), size_t(size));
#endif
}

std::filesystem::path fs_path(const std::string& utf8) { return std::u8string(utf8.begin(), utf8.end()); }

std::string utf8_string(const std::filesystem::path& path) {
    std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

FILE* open_file(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
    std::wstring wmode(mode, mode + std::strlen(mode));
    return _wfopen(path.c_str(), wmode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

std::filesystem::path logs_dir(const std::string& root) {
    namespace fs = std::filesystem;
    fs::path dir = fs_path(root.empty() ? "." : root) / "logs";
    std::error_code ec;
    fs::create_directories(dir, ec);
    static bool moved = false;
    if (!moved) {
        moved = true;
        for (const char* name : {"boej.log", "boej.old.log", "boej-replay.txt", "boej-replay.sav",
                                 "boej-replay.old.txt", "boej-replay.old.sav", "launcher.log"}) {
            fs::path old = dir.parent_path() / name;
            if (!fs::exists(old, ec)) continue;
            fs::remove(dir / name, ec);
            fs::rename(old, dir / name, ec);
        }
    }
    return dir;
}

}  // namespace ntr
