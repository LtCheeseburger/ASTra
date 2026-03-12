#pragma once

#include <cstdint>
#include <vector>

namespace gf::textures {

// Untile an Xbox 360 XPR2 TX2D surface using an XGAddress-style 2D tiled layout.
//
// Notes:
// - For block-compressed textures, treat each 4x4 block as a "texel".
// - width_blocks/height_blocks are the dimensions in blocks.
// - bytes_per_block is typically 8 (DXT1) or 16 (DXT3/DXT5).
//
// Input and output buffers are block-linear (no headers), just the compressed payload.
std::vector<std::uint8_t> xbox360_xgaddress_untile_dxt(
    const std::vector<std::uint8_t>& tiled,
    std::uint32_t width_blocks,
    std::uint32_t height_blocks,
    std::uint32_t bytes_per_block);

}  // namespace gf::textures
