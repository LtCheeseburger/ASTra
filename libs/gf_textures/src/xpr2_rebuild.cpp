// XPR2 (Xbox 360) -> DDS (mip0) rebuild helper
//
// Some EA XPR2 TX2D payloads are stored in a tiled/swizzled layout (needs untile),
// others are stored linearly (no untile), but both are 16-bit word swapped on disk.
//
// We generate two candidates:
//   A) word-swap only ("linear")
//   B) word-swap + untile_360_dxt ("untile")
// then decode both (BC1/BC2/BC3) and choose the one with the lower edge-discontinuity score.

#include <gf/textures/xpr2_rebuild.hpp>

#include <gf/textures/xbox360_xpr2_untile.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gf::textures {

namespace {

enum class Xpr2UntileMode {
  Auto = 0,
  Linear,
  Morton,
  XGAddress,
};

static Xpr2UntileMode xpr2_untile_mode_from_env() {
  // Debug override (no GUI wiring required yet).
  // Examples:
  //   ASTRA_XPR2_UNTILE_MODE=linear
  //   ASTRA_XPR2_UNTILE_MODE=morton
  //   ASTRA_XPR2_UNTILE_MODE=xgaddress
  const char* v = std::getenv("ASTRA_XPR2_UNTILE_MODE");
  if (!v || !*v) v = std::getenv("GF_XPR2_UNTILE_MODE");
  if (!v || !*v) return Xpr2UntileMode::Auto;

  std::string s(v);
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return char(std::tolower(c));
  });

  if (s == "linear") return Xpr2UntileMode::Linear;
  if (s == "morton" || s == "tile" || s == "untile") return Xpr2UntileMode::Morton;
  if (s == "xgaddress" || s == "xg") return Xpr2UntileMode::XGAddress;
  return Xpr2UntileMode::Auto;
}

static std::uint32_t be32(const std::uint8_t* p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) |
         std::uint32_t(p[3]);
}

static std::uint16_t be16(const std::uint8_t* p) {
  return (std::uint16_t(p[0]) << 8) | std::uint16_t(p[1]);
}

static std::uint16_t le16(const std::uint8_t* p) {
  return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
}

static std::uint32_t le32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
         (std::uint32_t(p[3]) << 24);
}

// XPR2 payloads are stored as 16-bit words with byte order swapped on disk.
// A simple byte-swap of every 16-bit word is required before any further processing.
static void swap16_inplace(std::vector<std::uint8_t>& v) {
  for (std::size_t i = 0; i + 1 < v.size(); i += 2) {
    std::swap(v[i], v[i + 1]);
  }
}


// Xbox 360 BC (DXT) payloads are stored in a GPU-friendly endianness.
// After swap16_inplace(), additional 32-bit word reordering is required
// for correct BC block decoding on little-endian hosts.
static void xbox360_fixup_dxt_endian_inplace(std::vector<std::uint8_t>& v, std::uint32_t bytes_per_block) {
  if (v.empty()) return;
  if (bytes_per_block != 8u && bytes_per_block != 16u) return;

  const std::size_t bpb = bytes_per_block;
  const std::size_t nBlocks = v.size() / bpb;

  for (std::size_t bi = 0; bi < nBlocks; ++bi) {
    std::uint8_t* b = v.data() + bi * bpb;

    auto swap4 = [&](std::size_t a, std::size_t c) {
      std::swap(b[a + 0], b[c + 0]);
      std::swap(b[a + 1], b[c + 1]);
      std::swap(b[a + 2], b[c + 2]);
      std::swap(b[a + 3], b[c + 3]);
    };

    if (bpb == 8) {
      // BC1/DXT1: swap the two dwords.
      swap4(0, 4);
    } else {
      // BC2/BC3 (DXT3/DXT5): swap dwords within each 8-byte half.
      swap4(0, 4);
      swap4(8, 12);
    }
  }
}

// Endian fixup is provided by xbox360_xpr2_untile.

static std::vector<std::uint8_t> build_dds_header(std::uint32_t w, std::uint32_t h,
                                                  const char fourcc[4]) {
  // Minimal DDS header (128 bytes)
  std::vector<std::uint8_t> dds(128, 0);
  auto w32 = [&](std::size_t off, std::uint32_t v) {
    dds[off + 0] = std::uint8_t(v & 0xFF);
    dds[off + 1] = std::uint8_t((v >> 8) & 0xFF);
    dds[off + 2] = std::uint8_t((v >> 16) & 0xFF);
    dds[off + 3] = std::uint8_t((v >> 24) & 0xFF);
  };

  dds[0] = 'D';
  dds[1] = 'D';
  dds[2] = 'S';
  dds[3] = ' ';
  w32(4, 124);
  w32(8, 0x00001007); // CAPS|HEIGHT|WIDTH|PIXELFORMAT
  w32(12, h);
  w32(16, w);

  // Pixel format
  w32(76, 32);
  w32(80, 0x00000004); // FOURCC
  dds[84] = std::uint8_t(fourcc[0]);
  dds[85] = std::uint8_t(fourcc[1]);
  dds[86] = std::uint8_t(fourcc[2]);
  dds[87] = std::uint8_t(fourcc[3]);

  // Caps
  w32(108, 0x00001000); // DDSCAPS_TEXTURE
  return dds;
}

struct FmtInfo {
  const char* fourcc;
  std::uint32_t bytes_per_block;
};

static std::optional<FmtInfo> fmt_from_xpr2(std::uint8_t fmt) {
  // Matches the Noesis plugin used by the user:
  //   0x52 -> DXT1, 0x53 -> DXT3, 0x54 -> DXT5
  switch (fmt) {
    case 0x52: return FmtInfo{"DXT1", 8};
    case 0x53: return FmtInfo{"DXT3", 16};
    case 0x54: return FmtInfo{"DXT5", 16};
    default: return std::nullopt;
  }
}

// Xbox 360 DXT textures are stored in a tiled layout.
// We untile in units of 4x4 blocks: 32x32 texel tiles == 8x8 blocks.
static constexpr std::uint32_t morton2_3bit(std::uint32_t x, std::uint32_t y) {
  x &= 7u;
  y &= 7u;
  std::uint32_t z = 0;
  z |= (x & 1u) << 0;
  z |= (y & 1u) << 1;
  z |= (x & 2u) << 1;
  z |= (y & 2u) << 2;
  z |= (x & 4u) << 2;
  z |= (y & 4u) << 3;
  return z;
}

static std::vector<std::uint8_t> untile_360_dxt_padded(const std::vector<std::uint8_t>& tiled,
                                                       std::uint32_t width_blocks,
                                                       std::uint32_t height_blocks,
                                                       std::uint32_t src_pitch_blocks,
                                                       std::uint32_t src_height_blocks,
                                                       std::uint32_t bytes_per_block) {
  // Xbox 360 2D tiled surfaces are laid out in 4KB macro-tiles.
  // For BC formats, tiles are 32x32 texels == 8x8 blocks.
  // Real surfaces often have padding in pitch/height; the source buffer must
  // be interpreted using the padded dimensions, while the destination is clipped
  // to the logical width/height.
  std::vector<std::uint8_t> linear(
      std::size_t(width_blocks) * std::size_t(height_blocks) * std::size_t(bytes_per_block), 0);

  const std::uint32_t tile_w = 8; // blocks
  const std::uint32_t tile_h = 8; // blocks
  const std::uint32_t tiles_x = (src_pitch_blocks + tile_w - 1) / tile_w;
  const std::uint32_t tiles_y = (src_height_blocks + tile_h - 1) / tile_h;

  const std::size_t blocks_per_tile = std::size_t(tile_w) * std::size_t(tile_h); // 64
  const std::size_t tile_bytes = blocks_per_tile * std::size_t(bytes_per_block);

  for (std::uint32_t ty = 0; ty < tiles_y; ++ty) {
    for (std::uint32_t tx = 0; tx < tiles_x; ++tx) {
      const std::size_t tile_index = std::size_t(ty) * std::size_t(tiles_x) + std::size_t(tx);
      const std::size_t tile_base = tile_index * tile_bytes;
      if (tile_base >= tiled.size()) continue;

      for (std::uint32_t by = 0; by < tile_h; ++by) {
        for (std::uint32_t bx = 0; bx < tile_w; ++bx) {
          const std::uint32_t dst_x = tx * tile_w + bx;
          const std::uint32_t dst_y = ty * tile_h + by;
          if (dst_x >= width_blocks || dst_y >= height_blocks) continue;

          const std::size_t mort = std::size_t(morton2_3bit(bx, by));
          const std::size_t src = tile_base + mort * std::size_t(bytes_per_block);
          const std::size_t dst =
              (std::size_t(dst_y) * std::size_t(width_blocks) + std::size_t(dst_x)) *
              std::size_t(bytes_per_block);

          if (src + bytes_per_block <= tiled.size() && dst + bytes_per_block <= linear.size()) {
            std::memcpy(linear.data() + dst, tiled.data() + src, bytes_per_block);
          }
        }
      }
    }
  }

  return linear;
}

static inline void rgb565_to_rgb888(std::uint16_t c, std::uint8_t& r, std::uint8_t& g,
                                    std::uint8_t& b) {
  const std::uint8_t r5 = (c >> 11) & 0x1F;
  const std::uint8_t g6 = (c >> 5) & 0x3F;
  const std::uint8_t b5 = (c >> 0) & 0x1F;
  r = (r5 << 3) | (r5 >> 2);
  g = (g6 << 2) | (g6 >> 4);
  b = (b5 << 3) | (b5 >> 2);
}

static void decode_bc1_block(const std::uint8_t* block, std::uint8_t rgba[16][4]) {
  const std::uint16_t c0 = le16(block + 0);
  const std::uint16_t c1 = le16(block + 2);

  std::uint8_t r0, g0, b0, r1, g1, b1;
  rgb565_to_rgb888(c0, r0, g0, b0);
  rgb565_to_rgb888(c1, r1, g1, b1);

  std::array<std::array<std::uint8_t, 4>, 4> pal{};
  pal[0] = {r0, g0, b0, 255};
  pal[1] = {r1, g1, b1, 255};

  if (c0 > c1) {
    pal[2] = {std::uint8_t((2 * r0 + r1) / 3), std::uint8_t((2 * g0 + g1) / 3),
              std::uint8_t((2 * b0 + b1) / 3), 255};
    pal[3] = {std::uint8_t((r0 + 2 * r1) / 3), std::uint8_t((g0 + 2 * g1) / 3),
              std::uint8_t((b0 + 2 * b1) / 3), 255};
  } else {
    pal[2] = {std::uint8_t((r0 + r1) / 2), std::uint8_t((g0 + g1) / 2),
              std::uint8_t((b0 + b1) / 2), 255};
    pal[3] = {0, 0, 0, 0};
  }

  const std::uint32_t idx = le32(block + 4);
  for (int i = 0; i < 16; ++i) {
    const std::uint32_t sel = (idx >> (2 * i)) & 0x3;
    rgba[i][0] = pal[sel][0];
    rgba[i][1] = pal[sel][1];
    rgba[i][2] = pal[sel][2];
    rgba[i][3] = pal[sel][3];
  }
}

static void decode_bc2_block(const std::uint8_t* block, std::uint8_t rgba[16][4]) {
  // BC2 (DXT3): explicit 4-bit alpha
  for (int i = 0; i < 16; ++i) {
    const std::uint8_t a4 = (i & 1) ? (block[i / 2] >> 4) : (block[i / 2] & 0x0F);
    rgba[i][3] = std::uint8_t(a4 * 17);
  }
  std::uint8_t tmp[16][4]{};
  decode_bc1_block(block + 8, tmp);
  for (int i = 0; i < 16; ++i) {
    rgba[i][0] = tmp[i][0];
    rgba[i][1] = tmp[i][1];
    rgba[i][2] = tmp[i][2];
  }
}

static void decode_bc3_block(const std::uint8_t* block, std::uint8_t rgba[16][4]) {
  const std::uint8_t a0 = block[0];
  const std::uint8_t a1 = block[1];

  std::array<std::uint8_t, 8> ap{};
  ap[0] = a0;
  ap[1] = a1;
  if (a0 > a1) {
    ap[2] = std::uint8_t((6 * a0 + 1 * a1) / 7);
    ap[3] = std::uint8_t((5 * a0 + 2 * a1) / 7);
    ap[4] = std::uint8_t((4 * a0 + 3 * a1) / 7);
    ap[5] = std::uint8_t((3 * a0 + 4 * a1) / 7);
    ap[6] = std::uint8_t((2 * a0 + 5 * a1) / 7);
    ap[7] = std::uint8_t((1 * a0 + 6 * a1) / 7);
  } else {
    ap[2] = std::uint8_t((4 * a0 + 1 * a1) / 5);
    ap[3] = std::uint8_t((3 * a0 + 2 * a1) / 5);
    ap[4] = std::uint8_t((2 * a0 + 3 * a1) / 5);
    ap[5] = std::uint8_t((1 * a0 + 4 * a1) / 5);
    ap[6] = 0;
    ap[7] = 255;
  }

  std::uint64_t abits = 0;
  for (int i = 0; i < 6; ++i) abits |= (std::uint64_t(block[2 + i]) << (8 * i));
  for (int i = 0; i < 16; ++i) {
    const std::uint8_t sel = std::uint8_t((abits >> (3 * i)) & 0x7);
    rgba[i][3] = ap[sel];
  }

  std::uint8_t tmp[16][4]{};
  decode_bc1_block(block + 8, tmp);
  for (int i = 0; i < 16; ++i) {
    rgba[i][0] = tmp[i][0];
    rgba[i][1] = tmp[i][1];
    rgba[i][2] = tmp[i][2];
  }
}

static std::optional<std::vector<std::uint8_t>> decode_dds_mip0_rgba(
    std::span<const std::uint8_t> dds, int& outW, int& outH) {
  if (dds.size() < 128) return std::nullopt;
  if (std::memcmp(dds.data(), "DDS ", 4) != 0) return std::nullopt;

  const std::uint32_t height = le32(dds.data() + 12);
  const std::uint32_t width = le32(dds.data() + 16);
  const std::uint32_t fourcc = le32(dds.data() + 84);

  outW = int(width);
  outH = int(height);
  if (outW <= 0 || outH <= 0) return std::nullopt;

  const bool isDXT1 = (fourcc == 0x31545844u);
  const bool isDXT3 = (fourcc == 0x33545844u);
  const bool isDXT5 = (fourcc == 0x35545844u);
  if (!isDXT1 && !isDXT3 && !isDXT5) return std::nullopt;

  const int bw = (outW + 3) / 4;
  const int bh = (outH + 3) / 4;
  const int bpp = isDXT1 ? 8 : 16;

  const std::size_t need = std::size_t(bw) * std::size_t(bh) * std::size_t(bpp);
  if (dds.size() < 128 + need) return std::nullopt;

  const std::uint8_t* src = dds.data() + 128;
  std::vector<std::uint8_t> rgba(std::size_t(outW) * std::size_t(outH) * 4, 0);

  std::uint8_t px[16][4]{};
  for (int by = 0; by < bh; ++by) {
    for (int bx = 0; bx < bw; ++bx) {
      const std::size_t off = (std::size_t(by) * std::size_t(bw) + std::size_t(bx)) *
                              std::size_t(bpp);
      const std::uint8_t* blk = src + off;

      if (isDXT1) decode_bc1_block(blk, px);
      else if (isDXT3) decode_bc2_block(blk, px);
      else decode_bc3_block(blk, px);

      for (int py = 0; py < 4; ++py) {
        for (int pxI = 0; pxI < 4; ++pxI) {
          const int x = bx * 4 + pxI;
          const int y = by * 4 + py;
          if (x >= outW || y >= outH) continue;
          const int i = py * 4 + pxI;
          const std::size_t di = (std::size_t(y) * std::size_t(outW) + std::size_t(x)) * 4;
          rgba[di + 0] = px[i][0];
          rgba[di + 1] = px[i][1];
          rgba[di + 2] = px[i][2];
          rgba[di + 3] = px[i][3];
        }
      }
    }
  }

  return rgba;
}

static double edge_score_rgb(const std::vector<std::uint8_t>& rgba, int w, int h) {
  if (w <= 1 || h <= 1) return 0.0;
  auto at = [&](int x, int y, int c) -> int {
    return int(rgba[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4 + std::size_t(c)]);
  };

  double score = 0.0;
  // Sample grid to keep this fast on big textures.
  const int stepX = std::max(1, w / 256);
  const int stepY = std::max(1, h / 256);

  for (int y = 0; y < h; y += stepY) {
    for (int x = 0; x < w; x += stepX) {
      if (x + 1 < w) {
        score += double(std::abs(at(x, y, 0) - at(x + 1, y, 0)) +
                        std::abs(at(x, y, 1) - at(x + 1, y, 1)) +
                        std::abs(at(x, y, 2) - at(x + 1, y, 2)));
      }
      if (y + 1 < h) {
        score += double(std::abs(at(x, y, 0) - at(x, y + 1, 0)) +
                        std::abs(at(x, y, 1) - at(x, y + 1, 1)) +
                        std::abs(at(x, y, 2) - at(x, y + 1, 2)));
      }
    }
  }

  return score;
}

struct Quality {
  double black_ratio = 1.0;
  double variance = 0.0;
  double edge = 0.0;
};

static Quality quality_metrics(const std::vector<std::uint8_t>& rgba, int w, int h) {
  Quality q;
  if (w <= 0 || h <= 0) return q;

  const int stepX = std::max(1, w / 256);
  const int stepY = std::max(1, h / 256);

  std::size_t samples = 0;
  std::size_t black = 0;
  double mean = 0.0;
  double m2 = 0.0;

  auto at = [&](int x, int y, int c) -> int {
    return int(rgba[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4 + std::size_t(c)]);
  };

  for (int y = 0; y < h; y += stepY) {
    for (int x = 0; x < w; x += stepX) {
      const int r = at(x, y, 0);
      const int g = at(x, y, 1);
      const int b = at(x, y, 2);
      const int l = (r * 299 + g * 587 + b * 114) / 1000;

      // Welford variance.
      ++samples;
      const double dl = double(l);
      const double delta = dl - mean;
      mean += delta / double(samples);
      m2 += delta * (dl - mean);

      if (r < 8 && g < 8 && b < 8) ++black;
    }
  }

  q.black_ratio = samples ? double(black) / double(samples) : 1.0;
  q.variance = samples > 1 ? (m2 / double(samples - 1)) : 0.0;
  q.edge = edge_score_rgb(rgba, w, h);
  return q;
}

} // namespace

std::optional<std::vector<std::uint8_t>> rebuild_xpr2_dds_first(std::span<const std::uint8_t> xpr2,
                                                                std::string* outName) {
  auto fail = [&](const std::string& msg) -> std::optional<std::vector<std::uint8_t>> {
    if (outName) *outName = msg;
    return std::nullopt;
  };

  if (xpr2.size() < 0x20) return fail("XPR2 too small");
  if (std::memcmp(xpr2.data(), "XPR2", 4) != 0) return fail("Not an XPR2 file");

  const std::uint32_t header1 = be32(xpr2.data() + 4); // base
  const std::uint32_t header2 = be32(xpr2.data() + 8); // total data bytes (aligned)
  const std::uint32_t count = be32(xpr2.data() + 12);
  if (count == 0) return fail("XPR2 has no entries");

  const std::size_t tableOff = 16;
  if (tableOff + std::size_t(count) * 16 > xpr2.size()) return fail("XPR2 table truncated");

  const std::uint32_t dataBase = header1 + 12; // Noesis plugin
  if (dataBase >= xpr2.size()) return fail("XPR2 data base out of range");

  // Find first TX2D entry.
  std::uint32_t tx2d_off_struct = 0;
  std::uint32_t tx2d_off_name = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint8_t* e = xpr2.data() + tableOff + std::size_t(i) * 16;
    const std::uint32_t type = be32(e + 0);
    if (type == 0x54583244u /* 'TX2D' */) {
      tx2d_off_struct = be32(e + 4);
      tx2d_off_name = be32(e + 12);
      break;
    }
  }
  if (!tx2d_off_struct) return fail("No TX2D entries found");

  // Extract name (best-effort) like Noesis: seek(nameOff + 12), read C string.
  if (outName) {
    outName->clear();
    if (tx2d_off_name) {
      const std::size_t nameOff = std::size_t(tx2d_off_name) + 12;
      if (nameOff < xpr2.size()) {
        std::size_t end = nameOff;
        while (end < xpr2.size() && xpr2[end] != 0) ++end;
        *outName = std::string(reinterpret_cast<const char*>(xpr2.data() + nameOff), end - nameOff);
      }
    }
  }

  // TX2D metadata (Noesis): seek(tx2d + 12), skip 33, then read >H B H H ...
  const std::size_t metaBase = std::size_t(tx2d_off_struct) + 12;
  const std::size_t meta = metaBase + 33;
  if (meta + 7 > xpr2.size()) return fail("TX2D metadata out of range");

  const std::uint16_t dataOffBlocks = be16(xpr2.data() + meta + 0);
  const std::uint8_t fmt = xpr2[meta + 2];
  const std::uint16_t hBlocks = be16(xpr2.data() + meta + 3);
  const std::uint16_t wPacked = be16(xpr2.data() + meta + 5);

  const std::uint32_t height = (std::uint32_t(hBlocks) + 1u) * 8u;
  const std::uint32_t width = (std::uint32_t(wPacked) + 1u) & 0x1FFFu;
  if (!width || !height) return fail("Invalid TX2D dimensions");

  const auto finfo = fmt_from_xpr2(fmt);
  if (!finfo) return fail("Unsupported XPR2 format code: " + std::to_string(fmt));

  if (header2 < 0x100) return fail("XPR2 data length too small");
  const std::uint32_t totalBlocks = header2 / 0x100;
  if (dataOffBlocks >= totalBlocks) return fail("XPR2 data offset out of range");

  const std::size_t dataOff = std::size_t(dataBase) + std::size_t(dataOffBlocks) * 0x100;
  if (dataOff >= xpr2.size()) return fail("XPR2 data offset outside file");

  // Decode just the top mip for preview.
  const std::uint32_t widthBlocks = (width + 3u) / 4u;
  const std::uint32_t heightBlocks = (height + 3u) / 4u;
  const std::size_t mipBytes = std::size_t(widthBlocks) * std::size_t(heightBlocks) *
                               std::size_t(finfo->bytes_per_block);
  if (dataOff + mipBytes > xpr2.size()) return fail("XPR2 texture data truncated");

  std::vector<std::uint8_t> raw(xpr2.begin() + std::ptrdiff_t(dataOff),
                                xpr2.begin() + std::ptrdiff_t(dataOff + mipBytes));

  // Many X360 tiled surfaces include padding in pitch/height (4KB macro-tiles).
  // When untile logic reads into padding, using only logical mip bytes can produce
  // structured corruption or large black regions.
  auto align_up_u32 = [](std::uint32_t v, std::uint32_t a) {
    return (v + (a - 1u)) & ~(a - 1u);
  };
  const std::uint32_t pitchBlocks = align_up_u32(widthBlocks, 32u);
  const std::uint32_t tileHBlocks = (finfo->bytes_per_block == 8u) ? 16u : 8u;
  const std::uint32_t paddedHeightBlocks = align_up_u32(heightBlocks, tileHBlocks);
  const std::size_t surfBytes =
      std::size_t(pitchBlocks) * std::size_t(paddedHeightBlocks) * std::size_t(finfo->bytes_per_block);

  std::vector<std::uint8_t> rawSurf;
  if (dataOff + surfBytes <= xpr2.size()) {
    rawSurf.assign(xpr2.begin() + std::ptrdiff_t(dataOff),
                   xpr2.begin() + std::ptrdiff_t(dataOff + surfBytes));
  }

  // Candidate A: endian-fix only (linear on disk)
  std::vector<std::uint8_t> candLinear = raw;
  // EA XPR2 BC payloads on Xbox 360 are commonly stored as 16-bit word swapped.
  // A simple swap16 across the surface matches Noesis' behavior for these titles.
  swap16_inplace(candLinear);
  xbox360_fixup_dxt_endian_inplace(candLinear, finfo->bytes_per_block);

  // Candidate B: endian-fix + morton untile.
  std::vector<std::uint8_t> candUntile = rawSurf.empty() ? raw : rawSurf;
  swap16_inplace(candUntile);
  xbox360_fixup_dxt_endian_inplace(candUntile, finfo->bytes_per_block);
  candUntile = untile_360_dxt_padded(
      candUntile,
      widthBlocks,
      heightBlocks,
      rawSurf.empty() ? widthBlocks : pitchBlocks,
      rawSurf.empty() ? heightBlocks : paddedHeightBlocks,
      finfo->bytes_per_block);

#ifdef USE_XGADDRESS_UNTILE
  // Candidate C: endian-fix + XGAddress/Xenia2D untile (expects padded surface bytes).
  std::vector<std::uint8_t> candXg = rawSurf.empty() ? raw : rawSurf;
  swap16_inplace(candXg);
  xbox360_fixup_dxt_endian_inplace(candXg, finfo->bytes_per_block);
  candXg = xbox360_xgaddress_untile_dxt(candXg, widthBlocks, heightBlocks, finfo->bytes_per_block);
#endif

  std::vector<std::uint8_t> ddsLinear = build_dds_header(width, height, finfo->fourcc);
  ddsLinear.insert(ddsLinear.end(), candLinear.begin(), candLinear.end());

  std::vector<std::uint8_t> ddsUntile = build_dds_header(width, height, finfo->fourcc);
  ddsUntile.insert(ddsUntile.end(), candUntile.begin(), candUntile.end());

#ifdef USE_XGADDRESS_UNTILE
  std::vector<std::uint8_t> ddsXg = build_dds_header(width, height, finfo->fourcc);
  ddsXg.insert(ddsXg.end(), candXg.begin(), candXg.end());
#endif

  const Xpr2UntileMode forced = xpr2_untile_mode_from_env();
  if (forced != Xpr2UntileMode::Auto) {
    if (outName) {
      *outName += "\n[xpr2] forced untile mode via ASTRA_XPR2_UNTILE_MODE";
    }

    switch (forced) {
      case Xpr2UntileMode::Linear: return ddsLinear;
      case Xpr2UntileMode::Morton: return ddsUntile;
      case Xpr2UntileMode::XGAddress:
#ifdef USE_XGADDRESS_UNTILE
        return ddsXg;
#else
        if (outName) {
          *outName += " (xgaddress requested but USE_XGADDRESS_UNTILE is OFF; using morton)";
        }
        return ddsUntile;
#endif
      default: break;
    }
  }

  int lw = 0, lh = 0, uw = 0, uh = 0;
  const auto imgL = decode_dds_mip0_rgba(std::span<const std::uint8_t>(ddsLinear), lw, lh);
  const auto imgU = decode_dds_mip0_rgba(std::span<const std::uint8_t>(ddsUntile), uw, uh);

#ifdef USE_XGADDRESS_UNTILE
  int xw = 0, xh = 0;
  const auto imgX = decode_dds_mip0_rgba(std::span<const std::uint8_t>(ddsXg), xw, xh);
#endif

  if (!imgL || !imgU || lw != uw || lh != uh) {
    if (outName) {
      *outName += "\n[xpr2] autopick: decode failed, default=untile";
    }
    return ddsUntile;
  }

  const Quality qL = quality_metrics(*imgL, lw, lh);
  const Quality qU = quality_metrics(*imgU, uw, uh);

  auto better = [](const Quality& a, const Quality& b) {
    // 1) Prefer less "mostly black" coverage.
    if (std::abs(a.black_ratio - b.black_ratio) > 0.01) return a.black_ratio < b.black_ratio;
    // 2) Prefer higher variance (more real signal).
    if (std::abs(a.variance - b.variance) > 1.0) return a.variance > b.variance;
    // 3) Prefer lower edge discontinuity.
    return a.edge < b.edge;
  };

#ifdef USE_XGADDRESS_UNTILE
  if (!imgX || xw != lw || xh != lh) {
    if (outName) {
      *outName += "\n[xpr2] autopick: xg decode failed, using best of linear/morton";
    }
    const bool pickLinear = better(qL, qU);
    return pickLinear ? ddsLinear : ddsUntile;
  }

  const Quality qX = quality_metrics(*imgX, xw, xh);

  enum class Pick { Linear, Morton, XG };
  Pick pick = Pick::XG;
  Quality best = qX;
  if (better(qL, best)) { best = qL; pick = Pick::Linear; }
  if (better(qU, best)) { best = qU; pick = Pick::Morton; }

  if (outName) {
    *outName += "\n[xpr2] autopick: "
                "linear(bk=" + std::to_string(qL.black_ratio) + ",var=" + std::to_string(qL.variance) + ",edge=" + std::to_string(qL.edge) + ") "
                "morton(bk=" + std::to_string(qU.black_ratio) + ",var=" + std::to_string(qU.variance) + ",edge=" + std::to_string(qU.edge) + ") "
                "xg(bk=" + std::to_string(qX.black_ratio) + ",var=" + std::to_string(qX.variance) + ",edge=" + std::to_string(qX.edge) + ") "
                "pick=" + std::string(pick == Pick::Linear ? "linear" : (pick == Pick::Morton ? "morton" : "xgaddress"));
  }

  switch (pick) {
    case Pick::Linear: return ddsLinear;
    case Pick::Morton: return ddsUntile;
    default: return ddsXg;
  }
#else
  const bool pickLinear = better(qL, qU);
  if (outName) {
    *outName += "\n[xpr2] autopick: "
                "linear(bk=" + std::to_string(qL.black_ratio) + ",var=" + std::to_string(qL.variance) + ",edge=" + std::to_string(qL.edge) + ") "
                "morton(bk=" + std::to_string(qU.black_ratio) + ",var=" + std::to_string(qU.variance) + ",edge=" + std::to_string(qU.edge) + ") "
                "pick=" + std::string(pickLinear ? "linear" : "morton");
  }
  return pickLinear ? ddsLinear : ddsUntile;
#endif
}

} // namespace gf::textures