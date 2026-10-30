/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "boxart.hpp"
#include "../system/pair_record.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <png.h>
#include <setjmp.h>
#include <sys/stat.h>

namespace boxart {

static const char *CACHE_ROOT = MOONLIGHT_3DS_PATH "/boxart";
// Bumped whenever the cache format changes so stale SD caches are redone
// ("BXA5": the header records the picture's fingerprint and which of the
// hosts' default pictures it was, box.png included).
static const u32 CACHE_MAGIC = 0x35415842; // "BXA5"

struct CacheHeader {
    u32 magic;
    u32 tex_w;
    u32 tex_h;
    u32 kind; // Default
    u32 src_fnv_lo;
    u32 src_fnv_hi;
    u32 src_size;
    u32 reserved;
};

// FNV-1a fingerprints of the default pictures, from Sunshine's and Apollo's
// src_assets/common/assets (every version in their history)
struct KnownDefault {
    u64 fnv;
    u32 size;
    Default kind;
};
static const KnownDefault KNOWN_DEFAULTS[] = {
    {0xc733836ab194c5a9ULL, 8231, Default::Desktop},   // desktop.png
    {0xde8dc9d3f6633eedULL, 8737, Default::Desktop},   // desktop-alt.png
    {0x26ba5ce4dea6340cULL, 31433, Default::Desktop},  // older desktop.png
    {0x6477d911cfe0ce4dULL, 32211, Default::Desktop},  // older desktop-alt
    {0xb7aac8da6ba22ec1ULL, 24833, Default::Steam},    // steam.png
    {0xe8a06a2099213d99ULL, 25012, Default::Steam},    // older steam.png
    {0x964e8fc046b60986ULL, 51024, Default::Steam},    // older steam.png
    {0xc844c1890e7e1af2ULL, 98678, Default::VirtualDisplay}, // Apollo
    {0x598071ab2d7e2dd9ULL, 101584, Default::RemoteInput},   // Apollo
    {0x30caf0963460f93cULL, 101768, Default::RemoteInput},   // older Apollo
    {0x2259a68fdd069d29ULL, 680, Default::Generic},          // box.png
    {0x392cdbc49843f29cULL, 998, Default::Generic},          // older box.png
};

Source fingerprint(const u8 *data, size_t size) {
    Source s;
    s.fnv = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < size; i++) {
        s.fnv = (s.fnv ^ data[i]) * 0x100000001b3ULL;
    }
    s.size = (u32)size;
    return s;
}

Default identify_default(const Source &source) {
    for (const auto &k : KNOWN_DEFAULTS) {
        if (k.size == source.size && k.fnv == source.fnv) {
            return k.kind;
        }
    }
    return Default::None;
}

void ensure_cache_dir(const std::string &host_key) {
    mkdir(MOONLIGHT_3DS_PATH, 0777);
    mkdir(CACHE_ROOT, 0777);
    std::string dir = std::string(CACHE_ROOT) + "/" + host_key;
    mkdir(dir.c_str(), 0777);
}

std::string cache_path(const std::string &host_key, int app_id) {
    return std::string(CACHE_ROOT) + "/" + host_key + "/" +
           std::to_string(app_id) + ".tex";
}

bool load_cached(const std::string &path, std::vector<u8> &tiled,
                 Default &kind, Source &source) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    CacheHeader header;
    bool ok = fread(&header, sizeof(header), 1, f) == 1 &&
              header.magic == CACHE_MAGIC && header.tex_w == TEX_W &&
              header.tex_h == TEX_H &&
              header.kind <= (u32)Default::Generic;
    if (ok) {
        kind = (Default)header.kind;
        source.fnv = ((u64)header.src_fnv_hi << 32) | header.src_fnv_lo;
        source.size = header.src_size;
        tiled.resize(TILED_BYTES);
        ok = fread(tiled.data(), 1, TILED_BYTES, f) == TILED_BYTES;
    }
    fclose(f);
    if (!ok) {
        tiled.clear();
    }
    return ok;
}

void save_cache(const std::string &path, const std::vector<u8> &tiled,
                Default kind, const Source &source) {
    if (tiled.size() != TILED_BYTES) {
        return;
    }
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        return;
    }
    CacheHeader header{CACHE_MAGIC,
                       TEX_W,
                       TEX_H,
                       (u32)kind,
                       (u32)(source.fnv & 0xFFFFFFFFu),
                       (u32)(source.fnv >> 32),
                       source.size,
                       0};
    fwrite(&header, sizeof(header), 1, f);
    fwrite(tiled.data(), 1, TILED_BYTES, f);
    fclose(f);
}

// ---- Tiled texture layout -------------------------------------------------

// Z-order (Morton) index of a pixel inside an 8x8 tile, x in the low bit.
static inline u32 morton_interleave(u32 x, u32 y) {
    u32 i = (x & 7) | ((y & 7) << 8);
    i = (i ^ (i << 2)) & 0x1313;
    i = (i ^ (i << 1)) & 0x1515;
    i = (i | (i >> 7)) & 0x3F;
    return i;
}

static inline size_t tiled_index(u32 x, u32 y) {
    u32 tile = (y >> 3) * (TEX_W >> 3) + (x >> 3);
    return (size_t)tile * 64 + morton_interleave(x, y);
}

static inline u16 to_rgb565(u8 r, u8 g, u8 b) {
    return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Packs a 96x128 RGBA8 image (top row first) into the tiled texture.
// Verified on hardware with the unrotated sub-texture (top = 1.0,
// bottom = 0.0): rows stored top-down and x unchanged draw correctly.
static void pack_tiled(const u8 *rgba, std::vector<u8> &tiled) {
    tiled.assign(TILED_BYTES, 0);
    u16 *dst = reinterpret_cast<u16 *>(tiled.data());
    for (int y = 0; y < IMG_H; y++) {
        const u8 *row = rgba + (size_t)y * IMG_W * 4;
        for (int x = 0; x < IMG_W; x++) {
            dst[tiled_index(x, y)] =
                to_rgb565(row[x * 4], row[x * 4 + 1], row[x * 4 + 2]);
        }
    }
}

// ---- PNG decoding ---------------------------------------------------------

struct MemReader {
    const u8 *data;
    size_t size;
    size_t pos;
};

static void png_mem_read(png_structp png, png_bytep out, png_size_t len) {
    MemReader *r = static_cast<MemReader *>(png_get_io_ptr(png));
    if (r->pos + len > r->size) {
        png_error(png, "unexpected end of PNG data");
        return;
    }
    memcpy(out, r->data + r->pos, len);
    r->pos += len;
}

static void png_quiet_error(png_structp png, png_const_charp msg) {
    png_longjmp(png, 1);
}

static void png_quiet_warning(png_structp png, png_const_charp msg) {}

struct DecodeBuffers {
    u8 *row = nullptr;
    u32 *acc = nullptr;
    u8 *rgba = nullptr;
};

// Everything that can longjmp lives here; the caller owns the buffers so no
// C++ destructors are skipped when libpng bails out.
static bool decode_body(png_structp png, png_infop info, DecodeBuffers &buf,
                        std::vector<u8> &tiled) {
    png_read_info(png, info);

    png_uint_32 w = 0, h = 0;
    int bit_depth = 0, color_type = 0, interlace = 0;
    png_get_IHDR(png, info, &w, &h, &bit_depth, &color_type, &interlace,
                 nullptr, nullptr);
    if (w == 0 || h == 0 || w > 4096 || h > 4096 ||
        interlace != PNG_INTERLACE_NONE) {
        return false;
    }

    // Normalize every input to 8-bit RGBA.
    if (bit_depth == 16) {
        png_set_strip_16(png);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    png_read_update_info(png, info);
    if (png_get_rowbytes(png, info) != (size_t)w * 4) {
        return false;
    }

    // Fit inside 96x128 keeping the aspect ratio; letterbox the rest.
    float scale = std::min((float)IMG_W / (float)w, (float)IMG_H / (float)h);
    int dw = std::max(1, (int)((float)w * scale + 0.5f));
    int dh = std::max(1, (int)((float)h * scale + 0.5f));
    dw = std::min(dw, IMG_W);
    dh = std::min(dh, IMG_H);
    int ox = (IMG_W - dw) / 2;
    int oy = (IMG_H - dh) / 2;

    buf.row = (u8 *)malloc((size_t)w * 4);
    // Per target pixel: alpha-weighted r, g, b, then alpha sum, then count.
    buf.acc = (u32 *)calloc((size_t)IMG_W * IMG_H * 5, sizeof(u32));
    buf.rgba = (u8 *)malloc((size_t)IMG_W * IMG_H * 4);
    if (!buf.row || !buf.acc || !buf.rgba) {
        return false;
    }

    for (png_uint_32 y = 0; y < h; y++) {
        png_read_row(png, buf.row, nullptr);
        int ty = oy + (int)(((u64)y * dh) / h);
        if (ty >= IMG_H) {
            ty = IMG_H - 1;
        }
        for (png_uint_32 x = 0; x < w; x++) {
            int tx = ox + (int)(((u64)x * dw) / w);
            if (tx >= IMG_W) {
                tx = IMG_W - 1;
            }
            const u8 *px = buf.row + x * 4;
            u32 *a = buf.acc + ((size_t)ty * IMG_W + tx) * 5;
            u32 alpha = px[3];
            a[0] += px[0] * alpha;
            a[1] += px[1] * alpha;
            a[2] += px[2] * alpha;
            a[3] += alpha;
            a[4] += 1;
        }
    }

    const u8 bg_r = 0x23, bg_g = 0x23, bg_b = 0x29;
    for (int i = 0; i < IMG_W * IMG_H; i++) {
        const u32 *a = buf.acc + (size_t)i * 5;
        u8 *out = buf.rgba + (size_t)i * 4;
        if (a[4] == 0 || a[3] == 0) {
            out[0] = bg_r;
            out[1] = bg_g;
            out[2] = bg_b;
        } else {
            u32 r = a[0] / a[3];
            u32 g = a[1] / a[3];
            u32 b = a[2] / a[3];
            u32 alpha = a[3] / a[4]; // average alpha, 0..255
            out[0] = (u8)((r * alpha + bg_r * (255 - alpha)) / 255);
            out[1] = (u8)((g * alpha + bg_g * (255 - alpha)) / 255);
            out[2] = (u8)((b * alpha + bg_b * (255 - alpha)) / 255);
        }
        out[3] = 0xFF;
    }

    pack_tiled(buf.rgba, tiled);
    return true;
}

bool decode_png(const u8 *data, size_t size, std::vector<u8> &tiled) {
    if (!data || size < 8 ||
        png_sig_cmp(const_cast<png_bytep>(data), 0, 8) != 0) {
        return false;
    }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                             png_quiet_error,
                                             png_quiet_warning);
    if (!png) {
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return false;
    }

    MemReader reader{data, size, 0};
    DecodeBuffers buffers;
    bool ok = false;
    if (setjmp(png_jmpbuf(png)) == 0) {
        png_set_read_fn(png, &reader, png_mem_read);
        ok = decode_body(png, info, buffers, tiled);
    }
    free(buffers.row);
    free(buffers.acc);
    free(buffers.rgba);
    png_destroy_read_struct(&png, &info, nullptr);
    if (!ok) {
        tiled.clear();
    }
    return ok;
}

// ---- GPU side -------------------------------------------------------------

bool texture_upload(Texture &t, const std::vector<u8> &tiled) {
    if (tiled.size() != TILED_BYTES) {
        return false;
    }
    texture_free(t);
    if (!C3D_TexInit(&t.tex, TEX_W, TEX_H, GPU_RGB565)) {
        return false;
    }
    memcpy(t.tex.data, tiled.data(), TILED_BYTES);
    C3D_TexFlush(&t.tex);
    C3D_TexSetFilter(&t.tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&t.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    t.sub.width = IMG_W;
    t.sub.height = IMG_H;
    // Unrotated sub-texture (top > bottom; citro3d treats top < bottom as a
    // quarter-turn rotation, which is what produced sideways art). The
    // orientation itself is handled in pack_tiled().
    t.sub.left = 0.0f;
    t.sub.top = 1.0f;
    t.sub.right = (float)IMG_W / (float)TEX_W;
    t.sub.bottom = 1.0f - (float)IMG_H / (float)TEX_H;
    t.valid = true;
    return true;
}

void texture_free(Texture &t) {
    if (t.valid) {
        C3D_TexDelete(&t.tex);
        t.valid = false;
    }
}

void draw(const Texture &t, float x, float y, float scale) {
    if (!t.valid) {
        return;
    }
    C2D_Image img;
    img.tex = const_cast<C3D_Tex *>(&t.tex);
    img.subtex = &t.sub;
    C2D_DrawImageAt(img, x, y, 0.0f, nullptr, scale, scale);
}

} // namespace boxart
