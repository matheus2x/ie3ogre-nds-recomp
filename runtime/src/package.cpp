// Mods as folders or .zips (ntr/package.h). The inflater follows zlib's "puff" (canonical
// Huffman codes decoded a bit at a time): small and plenty fast for files read once.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>

#include <ntr/common.h>
#include <ntr/package.h>

namespace ntr {

namespace fs = std::filesystem;

namespace {

uint16_t load16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

// --- inflate ------------------------------------------------------------------------------

struct Fail {};

struct Huffman {
    uint16_t count[16];   // codes of each length
    uint16_t symbol[320];  // symbols ordered by code
};

class Inflater {
public:
    Inflater(const uint8_t* in, size_t size, std::vector<uint8_t>& out) : in_(in), size_(size), out_(out) {}

    void run() {
        bool last;
        do {
            last = bits(1);
            switch (bits(2)) {
                case 0: stored(); break;
                case 1: fixed(); break;
                case 2: dynamic(); break;
                default: throw Fail{};
            }
        } while (!last);
    }

private:
    uint32_t bits(int need) {
        uint32_t v = buf_;
        while (cnt_ < need) {
            if (pos_ >= size_) throw Fail{};
            v |= uint32_t(in_[pos_++]) << cnt_;
            cnt_ += 8;
        }
        buf_ = v >> need;
        cnt_ -= need;
        return v & ((1u << need) - 1);
    }

    void stored() {
        buf_ = 0;
        cnt_ = 0;
        if (pos_ + 4 > size_) throw Fail{};
        uint32_t len = load16(in_ + pos_), nlen = load16(in_ + pos_ + 2);
        pos_ += 4;
        if (len != (~nlen & 0xFFFF) || pos_ + len > size_) throw Fail{};
        out_.insert(out_.end(), in_ + pos_, in_ + pos_ + len);
        pos_ += len;
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++) {
            code |= int(bits(1));
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        throw Fail{};
    }

    // >0: incomplete code, 0: complete, <0: over-subscribed.
    static int build(Huffman& h, const uint16_t* length, int n) {
        std::memset(h.count, 0, sizeof h.count);
        for (int s = 0; s < n; s++) h.count[length[s]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len < 16; len++) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return left;
        }
        uint16_t offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; len++) offs[len + 1] = uint16_t(offs[len] + h.count[len]);
        for (int s = 0; s < n; s++) {
            if (length[s]) h.symbol[offs[length[s]]++] = uint16_t(s);
        }
        return left;
    }

    void codes(const Huffman& lencode, const Huffman& distcode) {
        static const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                              31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                              2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                               193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                               6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lencode);
            if (sym < 256) {
                out_.push_back(uint8_t(sym));
            } else if (sym == 256) {
                return;
            } else {
                sym -= 257;
                if (sym >= 29) throw Fail{};
                size_t len = kLenBase[sym] + bits(kLenExtra[sym]);
                int dsym = decode(distcode);
                if (dsym >= 30) throw Fail{};
                size_t dist = kDistBase[dsym] + bits(kDistExtra[dsym]);
                if (dist > out_.size()) throw Fail{};
                size_t from = out_.size() - dist;
                for (size_t i = 0; i < len; i++) out_.push_back(out_[from + i]);
            }
        }
    }

    void fixed() {
        static const auto tables = [] {
            std::pair<Huffman, Huffman> t;
            uint16_t lengths[288];
            int s = 0;
            for (; s < 144; s++) lengths[s] = 8;
            for (; s < 256; s++) lengths[s] = 9;
            for (; s < 280; s++) lengths[s] = 7;
            for (; s < 288; s++) lengths[s] = 8;
            build(t.first, lengths, 288);
            for (s = 0; s < 30; s++) lengths[s] = 5;
            build(t.second, lengths, 30);
            return t;
        }();
        codes(tables.first, tables.second);
    }

    void dynamic() {
        static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = int(bits(5)) + 257, ndist = int(bits(5)) + 1, ncode = int(bits(4)) + 4;
        if (nlen > 286 || ndist > 30) throw Fail{};
        uint16_t lengths[320] = {};
        for (int i = 0; i < ncode; i++) lengths[kOrder[i]] = uint16_t(bits(3));
        Huffman lencode, distcode;
        if (build(lencode, lengths, 19) != 0) throw Fail{};
        for (int index = 0; index < nlen + ndist;) {
            int sym = decode(lencode);
            if (sym < 16) {
                lengths[index++] = uint16_t(sym);
                continue;
            }
            uint16_t len = 0;
            int repeat;
            if (sym == 16) {
                if (index == 0) throw Fail{};
                len = lengths[index - 1];
                repeat = 3 + int(bits(2));
            } else if (sym == 17) {
                repeat = 3 + int(bits(3));
            } else {
                repeat = 11 + int(bits(7));
            }
            if (index + repeat > nlen + ndist) throw Fail{};
            while (repeat--) lengths[index++] = len;
        }
        if (lengths[256] == 0) throw Fail{};
        int err = build(lencode, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) throw Fail{};
        err = build(distcode, lengths + nlen, ndist);
        if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) throw Fail{};
        codes(lencode, distcode);
    }

    const uint8_t* in_;
    size_t size_, pos_ = 0;
    uint32_t buf_ = 0;
    int cnt_ = 0;
    std::vector<uint8_t>& out_;
};

}  // namespace

bool inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    try {
        Inflater(data, size, out).run();
        return true;
    } catch (const Fail&) {
        return false;
    }
}

uint32_t crc32(const uint8_t* data, size_t size) {
    static const auto table = [] {
        std::vector<uint32_t> t(256);
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

// --- BPS --------------------------------------------------------------------------------

namespace {

bool bps_varint(const std::vector<uint8_t>& p, size_t& at, size_t end, uint64_t& n) {
    n = 0;
    for (uint64_t shift = 1; at < end && shift < (1ull << 56); shift <<= 7) {
        uint8_t x = p[at++];
        n += (x & 0x7F) * shift;
        if (x & 0x80) return true;
        n += shift << 7;
    }
    return false;
}

bool bps_header(const std::vector<uint8_t>& p, size_t& at, uint64_t& source, uint64_t& target) {
    uint64_t meta = 0;
    at = 4;
    if (p.size() < 4 + 3 + 12 || std::memcmp(p.data(), "BPS1", 4) != 0) return false;
    size_t end = p.size() - 12;
    if (!bps_varint(p, at, end, source) || !bps_varint(p, at, end, target) || !bps_varint(p, at, end, meta) ||
        meta > end - at)
        return false;
    at += size_t(meta);
    return true;
}

}  // namespace

bool bps_sizes(const std::vector<uint8_t>& patch, uint64_t& source_size, uint64_t& target_size) {
    size_t at = 0;
    return bps_header(patch, at, source_size, target_size);
}

bool bps_apply(const std::vector<uint8_t>& p, const uint8_t* source, size_t source_size, std::vector<uint8_t>& out,
               std::string& error) {
    out.clear();
    size_t at = 0;
    uint64_t want_source = 0, target = 0;
    if (!bps_header(p, at, want_source, target) || target > 0x10000000) {
        error = L("not a valid BPS patch", "não é um patch BPS válido");
        return false;
    }
    size_t end = p.size() - 12;
    if (crc32(p.data(), end + 8) != load32(&p[end + 8])) {
        error = L("the patch is damaged (CRC)", "o patch está danificado (CRC)");
        return false;
    }
    if (want_source != source_size || crc32(source, source_size) != load32(&p[end])) {
        error = L("the patch is for another version of that file (the original does not match)", "o patch é para outra versão desse arquivo (o original não confere)");
        return false;
    }
    out.reserve(size_t(target));
    uint64_t source_rel = 0, target_rel = 0;
    while (at < end) {
        uint64_t data = 0, rel = 0;
        if (!bps_varint(p, at, end, data)) break;
        uint64_t length = (data >> 2) + 1;
        unsigned kind = unsigned(data & 3);
        if (length > target - out.size()) break;
        if (kind == 0) {  // SourceRead
            if (out.size() + length > source_size) break;
            out.insert(out.end(), source + out.size(), source + out.size() + length);
        } else if (kind == 1) {  // TargetRead
            if (length > end - at) break;
            out.insert(out.end(), p.begin() + std::ptrdiff_t(at), p.begin() + std::ptrdiff_t(at + length));
            at += size_t(length);
        } else {
            if (!bps_varint(p, at, end, rel)) break;
            uint64_t& base = kind == 2 ? source_rel : target_rel;
            base = rel & 1 ? base - (rel >> 1) : base + (rel >> 1);
            if (kind == 2) {  // SourceCopy
                if (base > source_size || length > source_size - base) break;
                out.insert(out.end(), source + base, source + base + length);
            } else {  // TargetCopy: may overlap what it writes, so a byte at a time
                if (base >= out.size()) break;
                for (uint64_t i = 0; i < length; i++) out.push_back(out[size_t(base + i)]);
            }
            base += length;
        }
    }
    if (at != end || out.size() != target || crc32(out.data(), out.size()) != load32(&p[end + 4])) {
        error = L("the patch is damaged", "o patch está danificado");
        return false;
    }
    return true;
}

// --- packages ---------------------------------------------------------------------------

ModPackage::~ModPackage() {
    if (zip_) unmap_file(zip_, zip_size_);
    for (auto [p, n] : mapped_) unmap_file(p, n);
}

const ModPackage::Entry* ModPackage::find(const std::string& path) const {
    std::string want = lower(path);
    for (const Entry& e : entries_) {
        if (lower(e.path) == want) return &e;
    }
    return nullptr;
}

bool ModPackage::open(const std::string& path, std::string& error) {
    std::error_code ec;
    fs::path root = fs_path(path);
    if (!fs::is_directory(root, ec)) return open_zip(path, error);
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::u8string rel = it->path().lexically_relative(root).generic_u8string();
        Entry e;
        e.path.assign(rel.begin(), rel.end());
        e.size = it->file_size(ec);
        e.stamp = uint64_t(it->last_write_time(ec).time_since_epoch().count());
        e.file = utf8_string(it->path());
        entries_.push_back(std::move(e));
    }
    if (ec) {
        error = L("could not read the folder (", "não foi possível ler a pasta (") + ec.message() + ")";
        return false;
    }
    return true;
}

bool ModPackage::open_zip(const std::string& path, std::string& error) {
    zip_ = map_file(path, zip_size_);
    if (!zip_) {
        error = L("could not open the file", "não foi possível abrir o arquivo");
        return false;
    }
    // End of central directory: the last 22 bytes plus up to 64 KiB of comment.
    const uint8_t* eocd = nullptr;
    for (uint64_t back = 22; back <= std::min<uint64_t>(zip_size_, 22 + 0xFFFF) && !eocd; back++) {
        const uint8_t* p = zip_ + zip_size_ - back;
        if (load32(p) == 0x06054B50) eocd = p;
    }
    if (!eocd) {
        error = L("not a valid .zip", "não é um .zip válido");
        return false;
    }
    uint32_t count = load16(eocd + 10), cd_size = load32(eocd + 12), cd_off = load32(eocd + 16);
    if (count == 0xFFFF || cd_off == 0xFFFFFFFF || uint64_t(cd_off) + cd_size > zip_size_) {
        error = L("unsupported .zip format (zip64) or damaged file", "formato de .zip não suportado (zip64) ou arquivo danificado");
        return false;
    }
    std::vector<Entry> all;
    const uint8_t* p = zip_ + cd_off;
    const uint8_t* end = p + cd_size;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 46 > end || load32(p) != 0x02014B50) {
            error = L("the .zip is damaged", "o .zip está danificado");
            return false;
        }
        uint16_t flags = load16(p + 8), method = load16(p + 10);
        uint16_t name_len = load16(p + 28), extra_len = load16(p + 30), comment_len = load16(p + 32);
        if (p + 46 + name_len > end) {
            error = L("the .zip is damaged", "o .zip está danificado");
            return false;
        }
        Entry e;
        e.path.assign(reinterpret_cast<const char*>(p + 46), name_len);
        e.crc = load32(p + 16);
        e.packed = load32(p + 20);
        e.size = load32(p + 24);
        e.stamp = e.crc;
        e.method = method;
        uint64_t local = load32(p + 42);
        p += 46 + name_len + extra_len + comment_len;
        std::replace(e.path.begin(), e.path.end(), '\\', '/');
        if (e.path.empty() || e.path.back() == '/') continue;  // a folder
        if (flags & 1) {
            error = L("the .zip has a password (", "o .zip tem senha (") + e.path + ")";
            return false;
        }
        if (local + 30 > zip_size_ || load32(zip_ + local) != 0x04034B50) {
            error = L("the .zip is damaged (", "o .zip está danificado (") + e.path + ")";
            return false;
        }
        e.offset = local + 30 + load16(zip_ + local + 26) + load16(zip_ + local + 28);
        if (e.offset + e.packed > zip_size_) {
            error = L("the .zip is damaged (", "o .zip está danificado (") + e.path + ")";
            return false;
        }
        all.push_back(std::move(e));
    }
    // The root: where mod.ini is, at the top or in the only top-level folder.
    std::string prefix;
    bool at_top = std::any_of(all.begin(), all.end(), [](const Entry& e) { return lower(e.path) == "mod.ini"; });
    if (!at_top) {
        std::string first;
        bool single = true;
        for (const Entry& e : all) {
            size_t slash = e.path.find('/');
            std::string top = slash == std::string::npos ? "" : e.path.substr(0, slash + 1);
            if (first.empty()) first = top;
            if (top.empty() || top != first) single = false;
        }
        if (single && !first.empty()) prefix = first;
    }
    for (Entry& e : all) {
        if (e.path.compare(0, prefix.size(), prefix) != 0) continue;
        e.path = e.path.substr(prefix.size());
        entries_.push_back(std::move(e));
    }
    return true;
}

const uint8_t* ModPackage::direct(const Entry& e) const {
    if (zip_) return e.method == 0 ? zip_ + e.offset : nullptr;
    uint64_t size = 0;
    const uint8_t* data = map_file(e.file, size);
    if (!data) return nullptr;
    mapped_.push_back({data, size});
    return size == e.size ? data : nullptr;
}

bool ModPackage::read(const Entry& e, std::vector<uint8_t>& out, std::string& error) const {
    out.clear();
    if (!zip_) {
        FILE* f = open_file(fs_path(e.file), "rb");
        if (!f) {
            error = L("could not read ", "não foi possível ler ") + e.path;
            return false;
        }
        out.resize(e.size);
        bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
        std::fclose(f);
        if (!ok) error = L("could not read ", "não foi possível ler ") + e.path;
        return ok;
    }
    if (e.method == 0) {
        out.assign(zip_ + e.offset, zip_ + e.offset + e.packed);
    } else if (e.method == 8) {
        out.reserve(e.size);
        if (!inflate(zip_ + e.offset, e.packed, out)) {
            error = e.path + L(" is damaged in the .zip", " está danificado no .zip");
            return false;
        }
    } else {
        error = e.path + L(": unsupported .zip compression (use \"Deflate\" or none)", ": compressão do .zip não suportada (use \"Deflate\" ou nenhuma)");
        return false;
    }
    if (out.size() != e.size || crc32(out.data(), out.size()) != e.crc) {
        error = e.path + L(" is damaged in the .zip (CRC)", " está danificado no .zip (CRC)");
        return false;
    }
    return true;
}

}  // namespace ntr
