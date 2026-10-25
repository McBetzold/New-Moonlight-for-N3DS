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

// Box art handling: the host's PNG is decoded with libpng, box-filtered down
// to 96x128, converted to the GPU's tiled RGB565 layout and cached on the SD
// card so later visits only need a 32 KB read. Decoding runs on the worker
// thread; only texture_upload/texture_free/draw touch the GPU.
#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <string>
#include <vector>

namespace boxart {

constexpr int TEX_W = 128; // GPU texture dimensions (power of two)
constexpr int TEX_H = 128;
constexpr int IMG_W = 96; // visible box art region (3:4)
constexpr int IMG_H = 128;
constexpr size_t TILED_BYTES = (size_t)TEX_W * TEX_H * 2; // RGB565

void ensure_cache_dir(const std::string &host_key);
std::string cache_path(const std::string &host_key, int app_id);

// Worker-thread helpers
bool load_cached(const std::string &path, std::vector<u8> &tiled);
bool decode_png(const u8 *data, size_t size, std::vector<u8> &tiled);
void save_cache(const std::string &path, const std::vector<u8> &tiled);

struct Texture {
    C3D_Tex tex{};
    Tex3DS_SubTexture sub{};
    bool valid = false;
};

// UI-thread helpers
bool texture_upload(Texture &t, const std::vector<u8> &tiled);
void texture_free(Texture &t);
// Draws the art with its top-left corner at (x, y); scale 1.0 = 96x128.
void draw(const Texture &t, float x, float y, float scale);

} // namespace boxart
