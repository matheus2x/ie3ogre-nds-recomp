// 3D engine: the geometry engine (GX command FIFO and ports at 0x04000400-0x040005FF)
// and a software rendering engine whose output is engine A's BG0 in 3D mode.
//
// Geometry: matrix stacks, vertex/normal/texcoord/color commands, hardware lighting,
// texture coordinate transforms, polygon assembly (triangles, quads, strips), culling and
// clipping against the view volume, BOX/POS/VEC tests and the result registers.
// Commands run as soon as they are written, so the FIFO is always empty and never busy.
//
// Rendering: SWAP_BUFFERS renders the polygon list into the back buffer, which becomes
// visible at the next VBlank (like the hardware, which renders the swapped lists during
// the following frame). Supported: Z and W depth buffering, all seven texture formats,
// modulate/decal/toon/highlight/shadow polygons, translucency and alpha test, polygon
// IDs, edge marking, fog, anti-aliasing, wireframe (alpha 0) polygons and the clear
// color/depth. Not modeled: 1-dot polygon culling and the clear bitmap (rear plane).
//
// Internal resolution: the engine can render at N times 256x192 (gpu3d_set_scale). The
// 2D engine and display capture keep reading a native-size copy (one sample per native
// pixel), so the game sees the same 3D output at any scale; only the host's picture of
// engine A uses the large frame (gpu3d_hi_row_*).
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "runtime.h"

namespace ntr {

namespace {

// --- fixed point helpers --------------------------------------------------------------

using Matrix = int32_t[16];  // 20.12, row-major, applied to row vectors (v' = v * M)

void mtx_identity(int32_t* m) {
    std::memset(m, 0, 16 * sizeof(int32_t));
    m[0] = m[5] = m[10] = m[15] = 0x1000;
}

// a = b * a
void mtx_mult(int32_t* a, const int32_t* b) {
    int32_t r[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int64_t s = 0;
            for (int k = 0; k < 4; k++) s += int64_t(b[i * 4 + k]) * a[k * 4 + j];
            r[i * 4 + j] = int32_t(s >> 12);
        }
    }
    std::memcpy(a, r, sizeof r);
}

void vec_mult(const int32_t* v, const int32_t* m, int32_t* out) {  // 4-component row vector
    for (int c = 0; c < 4; c++) {
        int64_t s = 0;
        for (int r = 0; r < 4; r++) s += int64_t(v[r]) * m[r * 4 + c];
        out[c] = int32_t(s >> 12);
    }
}

int32_t sext(uint32_t v, int bits) { return int32_t(v << (32 - bits)) >> (32 - bits); }

// --- geometry engine state -----------------------------------------------------------

struct Vertex {
    int32_t clip[4];   // x, y, z, w in clip space (20.12)
    float color[3];    // 0..31
    float s, t;        // texel units (12.4 / 16)
    // after projection
    float sx, sy, z, w;
};

struct Polygon {
    Vertex v[10];
    int count = 0;
    uint32_t attr = 0;       // POLYGON_ATTR at BEGIN_VTXS
    uint32_t tex = 0;        // TEXIMAGE_PARAM
    uint32_t pal = 0;        // PLTT_BASE
    bool translucent = false;
    bool wbuffer = false;
    float ymin = 0, ymax = 0;
};

struct Light {
    float dir[3];
    float color[3];
};

struct Geometry {
    // Matrices and stacks.
    uint32_t mode = 0;
    Matrix proj, pos, vec, tex;
    Matrix proj_stack[1], pos_stack[32], vec_stack[32], tex_stack[1];
    uint32_t proj_sp = 0, pos_sp = 0, tex_sp = 0;
    bool overflow = false;
    Matrix clip;
    bool clip_dirty = true;

    // Vertex attributes.
    int32_t vtx[3] = {};
    float color[3] = {31, 31, 31};
    int32_t raw_s = 0, raw_t = 0;  // 12.4
    float s = 0, t = 0;
    int32_t normal[3] = {};
    uint32_t polygon_attr = 0, current_attr = 0;
    uint32_t teximage = 0, pltt = 0;
    float diffuse[3] = {}, ambient[3] = {}, specular[3] = {}, emission[3] = {};
    bool shininess_table = false;
    uint8_t shininess[128] = {};
    Light lights[4] = {};

    // Primitive assembly.
    bool in_begin = false;
    uint32_t prim = 0;
    std::vector<Vertex> strip;  // vertices of the primitive being assembled
    uint32_t prim_count = 0;    // polygons emitted in this strip

    // Viewport (screen coordinates, y measured from the bottom).
    int vp_x1 = 0, vp_y1 = 0, vp_x2 = 255, vp_y2 = 191;

    // Lists.
    std::vector<Polygon> polys;
    uint32_t vertex_count = 0;
    bool swap_pending = false;

    // Packed FIFO decoding and direct ports.
    uint32_t fifo_cmds = 0;   // up to 4 pending command ids, lowest byte first
    int fifo_left = 0;        // pending commands in fifo_cmds
    std::vector<uint32_t> params;       // parameters of the packed command being collected
    std::vector<uint32_t> port_params;  // the same for the direct ports
    uint32_t port_cmd = 0;              // command whose parameters the ports are collecting

    // Statistics for NTR_LOG_3D.
    uint32_t stat_vertices = 0, stat_culled = 0, stat_clipped = 0, stat_swaps = 0;
    std::map<uint32_t, uint32_t> stat_cmds;

    bool box_result = false;
    int32_t pos_result[4] = {};
    int16_t vec_result[3] = {};
};
Geometry g;

int param_count(uint32_t cmd) {
    switch (cmd) {
        case 0x10: case 0x12: case 0x13: case 0x14: case 0x20: case 0x21: case 0x22: case 0x24: case 0x25:
        case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x30: case 0x31: case 0x32:
        case 0x33: case 0x40: case 0x50: case 0x60: case 0x72:
            return 1;
        case 0x16: case 0x18: return 16;
        case 0x17: case 0x19: return 12;
        case 0x1A: return 9;
        case 0x1B: case 0x1C: case 0x70: return 3;
        case 0x23: case 0x71: return 2;
        case 0x34: return 32;
        default: return 0;  // 0x11, 0x15, 0x41 and NOPs
    }
}

uint16_t io16(uint32_t off) { return memory().io[off] | memory().io[off + 1] << 8; }
uint32_t io32(uint32_t off) { return load32(memory().io.data() + off); }

void rgb5(uint32_t c, float* out) {
    out[0] = float(c & 31);
    out[1] = float((c >> 5) & 31);
    out[2] = float((c >> 10) & 31);
}

const int32_t* clip_matrix() {
    if (g.clip_dirty) {
        std::memcpy(g.clip, g.proj, sizeof g.clip);
        mtx_mult(g.clip, g.pos);  // clip = pos * proj
        g.clip_dirty = false;
    }
    return g.clip;
}

// --- matrix commands ----------------------------------------------------------------

// Applies `op` to the matrices selected by MTX_MODE.
template <typename Op>
void on_current(Op op, bool also_vector = true) {
    switch (g.mode) {
        case 0: op(g.proj); break;
        case 1: op(g.pos); break;
        case 2: op(g.pos); if (also_vector) op(g.vec); break;
        default: op(g.tex); break;
    }
    if (g.mode != 3) g.clip_dirty = true;
}

void load_matrix(const uint32_t* p, int rows, int cols) {
    Matrix m;
    mtx_identity(m);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) m[r * 4 + c] = int32_t(p[r * cols + c]);
    }
    on_current([&](int32_t* dst) { std::memcpy(dst, m, sizeof m); });
}

void mult_matrix(const uint32_t* p, int rows, int cols) {
    Matrix m;
    mtx_identity(m);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) m[r * 4 + c] = int32_t(p[r * cols + c]);
    }
    on_current([&](int32_t* dst) { mtx_mult(dst, m); });
}

void push() {
    if (g.mode == 0) {
        if (g.proj_sp > 0) g.overflow = true;
        std::memcpy(g.proj_stack[0], g.proj, sizeof(Matrix));
        g.proj_sp = 1;
    } else if (g.mode == 3) {
        std::memcpy(g.tex_stack[0], g.tex, sizeof(Matrix));
        g.tex_sp = 1;
    } else {
        if (g.pos_sp >= 31) g.overflow = true;
        std::memcpy(g.pos_stack[g.pos_sp & 31], g.pos, sizeof(Matrix));
        std::memcpy(g.vec_stack[g.pos_sp & 31], g.vec, sizeof(Matrix));
        g.pos_sp = (g.pos_sp + 1) & 63;
    }
}

void pop(uint32_t param) {
    if (g.mode == 0) {
        if (g.proj_sp == 0) g.overflow = true;
        g.proj_sp = 0;
        std::memcpy(g.proj, g.proj_stack[0], sizeof(Matrix));
        g.clip_dirty = true;
    } else if (g.mode == 3) {
        g.tex_sp = 0;
        std::memcpy(g.tex, g.tex_stack[0], sizeof(Matrix));
    } else {
        int32_t n = sext(param, 6);
        int32_t sp = int32_t(g.pos_sp) - n;
        if (sp < 0 || sp > 30) g.overflow = true;
        g.pos_sp = uint32_t(sp) & 63;
        std::memcpy(g.pos, g.pos_stack[g.pos_sp & 31], sizeof(Matrix));
        std::memcpy(g.vec, g.vec_stack[g.pos_sp & 31], sizeof(Matrix));
        g.clip_dirty = true;
    }
}

void store(uint32_t param) {
    if (g.mode == 0) {
        std::memcpy(g.proj_stack[0], g.proj, sizeof(Matrix));
    } else if (g.mode == 3) {
        std::memcpy(g.tex_stack[0], g.tex, sizeof(Matrix));
    } else {
        uint32_t i = param & 31;
        if (i == 31) g.overflow = true;
        std::memcpy(g.pos_stack[i], g.pos, sizeof(Matrix));
        std::memcpy(g.vec_stack[i], g.vec, sizeof(Matrix));
    }
}

void restore(uint32_t param) {
    if (g.mode == 0) {
        std::memcpy(g.proj, g.proj_stack[0], sizeof(Matrix));
    } else if (g.mode == 3) {
        std::memcpy(g.tex, g.tex_stack[0], sizeof(Matrix));
    } else {
        uint32_t i = param & 31;
        if (i == 31) g.overflow = true;
        std::memcpy(g.pos, g.pos_stack[i], sizeof(Matrix));
        std::memcpy(g.vec, g.vec_stack[i], sizeof(Matrix));
    }
    g.clip_dirty = true;
}

// --- lighting -------------------------------------------------------------------------

void transform_normal(float* out, const int32_t* n) {  // by the vector matrix, n in 1.9
    for (int c = 0; c < 3; c++) {
        float s = 0;
        for (int r = 0; r < 3; r++) s += float(n[r]) / 512.0f * float(g.vec[r * 4 + c]) / 4096.0f;
        out[c] = s;
    }
}

void apply_lighting() {
    float n[3];
    transform_normal(n, g.normal);
    float out[3] = {g.emission[0], g.emission[1], g.emission[2]};
    for (int i = 0; i < 4; i++) {
        if (!(g.current_attr & (1u << i))) continue;
        const Light& l = g.lights[i];
        float dot = l.dir[0] * n[0] + l.dir[1] * n[1] + l.dir[2] * n[2];
        float diffuse = std::max(0.0f, -dot);
        float half[3] = {l.dir[0] / 2, l.dir[1] / 2, (l.dir[2] - 1.0f) / 2};
        float shine = std::max(0.0f, -(half[0] * n[0] + half[1] * n[1] + half[2] * n[2]));
        shine = std::min(1.0f, shine * shine);
        if (g.shininess_table) shine = g.shininess[std::min(127, int(shine * 127.0f))] / 255.0f;
        for (int c = 0; c < 3; c++) {
            float lc = l.color[c] / 31.0f;
            out[c] += g.specular[c] * lc * shine + g.diffuse[c] * lc * diffuse + g.ambient[c] * lc;
        }
    }
    for (int c = 0; c < 3; c++) g.color[c] = std::min(31.0f, out[c]);
}

// --- polygons ---------------------------------------------------------------------------

bool polygon_is_translucent(uint32_t attr, uint32_t tex) {
    uint32_t alpha = (attr >> 16) & 31;
    uint32_t format = (tex >> 26) & 7;
    uint32_t mode = (attr >> 4) & 3;
    return (alpha > 0 && alpha < 31) || ((format == 1 || format == 6) && mode != 1);
}

// Clips against one plane: keep where sign * coord[axis] <= w.
int clip_plane(const Vertex* in, int n, Vertex* out, int axis, int sign) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const Vertex& a = in[i];
        const Vertex& b = in[(i + 1) % n];
        float da = float(a.clip[3]) - sign * float(a.clip[axis]);
        float db = float(b.clip[3]) - sign * float(b.clip[axis]);
        if (da >= 0) out[m++] = a;
        if ((da >= 0) != (db >= 0) && m < 10) {
            float t = da / (da - db);
            Vertex v;
            for (int k = 0; k < 4; k++) v.clip[k] = int32_t(a.clip[k] + (b.clip[k] - a.clip[k]) * t);
            for (int k = 0; k < 3; k++) v.color[k] = a.color[k] + (b.color[k] - a.color[k]) * t;
            v.s = a.s + (b.s - a.s) * t;
            v.t = a.t + (b.t - a.t) * t;
            out[m++] = v;
        }
        if (m >= 10) break;
    }
    return m;
}

void submit_polygon(const Vertex* verts, int n) {
    if (g.polys.size() >= 2048) {
        return;
    }
    // Facing, from the clip-space positions (x, y, w) of the first three vertices.
    const Vertex &v0 = verts[0], &v1 = verts[1], &v2 = verts[2];
    double ax = double(v0.clip[0]) - v1.clip[0], ay = double(v0.clip[1]) - v1.clip[1], aw = double(v0.clip[3]) - v1.clip[3];
    double bx = double(v2.clip[0]) - v1.clip[0], by = double(v2.clip[1]) - v1.clip[1], bw = double(v2.clip[3]) - v1.clip[3];
    double nx = ay * bw - aw * by, ny = aw * bx - ax * bw, nw = ax * by - ay * bx;
    double dot = v1.clip[0] * nx + v1.clip[1] * ny + v1.clip[3] * nw;
    bool front = dot <= 0;
    uint32_t attr = g.current_attr;
    if (front ? !(attr & 0x80) : !(attr & 0x40)) {
        g.stat_culled++;
        return;
    }

    // Far plane: crossing polygons are dropped unless attribute bit 12 allows clipping.
    bool crosses_far = false;
    for (int i = 0; i < n; i++) {
        if (verts[i].clip[2] > verts[i].clip[3]) crosses_far = true;
    }
    if (crosses_far && !(attr & 0x1000)) return;

    Vertex a[10], b[10];
    int m = std::min(n, 10);
    std::copy(verts, verts + m, a);
    static const int kPlanes[6][2] = {{2, -1}, {2, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 1}};
    for (const auto& p : kPlanes) {
        m = clip_plane(a, m, b, p[0], p[1]);
        std::copy(b, b + m, a);
        if (m < 3) {
            g.stat_clipped++;
            return;
        }
    }

    Polygon poly;
    poly.count = m;
    poly.attr = attr;
    poly.tex = g.teximage;
    poly.pal = g.pltt;
    poly.translucent = polygon_is_translucent(attr, g.teximage);
    float vw = float(g.vp_x2 - g.vp_x1 + 1), vh = float(g.vp_y2 - g.vp_y1 + 1);
    poly.ymin = 1e9f;
    poly.ymax = -1e9f;
    for (int i = 0; i < m; i++) {
        Vertex& v = a[i];
        float w = v.clip[3] > 0 ? float(v.clip[3]) : 1.0f;
        v.sx = (float(v.clip[0]) + w) * vw / (2 * w) + float(g.vp_x1);
        v.sy = 192.0f - ((float(v.clip[1]) + w) * vh / (2 * w) + float(g.vp_y1));
        v.z = float(v.clip[2]) / w;  // -1..1
        v.w = w;
        poly.v[i] = v;
        poly.ymin = std::min(poly.ymin, v.sy);
        poly.ymax = std::max(poly.ymax, v.sy);
    }
    g.polys.push_back(poly);
    g.vertex_count += m;
}

void texcoord_from_vertex(const int32_t* v) {
    if ((g.teximage >> 30) != 3) return;
    const int32_t* m = g.tex;
    int64_t s = int64_t(v[0]) * m[0] + int64_t(v[1]) * m[4] + int64_t(v[2]) * m[8];
    int64_t t = int64_t(v[0]) * m[1] + int64_t(v[1]) * m[5] + int64_t(v[2]) * m[9];
    g.s = float((s >> 24) + g.raw_s) / 16.0f;
    g.t = float((t >> 24) + g.raw_t) / 16.0f;
}

void add_vertex() {
    if (!g.in_begin) return;
    if (g.vertex_count >= 6144) return;
    g.stat_vertices++;
    texcoord_from_vertex(g.vtx);
    int32_t in[4] = {g.vtx[0], g.vtx[1], g.vtx[2], 0x1000};
    Vertex v{};
    vec_mult(in, clip_matrix(), v.clip);
    std::copy(g.color, g.color + 3, v.color);
    v.s = g.s;
    v.t = g.t;
    g.strip.push_back(v);

    auto& s = g.strip;
    switch (g.prim) {
        case 0:  // triangles
            if (s.size() == 3) {
                submit_polygon(s.data(), 3);
                s.clear();
            }
            break;
        case 1:  // quads
            if (s.size() == 4) {
                submit_polygon(s.data(), 4);
                s.clear();
            }
            break;
        case 2:  // triangle strip
            if (s.size() == 3) {
                if (g.prim_count & 1) {
                    Vertex t[3] = {s[1], s[0], s[2]};
                    submit_polygon(t, 3);
                } else {
                    submit_polygon(s.data(), 3);
                }
                g.prim_count++;
                s.erase(s.begin());
            }
            break;
        default:  // quad strip: v0 v1 v3 v2
            if (s.size() == 4) {
                Vertex q[4] = {s[0], s[1], s[3], s[2]};
                submit_polygon(q, 4);
                s.erase(s.begin(), s.begin() + 2);
            }
            break;
    }
}

// --- tests ------------------------------------------------------------------------------

void box_test(const uint32_t* p) {
    int32_t x = sext(p[0], 16), y = sext(p[0] >> 16, 16), z = sext(p[1], 16);
    int32_t w = sext(p[1] >> 16, 16), h = sext(p[2], 16), d = sext(p[2] >> 16, 16);
    int32_t corners[8][4];
    const int32_t* m = clip_matrix();
    for (int i = 0; i < 8; i++) {
        int32_t v[4] = {x + ((i & 1) ? w : 0), y + ((i & 2) ? h : 0), z + ((i & 4) ? d : 0), 0x1000};
        vec_mult(v, m, corners[i]);
    }
    // Visible unless every corner is outside the same plane.
    g.box_result = true;
    for (int axis = 0; axis < 3; axis++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            bool all_out = true;
            for (const auto& c : corners) {
                if (sign * int64_t(c[axis]) <= c[3]) all_out = false;
            }
            if (all_out) g.box_result = false;
        }
    }
}

// --- command execution ------------------------------------------------------------------

void render_frame();

void execute(uint32_t cmd, const uint32_t* p) {
    switch (cmd) {
        case 0x10: g.mode = p[0] & 3; break;
        case 0x11: push(); break;
        case 0x12: pop(p[0]); break;
        case 0x13: store(p[0]); break;
        case 0x14: restore(p[0]); break;
        case 0x15: on_current([](int32_t* m) { mtx_identity(m); }); break;
        case 0x16: load_matrix(p, 4, 4); break;
        case 0x17: load_matrix(p, 4, 3); break;
        case 0x18: mult_matrix(p, 4, 4); break;
        case 0x19: mult_matrix(p, 4, 3); break;
        case 0x1A: mult_matrix(p, 3, 3); break;
        case 0x1B: {  // MTX_SCALE: not applied to the vector matrix
            int32_t s[3] = {int32_t(p[0]), int32_t(p[1]), int32_t(p[2])};
            on_current([&](int32_t* m) {
                for (int r = 0; r < 3; r++) {
                    for (int c = 0; c < 4; c++) m[r * 4 + c] = int32_t((int64_t(m[r * 4 + c]) * s[r]) >> 12);
                }
            }, false);
            break;
        }
        case 0x1C: {  // MTX_TRANS
            int32_t t[3] = {int32_t(p[0]), int32_t(p[1]), int32_t(p[2])};
            on_current([&](int32_t* m) {
                for (int c = 0; c < 4; c++) {
                    int64_t s = int64_t(t[0]) * m[c] + int64_t(t[1]) * m[4 + c] + int64_t(t[2]) * m[8 + c];
                    m[12 + c] += int32_t(s >> 12);
                }
            });
            break;
        }
        case 0x20: rgb5(p[0], g.color); break;
        case 0x21: {  // NORMAL
            g.normal[0] = sext(p[0], 10);
            g.normal[1] = sext(p[0] >> 10, 10);
            g.normal[2] = sext(p[0] >> 20, 10);
            if ((g.teximage >> 30) == 2) {  // texcoords from the normal
                const int32_t* m = g.tex;
                int32_t n[3] = {g.normal[0] << 3, g.normal[1] << 3, g.normal[2] << 3};  // 1.12
                int64_t s = int64_t(n[0]) * m[0] + int64_t(n[1]) * m[4] + int64_t(n[2]) * m[8];
                int64_t t = int64_t(n[0]) * m[1] + int64_t(n[1]) * m[5] + int64_t(n[2]) * m[9];
                g.s = float((s >> 21) + g.raw_s) / 16.0f;
                g.t = float((t >> 21) + g.raw_t) / 16.0f;
            }
            apply_lighting();
            break;
        }
        case 0x22: {  // TEXCOORD
            g.raw_s = sext(p[0], 16);
            g.raw_t = sext(p[0] >> 16, 16);
            if ((g.teximage >> 30) == 1) {
                const int32_t* m = g.tex;
                int64_t s = int64_t(g.raw_s) * m[0] + int64_t(g.raw_t) * m[4] + m[8] + m[12];
                int64_t t = int64_t(g.raw_s) * m[1] + int64_t(g.raw_t) * m[5] + m[9] + m[13];
                g.s = float(s >> 12) / 16.0f;
                g.t = float(t >> 12) / 16.0f;
            } else {
                g.s = float(g.raw_s) / 16.0f;
                g.t = float(g.raw_t) / 16.0f;
            }
            break;
        }
        case 0x23:
            g.vtx[0] = sext(p[0], 16);
            g.vtx[1] = sext(p[0] >> 16, 16);
            g.vtx[2] = sext(p[1], 16);
            add_vertex();
            break;
        case 0x24:
            g.vtx[0] = sext(p[0], 10) << 6;
            g.vtx[1] = sext(p[0] >> 10, 10) << 6;
            g.vtx[2] = sext(p[0] >> 20, 10) << 6;
            add_vertex();
            break;
        case 0x25: g.vtx[0] = sext(p[0], 16); g.vtx[1] = sext(p[0] >> 16, 16); add_vertex(); break;
        case 0x26: g.vtx[0] = sext(p[0], 16); g.vtx[2] = sext(p[0] >> 16, 16); add_vertex(); break;
        case 0x27: g.vtx[1] = sext(p[0], 16); g.vtx[2] = sext(p[0] >> 16, 16); add_vertex(); break;
        case 0x28:
            g.vtx[0] = int16_t(g.vtx[0] + sext(p[0], 10));
            g.vtx[1] = int16_t(g.vtx[1] + sext(p[0] >> 10, 10));
            g.vtx[2] = int16_t(g.vtx[2] + sext(p[0] >> 20, 10));
            add_vertex();
            break;
        case 0x29: g.polygon_attr = p[0]; break;
        case 0x2A: g.teximage = p[0]; break;
        case 0x2B: g.pltt = p[0] & 0x1FFF; break;
        case 0x30:  // DIF_AMB
            rgb5(p[0], g.diffuse);
            rgb5(p[0] >> 16, g.ambient);
            if (p[0] & 0x8000) std::copy(g.diffuse, g.diffuse + 3, g.color);
            break;
        case 0x31:  // SPE_EMI
            rgb5(p[0], g.specular);
            rgb5(p[0] >> 16, g.emission);
            g.shininess_table = p[0] & 0x8000;
            break;
        case 0x32: {  // LIGHT_VECTOR: transformed by the vector matrix now
            int32_t d[3] = {sext(p[0], 10), sext(p[0] >> 10, 10), sext(p[0] >> 20, 10)};
            transform_normal(g.lights[p[0] >> 30].dir, d);
            break;
        }
        case 0x33: rgb5(p[0], g.lights[p[0] >> 30].color); break;
        case 0x34:
            for (int i = 0; i < 32; i++) {
                for (int k = 0; k < 4; k++) g.shininess[i * 4 + k] = uint8_t(p[i] >> (8 * k));
            }
            break;
        case 0x40:  // BEGIN_VTXS
            g.in_begin = true;
            g.prim = p[0] & 3;
            g.strip.clear();
            g.prim_count = 0;
            g.current_attr = g.polygon_attr;
            break;
        case 0x41: g.in_begin = false; g.strip.clear(); break;
        case 0x50: {  // SWAP_BUFFERS
            g.swap_pending = true;
            for (Polygon& poly : g.polys) poly.wbuffer = p[0] & 2;
            // The hardware draws opaque polygons first, sorted by their bottom screen line and
            // then their top line; translucent ones follow, sorted the same way unless the
            // game asks for manual order (bit 0). Ties keep the submission order. The order
            // matters for the 2D-style menus drawn in 3D: digits drawn on a panel at the same
            // depth only show because they end higher on screen and are drawn first.
            bool manual = p[0] & 1;
            auto key = [](const Polygon& a) {
                int bottom = std::clamp(int(std::floor(a.ymax)), 0, 191), top = std::clamp(int(std::floor(a.ymin)), 0, 191);
                return bottom << 8 | top;
            };
            std::stable_sort(g.polys.begin(), g.polys.end(), [&](const Polygon& a, const Polygon& b) {
                if (a.translucent != b.translucent) return !a.translucent;
                if (a.translucent && manual) return false;
                return key(a) < key(b);
            });
            static const bool log = std::getenv("NTR_LOG_3D");
            if (log && g.stat_swaps++ % 60 == 0) {
                NTR_LOG("[3d] swap: %zu polygons drawn, %u vertices, %u culled, %u clipped away; DISP3DCNT %04X\n",
                        g.polys.size(), g.stat_vertices, g.stat_culled, g.stat_clipped, io16(0x60));
                NTR_LOG("[3d] commands:");
                for (auto [c, n] : g.stat_cmds) NTR_LOG(" %02X:%u", c, n);
                NTR_LOG("\n");
            }
            g.stat_vertices = g.stat_culled = g.stat_clipped = 0;
            g.stat_cmds.clear();
            render_frame();
            g.polys.clear();
            g.vertex_count = 0;
            break;
        }
        case 0x60:
            g.vp_x1 = p[0] & 0xFF;
            g.vp_y1 = (p[0] >> 8) & 0xFF;
            g.vp_x2 = (p[0] >> 16) & 0xFF;
            g.vp_y2 = (p[0] >> 24) & 0xFF;
            break;
        case 0x70: box_test(p); break;
        case 0x71: {
            g.vtx[0] = sext(p[0], 16);
            g.vtx[1] = sext(p[0] >> 16, 16);
            g.vtx[2] = sext(p[1], 16);
            int32_t v[4] = {g.vtx[0], g.vtx[1], g.vtx[2], 0x1000};
            vec_mult(v, clip_matrix(), g.pos_result);
            break;
        }
        case 0x72: {
            int32_t n[3] = {sext(p[0], 10), sext(p[0] >> 10, 10), sext(p[0] >> 20, 10)};
            for (int c = 0; c < 3; c++) {
                int64_t s = 0;
                for (int r = 0; r < 3; r++) s += int64_t(n[r] << 3) * g.vec[r * 4 + c];
                g.vec_result[c] = int16_t(sext(uint32_t(s >> 12), 16));
            }
            break;
        }
        default:
            break;
    }
}

void run(uint32_t cmd, std::vector<uint32_t>& params) {
    g.stat_cmds[cmd]++;
    params.resize(std::max<size_t>(params.size(), 32));  // commands read up to 32 words
    execute(cmd, params.data());
    params.clear();
}

// Runs leading commands of the packed word that take no parameters.
void drain_fifo() {
    while (g.fifo_left > 0) {
        uint32_t cmd = g.fifo_cmds & 0xFF;
        if (param_count(cmd) > int(g.params.size())) return;
        run(cmd, g.params);
        g.fifo_cmds >>= 8;
        g.fifo_left--;
    }
}

void fifo_write(uint32_t word) {
    if (g.fifo_left == 0) {
        if (word == 0) return;
        g.fifo_cmds = word;
        g.fifo_left = 4;
        // Trailing zero bytes are not commands.
        while (g.fifo_left > 0 && ((word >> (8 * (g.fifo_left - 1))) & 0xFF) == 0) g.fifo_left--;
        drain_fifo();
        return;
    }
    g.params.push_back(word);
    drain_fifo();
}

void port_write(uint32_t cmd, uint32_t value) {
    int n = param_count(cmd);
    if (n == 0) {
        std::vector<uint32_t> none;
        run(cmd, none);
        return;
    }
    if (g.port_cmd != cmd) {
        g.port_cmd = cmd;
        g.port_params.clear();
    }
    g.port_params.push_back(value);
    if (int(g.port_params.size()) >= n) {
        run(cmd, g.port_params);
        g.port_cmd = 0;
    }
}

// --- rendering engine -------------------------------------------------------------------

int g_wanted_scale = 1;  // gpu3d_set_scale
int g_scale = 1;         // of the frame being rendered (native while fast-forwarding)
int g_front_scale = 1;   // of the frame on screen
int W = kScreenWidth, H = kScreenHeight;  // render size: native size times g_scale

struct Frame {
    std::vector<uint16_t> color;  // BGR555
    std::vector<uint8_t> alpha;   // 0-31
    void resize(size_t n) {
        color.assign(n, 0);
        alpha.assign(n, 0);
    }
};
Frame g_back, g_front;  // render size
Frame g_native;         // g_front at 256x192, for the 2D engine and display capture

struct Pixel {
    uint32_t depth;
    uint8_t id;
    bool fog;
    bool translucent;
    bool stencil;
    bool edge;            // opaque polygon pixel (edge marking source)
    uint8_t coverage;     // anti-aliasing: 0-31 share of the pixel the top polygon covers
    uint8_t under_alpha;  // what the top opaque pixel was drawn over (anti-aliasing)
    uint16_t under_color;
};
std::vector<Pixel> g_attr;

// The front frame keeps its size until the next swap, so a scale change never blanks it.
void resize_buffers() {
    W = kScreenWidth * g_scale;
    H = kScreenHeight * g_scale;
    g_back.resize(size_t(W) * H);
    g_attr.assign(size_t(W) * H, Pixel{});
    if (g_native.color.empty()) g_native.resize(size_t(kScreenWidth) * kScreenHeight);
}

struct Texel {
    uint8_t r, g, b, a;  // 5-bit
};

// Looked up once per frame: the views do not change while a frame renders.
const VramView* g_tex_view = nullptr;
const VramView* g_pal_view = nullptr;

uint16_t tex16(uint32_t off) { return g_tex_view->read16(off); }
uint8_t tex8(uint32_t off) { return g_tex_view->read8(off); }
uint16_t pal16(uint32_t off) { return g_pal_view->read16(off); }

Texel make_texel(uint16_t c, uint8_t a) { return {uint8_t(c & 31), uint8_t((c >> 5) & 31), uint8_t((c >> 10) & 31), a}; }

Texel sample(const Polygon& p, float fs, float ft) {
    uint32_t tex = p.tex;
    uint32_t format = (tex >> 26) & 7;
    int32_t w = 8 << ((tex >> 20) & 7), h = 8 << ((tex >> 23) & 7);
    int32_t s = int32_t(std::floor(fs)), t = int32_t(std::floor(ft));
    auto wrap = [](int32_t v, int32_t size, bool repeat, bool flip) {
        if (!repeat) return std::clamp(v, 0, size - 1);
        if (flip) {
            int32_t period = v & (size * 2 - 1);
            return period >= size ? size * 2 - 1 - period : period;
        }
        return v & (size - 1);
    };
    s = wrap(s, w, tex & (1u << 16), tex & (1u << 18));
    t = wrap(t, h, tex & (1u << 17), tex & (1u << 19));
    uint32_t base = (tex & 0xFFFF) << 3;
    uint32_t idx = uint32_t(t * w + s);
    bool zero_transparent = tex & (1u << 29);
    uint32_t pal = p.pal;
    switch (format) {
        case 1: {  // A3I5
            uint8_t v = tex8(base + idx);
            uint8_t a3 = v >> 5;
            return make_texel(pal16((pal << 4) + (v & 31) * 2), uint8_t(a3 * 4 + a3 / 2));
        }
        case 2: {  // 4 colors
            uint8_t v = (tex8(base + idx / 4) >> ((idx & 3) * 2)) & 3;
            if (v == 0 && zero_transparent) return {0, 0, 0, 0};
            return make_texel(pal16((pal << 3) + v * 2), 31);
        }
        case 3: {  // 16 colors
            uint8_t v = (tex8(base + idx / 2) >> ((idx & 1) * 4)) & 15;
            if (v == 0 && zero_transparent) return {0, 0, 0, 0};
            return make_texel(pal16((pal << 4) + v * 2), 31);
        }
        case 4: {  // 256 colors
            uint8_t v = tex8(base + idx);
            if (v == 0 && zero_transparent) return {0, 0, 0, 0};
            return make_texel(pal16((pal << 4) + v * 2), 31);
        }
        case 5: {  // 4x4 compressed
            uint32_t block = uint32_t((t / 4) * (w / 4) + s / 4);
            uint32_t word_addr = base + block * 4;
            uint32_t bits = tex8(word_addr + (t & 3));
            uint32_t v = (bits >> ((s & 3) * 2)) & 3;
            uint32_t slot_off = word_addr & 0x1FFFF;
            uint32_t info_addr = 0x20000 + (slot_off >> 1) + ((word_addr >> 17) == 2 ? 0x10000 : 0);
            uint16_t info = tex16(info_addr);
            uint32_t pbase = (pal << 4) + (info & 0x3FFF) * 4;
            uint32_t mode = info >> 14;
            auto pc = [&](int i) { return pal16(pbase + i * 2); };
            auto mix = [](uint16_t a, uint16_t b, int wa, int wb, int div) {
                uint16_t out = 0;
                for (int sh = 0; sh < 15; sh += 5) out |= uint16_t(((((a >> sh) & 31) * wa + ((b >> sh) & 31) * wb) / div) << sh);
                return out;
            };
            switch (mode) {
                case 0: return v == 3 ? Texel{0, 0, 0, 0} : make_texel(pc(v), 31);
                case 1:
                    if (v == 3) return {0, 0, 0, 0};
                    return make_texel(v == 2 ? mix(pc(0), pc(1), 1, 1, 2) : pc(v), 31);
                case 2: return make_texel(pc(v), 31);
                default:
                    if (v == 2) return make_texel(mix(pc(0), pc(1), 5, 3, 8), 31);
                    if (v == 3) return make_texel(mix(pc(0), pc(1), 3, 5, 8), 31);
                    return make_texel(pc(v), 31);
            }
        }
        case 6: {  // A5I3
            uint8_t v = tex8(base + idx);
            return make_texel(pal16((pal << 4) + (v & 7) * 2), v >> 3);
        }
        case 7: {  // direct color
            uint16_t c = tex16(base + idx * 2);
            return make_texel(c, (c & 0x8000) ? 31 : 0);
        }
        default:
            return {31, 31, 31, 31};
    }
}

inline uint32_t to6(uint32_t c5) { return c5 ? c5 * 2 + 1 : 0; }

// Combines vertex color and texel; returns 5-bit components and alpha.
Texel shade(const Polygon& p, const float* vcol, float s, float t, uint32_t poly_alpha, uint32_t disp3d) {
    uint32_t mode = (p.attr >> 4) & 3;
    uint32_t vr = uint32_t(vcol[0]), vg = uint32_t(vcol[1]), vb = uint32_t(vcol[2]);
    bool textured = ((p.tex >> 26) & 7) != 0 && (disp3d & 1);
    if (mode == 2) {  // toon / highlight: the vertex red component indexes the table
        uint16_t toon = io16(0x380 + (std::min(vr, 31u)) * 2);
        if (disp3d & 2) {  // highlight: shade comes from the table, added after modulation
            Texel base{uint8_t(vr), uint8_t(vr), uint8_t(vr), uint8_t(poly_alpha)};
            Texel tx = textured ? sample(p, s, t) : Texel{31, 31, 31, 31};
            uint32_t c[3] = {base.r, base.g, base.b}, x[3] = {tx.r, tx.g, tx.b};
            uint32_t tc[3] = {toon & 31u, (toon >> 5) & 31u, (toon >> 10) & 31u};
            Texel out;
            uint8_t* o[3] = {&out.r, &out.g, &out.b};
            for (int i = 0; i < 3; i++) {
                uint32_t m = ((to6(x[i]) + 1) * (to6(c[i]) + 1) - 1) / 64;
                *o[i] = uint8_t(std::min(31u, (m + to6(tc[i])) >> 1));
            }
            out.a = uint8_t(((to6(tx.a) + 1) * (to6(poly_alpha) + 1) - 1) / 64 >> 1);
            return out;
        }
        vr = toon & 31;
        vg = (toon >> 5) & 31;
        vb = (toon >> 10) & 31;
    }
    if (!textured) return {uint8_t(vr), uint8_t(vg), uint8_t(vb), uint8_t(poly_alpha)};
    Texel tx = sample(p, s, t);
    if (mode == 1) {  // decal
        if (tx.a == 0) return {uint8_t(vr), uint8_t(vg), uint8_t(vb), uint8_t(poly_alpha)};
        if (tx.a == 31) return {tx.r, tx.g, tx.b, uint8_t(poly_alpha)};
        auto d = [&](uint32_t t5, uint32_t v5) {
            return uint8_t(((to6(t5) * tx.a + to6(v5) * (31 - tx.a)) / 31) >> 1);
        };
        return {d(tx.r, vr), d(tx.g, vg), d(tx.b, vb), uint8_t(poly_alpha)};
    }
    auto m = [](uint32_t a, uint32_t b) { return uint8_t((((to6(a) + 1) * (to6(b) + 1) - 1) / 64) >> 1); };
    return {m(tx.r, vr), m(tx.g, vg), m(tx.b, vb), m(tx.a, poly_alpha)};
}

uint16_t pack(const Texel& t) { return uint16_t(t.r | t.g << 5 | t.b << 10); }

uint16_t blend_colors(uint16_t src, uint16_t dst, uint32_t a) {  // a = 0..31 source weight
    uint16_t out = 0;
    for (int sh = 0; sh < 15; sh += 5) {
        uint32_t s = (src >> sh) & 31, d = (dst >> sh) & 31;
        out |= uint16_t(((s * (a + 1) + d * (31 - a)) / 32) << sh);
    }
    return out;
}

uint32_t depth_value(const Polygon& p, float z, float w) {
    if (p.wbuffer) return uint32_t(std::clamp(w, 0.0f, float(0xFFFFFF)));
    float d = (z * 0x4000 + 0x3FFF) * 0x200;
    return uint32_t(std::clamp(d, 0.0f, float(0xFFFFFF)));
}

// Draws the rows y with y % bands == band (bands render in parallel; each one draws every
// polygon in list order, so the result does not depend on how many there are).
void draw_polygon(const Polygon& p, int band, int bands) {
    uint32_t attr = p.attr;
    uint32_t poly_alpha = (attr >> 16) & 31;
    bool wireframe = poly_alpha == 0;  // only the outline is drawn, opaque
    if (wireframe) poly_alpha = 31;
    uint32_t mode = (attr >> 4) & 3;
    uint32_t id = (attr >> 24) & 63;
    uint32_t disp3d = io16(0x60);
    bool alpha_test = disp3d & 4;
    uint32_t alpha_ref = io16(0x340) & 31;
    bool blending = disp3d & 8;
    bool antialias = disp3d & 0x10;
    bool depth_equal = attr & (1u << 14);
    bool update_depth = !p.translucent || (attr & (1u << 11));
    bool shadow_mask = mode == 3 && id == 0;
    float scale = float(g_scale);

    // Fan triangulation. Edge k of a triangle is the one opposite its vertex k; only the
    // polygon's own sides (not the diagonals between fan triangles) are outline edges.
    for (int k = 1; k + 1 < p.count; k++) {
        const Vertex* v[3] = {&p.v[0], &p.v[k], &p.v[k + 1]};
        float sx[3], sy[3];
        for (int i = 0; i < 3; i++) {
            sx[i] = v[i]->sx * scale;
            sy[i] = v[i]->sy * scale;
        }
        bool outer[3] = {true, k + 2 == p.count, k == 1};
        float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (std::fabs(area) < 1e-6f) continue;
        float edge_scale[3];  // barycentric weight -> distance to that edge, in pixels
        for (int i = 0; i < 3; i++) {
            int a = (i + 1) % 3, c = (i + 2) % 3;
            float len = std::hypot(sx[c] - sx[a], sy[c] - sy[a]);
            edge_scale[i] = len > 0 ? std::fabs(area) / len : 0;
        }
        int x0 = std::max(0, int(std::floor(std::min({sx[0], sx[1], sx[2]}))));
        int x1 = std::min(W - 1, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
        int y0 = std::max(0, int(std::floor(std::min({sy[0], sy[1], sy[2]}))));
        int y1 = std::min(H - 1, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
        float inv_w[3], cw[3][3], sw[3], tw[3];
        for (int i = 0; i < 3; i++) {
            inv_w[i] = 1.0f / v[i]->w;
            for (int c = 0; c < 3; c++) cw[i][c] = v[i]->color[c] * inv_w[i];
            sw[i] = v[i]->s * inv_w[i];
            tw[i] = v[i]->t * inv_w[i];
        }
        int first_row = y0 + ((band - y0 % bands) % bands + bands) % bands;
        for (int y = first_row; y <= y1; y += bands) {
            float py = y + 0.5f;
            // Span of pixel centers inside all three edges, widened by a pixel on each side:
            // the exact test below still decides every pixel.
            double lo = x0, hi = x1 + 1;
            for (int i = 0; i < 3; i++) {
                int a = (i + 1) % 3, c = (i + 2) % 3;
                // e(px) = k - d * px must have the sign of area.
                double d = double(sy[c] - sy[a]) * (area > 0 ? 1 : -1);
                double k = (double(sx[c] - sx[a]) * (py - sy[a]) + double(sy[c] - sy[a]) * sx[a]) * (area > 0 ? 1 : -1);
                if (d > 0) hi = std::min(hi, k / d);
                else if (d < 0) lo = std::max(lo, k / d);
                else if (k < 0) lo = hi + 1;
            }
            if (lo > hi + 1) continue;
            int xa = std::max(x0, int(std::floor(lo - 0.5)) - 1);
            int xb = std::min(x1, int(std::ceil(hi - 0.5)) + 1);
            for (int x = xa; x <= xb; x++) {
                float px = x + 0.5f;
                float b[3];
                bool inside = true;
                for (int i = 0; i < 3; i++) {
                    int a = (i + 1) % 3, c = (i + 2) % 3;
                    float e = (sx[c] - sx[a]) * (py - sy[a]) - (sy[c] - sy[a]) * (px - sx[a]);
                    b[i] = e / area;
                    if (b[i] < 0) {
                        inside = false;
                        break;
                    }
                }
                if (!inside) continue;
                float edge_dist = 1e9f;  // to the nearest outline edge
                for (int i = 0; i < 3; i++) {
                    if (outer[i]) edge_dist = std::min(edge_dist, b[i] * edge_scale[i]);
                }
                if (wireframe && edge_dist >= scale) continue;
                int at = y * W + x;
                Pixel& pix = g_attr[at];
                float z = b[0] * v[0]->z + b[1] * v[1]->z + b[2] * v[2]->z;
                float iw = b[0] * inv_w[0] + b[1] * inv_w[1] + b[2] * inv_w[2];
                float w = 1.0f / iw;
                uint32_t depth = depth_value(p, z, w);
                bool pass = depth_equal ? (depth >= pix.depth ? depth - pix.depth : pix.depth - depth) <= 0x200
                                        : depth < pix.depth;
                if (shadow_mask) {  // shadow volume: mark where the depth test fails
                    if (!pass) pix.stencil = true;
                    continue;
                }
                if (!pass) continue;
                if (mode == 3) {  // shadow polygon: only where the mask was set
                    if (!pix.stencil || pix.id == id) continue;
                    pix.stencil = false;
                }
                float col[3];
                for (int c = 0; c < 3; c++) col[c] = (b[0] * cw[0][c] + b[1] * cw[1][c] + b[2] * cw[2][c]) * w;
                float s = (b[0] * sw[0] + b[1] * sw[1] + b[2] * sw[2]) * w;
                float t = (b[0] * tw[0] + b[1] * tw[1] + b[2] * tw[2]) * w;
                Texel out = shade(p, col, s, t, poly_alpha, disp3d);
                if (out.a == 0) continue;
                if (alpha_test && out.a <= alpha_ref) continue;
                uint16_t color = pack(out);
                if (out.a < 31) {
                    // A translucent polygon does not draw over its own ID twice.
                    if (pix.translucent && pix.id == id) continue;
                    uint8_t dst_a = g_back.alpha[at];
                    if (blending && dst_a > 0) {
                        color = blend_colors(color, g_back.color[at], out.a);
                        out.a = uint8_t(std::max<uint32_t>(out.a, dst_a));
                    }
                    pix.translucent = true;
                    pix.coverage = 31;
                } else {
                    pix.translucent = false;
                    pix.edge = true;
                    pix.under_color = g_back.color[at];
                    pix.under_alpha = g_back.alpha[at];
                    // Pixel centers less than half a pixel inside an outline edge are
                    // partly covered: 0.5 at the edge, 1 half a pixel in.
                    float cover = antialias ? std::min(1.0f, 0.5f + edge_dist / scale) : 1.0f;
                    pix.coverage = uint8_t(cover * 31 + 0.5f);
                }
                g_back.color[at] = color;
                g_back.alpha[at] = out.a;
                if (update_depth) pix.depth = depth;
                pix.id = uint8_t(id);
                pix.fog = attr & (1u << 15);
            }
        }
    }
}

// Edge marking, anti-aliasing and fog for the rows of one band. Each pass changes only
// the pixel it looks at and reads the attribute buffer, so rows are independent.
void post_process(int band, int bands) {
    uint32_t disp3d = io16(0x60);
    static const int kN[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    uint32_t fog_color = io32(0x358);
    uint32_t fog_alpha = (fog_color >> 16) & 31;
    uint32_t offset = io16(0x35C) & 0x7FFF;
    uint32_t shift = (disp3d >> 8) & 15;
    bool alpha_only = disp3d & 0x40;
    const uint8_t* fog_table = memory().io.data() + 0x360;
    for (int y = band; y < H; y += bands) {
        if (disp3d & 0x20) {  // edge marking: opaque pixels next to another ID that is farther away
            for (int x = 0; x < W; x++) {
                const Pixel& p = g_attr[y * W + x];
                if (!p.edge) continue;
                // Neighbors one native pixel away keep the outline as thick as on hardware.
                for (const auto& n : kN) {
                    int nx = x + n[0] * g_scale, ny = y + n[1] * g_scale;
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    const Pixel& q = g_attr[ny * W + nx];
                    if (q.id != p.id && p.depth < q.depth) {
                        g_back.color[y * W + x] = io16(0x330 + (p.id >> 3) * 2) & 0x7FFF;
                        break;
                    }
                }
            }
        }
        if (disp3d & 0x10) {  // anti-aliasing: blend partly covered silhouette pixels
            for (int x = 0; x < W; x++) {
                int at = y * W + x;
                const Pixel& p = g_attr[at];
                if (!p.edge || p.coverage >= 31) continue;
                // Only where the polygon meets something else: the sides shared by
                // polygons of one model stay solid.
                bool silhouette = false;
                for (const auto& n : kN) {
                    int nx = x + n[0], ny = y + n[1];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    const Pixel& q = g_attr[ny * W + nx];
                    if (q.id != p.id || !q.edge) silhouette = true;
                }
                if (!silhouette) continue;
                if (p.under_alpha == 0) {
                    g_back.alpha[at] = p.coverage;  // the 2D layers below show through
                } else {
                    g_back.color[at] = blend_colors(g_back.color[at], p.under_color, p.coverage);
                }
            }
        }
        if (disp3d & 0x80) {  // fog
            for (int i = y * W; i < (y + 1) * W; i++) {
                const Pixel& p = g_attr[i];
                if (!p.fog) continue;
                uint32_t d = p.depth >> 9;
                uint32_t density;
                if (d < offset) {
                    density = fog_table[0] & 0x7F;
                } else {
                    uint32_t pos = (d - offset) << shift;
                    uint32_t idx = pos >> 10, frac = pos & 0x3FF;
                    if (idx >= 31) {
                        density = fog_table[31] & 0x7F;
                    } else {
                        uint32_t a = fog_table[idx] & 0x7F, b = fog_table[idx + 1] & 0x7F;
                        density = (a * (0x400 - frac) + b * frac) >> 10;
                    }
                }
                if (density == 127) density = 128;
                if (!alpha_only) {
                    uint16_t c = g_back.color[i], out = 0;
                    for (int sh = 0; sh < 15; sh += 5) {
                        uint32_t pc = (c >> sh) & 31, fc = (fog_color >> sh) & 31;
                        out |= uint16_t(((fc * density + pc * (128 - density)) >> 7) << sh);
                    }
                    g_back.color[i] = out;
                }
                g_back.alpha[i] = uint8_t((fog_alpha * density + g_back.alpha[i] * (128 - density)) >> 7);
            }
        }
    }
}

// Worker threads for the rendering bands. run(f) calls f(band) for every band, the
// calling thread doing band 0, and returns when all are done.
class BandPool {
public:
    explicit BandPool(int bands) : bands_(bands) {
        // Detached and never destroyed: the pool lives until the process exits.
        for (int i = 1; i < bands_; i++) std::thread([this, i] { work(i); }).detach();
    }
    int bands() const { return bands_; }
    template <class F>
    void run(F f) {
        if (bands_ == 1) {
            f(0);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            job_ = f;
            pending_ = bands_ - 1;
            generation_++;
        }
        start_.notify_all();
        f(0);
        std::unique_lock lock(mutex_);
        done_.wait(lock, [this] { return pending_ == 0; });
    }

private:
    void work(int band) {
        uint64_t seen = 0;
        for (;;) {
            std::function<void(int)> job;
            {
                std::unique_lock lock(mutex_);
                start_.wait(lock, [&] { return generation_ != seen; });
                seen = generation_;
                job = job_;
            }
            job(band);
            std::lock_guard lock(mutex_);
            if (--pending_ == 0) done_.notify_one();
        }
    }

    int bands_;
    std::mutex mutex_;
    std::condition_variable start_, done_;
    std::function<void(int)> job_;
    uint64_t generation_ = 0;
    int pending_ = 0;
};

// NTR_3D_THREADS overrides the thread count (1 = render on the emulation thread only).
BandPool& band_pool() {
    static BandPool* pool = new BandPool([] {
        int n = int(std::thread::hardware_concurrency());
        if (const char* env = std::getenv("NTR_3D_THREADS")) n = std::atoi(env);
        return std::clamp(n, 1, 8);
    }());
    return *pool;
}

void render_frame() {
    // Fast forward shows few frames; rendering them at native size keeps it fast.
    int scale = host_fast_forward() ? 1 : g_wanted_scale;
    if (scale != g_scale || g_attr.empty()) {
        g_scale = scale;
        resize_buffers();
    }
    uint32_t clear = io32(0x350);
    uint32_t clear_depth = io16(0x354) & 0x7FFF;
    uint32_t depth = clear_depth * 0x200 + ((clear_depth + 1) / 0x8000) * 0x1FF;
    Pixel cleared{depth, uint8_t((clear >> 24) & 63), bool(clear & 0x8000), false, false, false, 31, 0, 0};
    std::fill(g_attr.begin(), g_attr.end(), cleared);
    std::fill(g_back.color.begin(), g_back.color.end(), uint16_t(clear & 0x7FFF));
    std::fill(g_back.alpha.begin(), g_back.alpha.end(), uint8_t((clear >> 16) & 31));
    g_tex_view = &vram_view(VramArea::Texture);
    g_pal_view = &vram_view(VramArea::TexPalette);
    BandPool& pool = band_pool();
    pool.run([&](int band) {
        for (const Polygon& p : g.polys) draw_polygon(p, band, pool.bands());
    });
    // Edge marking and anti-aliasing look at neighboring rows: all bands must be done.
    pool.run([&](int band) { post_process(band, pool.bands()); });
}

}  // namespace

void gpu3d_write(uint32_t addr, uint32_t value) {
    if (addr < 0x04000440) {  // 0x04000400-0x0400043F all reach the FIFO
        fifo_write(value);
    } else if (addr >= 0x04000440 && addr < 0x04000600) {
        port_write((addr - 0x04000400) >> 2, value);
    }
}

void gpu3d_write_gxstat(uint32_t value) {
    if (value & 0x8000) {  // acknowledge a matrix stack error
        g.overflow = false;
        g.proj_sp = 0;
    }
}

uint32_t gpu3d_read(uint32_t addr, uint32_t stored) {
    switch (addr) {
        case 0x04000600: {
            uint32_t v = stored & 0xC0000000;
            v |= g.box_result ? 2 : 0;
            v |= (g.pos_sp & 31) << 8;
            v |= (g.proj_sp & 1) << 13;
            v |= g.overflow ? 0x8000 : 0;
            v |= 0x06000000;  // FIFO empty and less than half full; geometry engine idle
            return v;
        }
        case 0x04000604:
            return uint32_t(g.polys.size()) | g.vertex_count << 16;
        default:
            break;
    }
    if (addr >= 0x04000620 && addr < 0x04000630) return uint32_t(g.pos_result[(addr - 0x04000620) >> 2]);
    if (addr >= 0x04000630 && addr < 0x04000638) {
        uint32_t i = (addr - 0x04000630) >> 1;
        return uint16_t(g.vec_result[i]) | (i + 1 < 3 ? uint32_t(uint16_t(g.vec_result[i + 1])) << 16 : 0);
    }
    if (addr >= 0x04000640 && addr < 0x04000680) return uint32_t(clip_matrix()[(addr - 0x04000640) >> 2]);
    if (addr >= 0x04000680 && addr < 0x040006A4) {
        uint32_t i = (addr - 0x04000680) >> 2;
        return uint32_t(g.vec[(i / 3) * 4 + i % 3]);
    }
    return stored;
}

void gpu3d_set_scale(int scale) { g_wanted_scale = std::clamp(scale, 1, kMax3dScale); }

int gpu3d_scale() { return g_front_scale; }

void gpu3d_init() {
    if (g_attr.empty()) {
        resize_buffers();
        g_front = g_back;
    }
    mtx_identity(g.proj);
    mtx_identity(g.pos);
    mtx_identity(g.vec);
    mtx_identity(g.tex);
}

void gpu3d_vblank() {
    if (!g.swap_pending) return;
    g.swap_pending = false;
    g_front.color = g_back.color;
    g_front.alpha = g_back.alpha;
    g_front_scale = g_scale;
    // One sample per native pixel, at its center.
    int half = g_scale / 2;
    for (int y = 0; y < kScreenHeight; y++) {
        for (int x = 0; x < kScreenWidth; x++) {
            size_t from = size_t(y * g_scale + half) * W + x * g_scale + half;
            g_native.color[y * kScreenWidth + x] = g_front.color[from];
            g_native.alpha[y * kScreenWidth + x] = g_front.alpha[from];
        }
    }
}

void gpu3d_irq_check() {
    // GXSTAT bits 30-31: IRQ while the FIFO is less than half full (1) or empty (2). The
    // FIFO is always empty here, so either condition holds.
    if (io32(0x600) >> 30 & 3) raise_irq(1u << 21);
}

const uint16_t* gpu3d_line_color(uint32_t line) { return g_native.color.data() + line * kScreenWidth; }
const uint8_t* gpu3d_line_alpha(uint32_t line) { return g_native.alpha.data() + line * kScreenWidth; }
const uint16_t* gpu3d_hi_row_color(uint32_t row) {
    return g_front.color.data() + size_t(row) * kScreenWidth * g_front_scale;
}
const uint8_t* gpu3d_hi_row_alpha(uint32_t row) {
    return g_front.alpha.data() + size_t(row) * kScreenWidth * g_front_scale;
}

}  // namespace ntr
