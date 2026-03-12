#include <gf/textures/xbox360_xpr2_untile.hpp>

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

namespace gf::textures {

namespace {

static inline std::uint32_t align_up(std::uint32_t v, std::uint32_t a) {
  return (v + (a - 1u)) & ~(a - 1u);
}

}  // namespace

// Untile an Xbox 360 XPR2 TX2D surface using an XGAddress-style *macrotile* layout.
//
// For block-compressed textures, treat each 4x4 block as a "texel" (unit).
//
// This implementation is intentionally simple and deterministic:
// - Pitch is aligned to 32 blocks (common for 360 tiled 2D textures).
// - Macro tiles are treated as 32x16 blocks for DXT (the key difference vs simple Morton/32x32 layouts).
//
// Input and output are just the compressed payload (no DDS headers).
std::vector<std::uint8_t> xbox360_xgaddress_untile_dxt(
    const std::vector<std::uint8_t>& tiled,
    std::uint32_t width_blocks,
    std::uint32_t height_blocks,
    std::uint32_t bytes_per_block) {
  // Xbox 360 uses the XGAddress 2D tiled layout.
  // This implementation matches the canonical XGAddress2DTiledOffset math
  // (see BlueRaja/UModel GetTiledOffset), operating in *block* units.
  if (width_blocks == 0 || height_blocks == 0 || bytes_per_block == 0) {
    return {};
  }

  // bytes_per_block is a power-of-two for BC formats we support (8 or 16).
  std::uint32_t log_bpb = 0;
  {
    std::uint32_t v = bytes_per_block;
    while (v > 1u) {
      v >>= 1u;
      ++log_bpb;
    }
  }

  const std::uint32_t aligned_width_blocks = (width_blocks + 31u) & ~31u;

  auto get_tiled_offset_blocks = [&](std::uint32_t x, std::uint32_t y) -> std::uint32_t {
    const std::uint32_t alignedWidth = aligned_width_blocks;

    const std::uint32_t macro =
        ((x >> 5u) + (y >> 5u) * (alignedWidth >> 5u)) << (log_bpb + 7u);

    const std::uint32_t micro =
        ((x & 7u) + ((y & 0xEu) << 2u)) << log_bpb;

    const std::uint32_t offset =
        macro + ((micro & ~0xFu) << 1u) + (micro & 0xFu) + ((y & 1u) << 4u);

    const std::uint32_t address =
        (((offset & ~0x1FFu) << 3u) +
         ((y & 16u) << 7u) +
         ((offset & 0x1C0u) << 2u) +
         (((((y & 8u) >> 2u) + (x >> 3u)) & 3u) << 6u) +
         (offset & 0x3Fu));

    return address >> log_bpb; // convert from bytes to blocks
  };

  std::vector<std::uint8_t> out(static_cast<std::size_t>(width_blocks) *
                                    static_cast<std::size_t>(height_blocks) *
                                    static_cast<std::size_t>(bytes_per_block),
                                0);

  for (std::uint32_t y = 0; y < height_blocks; ++y) {
    for (std::uint32_t x = 0; x < width_blocks; ++x) {
      const std::uint32_t src_block = get_tiled_offset_blocks(x, y);

      // Guard against containers that store only the minimal (unpadded) width.
      if (src_block >= aligned_width_blocks * height_blocks) {
        continue;
      }

      const std::size_t src_off = static_cast<std::size_t>(src_block) *
                                  static_cast<std::size_t>(bytes_per_block);

      const std::size_t dst_off = (static_cast<std::size_t>(y) * width_blocks +
                                   static_cast<std::size_t>(x)) *
                                  static_cast<std::size_t>(bytes_per_block);

      if (src_off + bytes_per_block <= tiled.size() &&
          dst_off + bytes_per_block <= out.size()) {
        std::memcpy(out.data() + dst_off, tiled.data() + src_off, bytes_per_block);
      }
    }
  }

  return out;
}

}  // namespace gf::textures
