// SHA-1 (dump identification) and SHA-256 (downloads checked against sha256sums.txt and
// pinned tool archives).
#include <algorithm>
#include <cstring>

#include <ntr/common.h>

namespace ntr {

namespace {

uint32_t rol(uint32_t v, int n) { return v << n | v >> (32 - n); }
uint32_t ror(uint32_t v, int n) { return v >> n | v << (32 - n); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// Shared block buffering: both hashes take 64-byte blocks and end with a 64-bit
// big-endian bit count.
template <typename Compress>
void feed(uint8_t* block, size_t& used, uint64_t& total, const uint8_t* p, size_t n, Compress compress) {
    total += n;
    if (used) {
        size_t take = std::min(n, 64 - used);
        std::memcpy(block + used, p, take);
        used += take;
        p += take;
        n -= take;
        if (used < 64) return;
        compress(block);
        used = 0;
    }
    for (; n >= 64; p += 64, n -= 64) compress(p);
    std::memcpy(block, p, n);
    used = n;
}

// The padding: 0x80, zeros, then the length in bits.
size_t padding(size_t used, uint64_t total, uint8_t (&pad)[72]) {
    std::memset(pad, 0, sizeof pad);
    pad[0] = 0x80;
    size_t len = (used < 56 ? 56 : 120) - used;
    uint64_t bits = total * 8;
    for (int i = 0; i < 8; i++) pad[len + i] = uint8_t(bits >> (56 - 8 * i));
    return len + 8;
}

std::string to_hex(const uint32_t* words, int count) {
    std::string out(size_t(count) * 8, '0');
    for (int i = 0; i < count; i++) std::snprintf(out.data() + 8 * i, 9, "%08x", words[i]);
    return out;
}

constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

}  // namespace

void Sha1::update(const uint8_t* p, size_t n) {
    feed(block_, used_, total_, p, n, [this](const uint8_t* b) { compress(b); });
}

std::string Sha1::hex() {
    uint8_t pad[72];
    update(pad, padding(used_, total_, pad));
    return to_hex(h_, 5);
}

void Sha1::compress(const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = be32(p + 4 * i);
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
        else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
        else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
        else f = b ^ c ^ d, k = 0xCA62C1D6;
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
}

void Sha256::update(const uint8_t* p, size_t n) {
    feed(block_, used_, total_, p, n, [this](const uint8_t* b) { compress(b); });
}

std::string Sha256::hex() {
    uint8_t pad[72];
    update(pad, padding(used_, total_, pad));
    return to_hex(h_, 8);
}

void Sha256::compress(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = be32(p + 4 * i);
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + kSha256K[i] + w[i];
        uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

std::string sha256_file(const std::string& path) {
    uint64_t size = 0;
    const uint8_t* data = map_file(path, size);
    if (!data) return "";
    Sha256 sha;
    sha.update(data, size_t(size));
    unmap_file(data, size);
    return sha.hex();
}

}  // namespace ntr
