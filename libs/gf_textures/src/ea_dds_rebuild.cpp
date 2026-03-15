#include "gf/textures/ea_dds_rebuild.hpp"
#include "gf/textures/dds_validate.hpp"
#include <cstring>

namespace gf::textures {

static uint32_t rd_u32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

EaDxtFormat map_ast_flags_to_format(uint32_t flags) {
  // Known EA / NCAA14 mappings (validated vs EASE)
  // 0x01 -> DXT1
  // 0x11 -> DXT5
  // 0x21 -> BC4 / ATI1
  // 0x31 -> BC5 / ATI2
  switch (flags & 0x3F) {
    case 0x01: return EaDxtFormat::DXT1;
    case 0x11: return EaDxtFormat::DXT5;
    case 0x21: return EaDxtFormat::BC4;
    case 0x31: return EaDxtFormat::BC5;
    default:   return EaDxtFormat::UNKNOWN;
  }
}

std::optional<std::vector<uint8_t>>
rebuild_ea_dds(std::span<const uint8_t> payload, EaDdsInfo* outInfo) {
  if (payload.size() < 0x50) return std::nullopt;

  // EA DDS header layout (observed):
  // 0x00 magic 'DDS '
  // 0x0C height
  // 0x10 width
  // 0x1C mip count
  const uint8_t* dds = payload.data();

  // Some EA textures use a P3R signature instead of "DDS " (common in Madden/NCAA ASTs).
// In that case the blob is otherwise a normal DDS; we just need to swap the magic.
bool isP3R = false;
if (payload.size() >= 4 &&
    (std::memcmp(dds, "p3R", 3) == 0 || std::memcmp(dds, "P3R", 3) == 0) &&
    (dds[3] == 0x02 || dds[3] == 0x00)) {
  isP3R = true;
}

if (!isP3R && std::memcmp(dds, "DDS ", 4) != 0) {
  // Some EA entries prepend junk; scan for DDS magic
  bool found = false;
  for (size_t i = 0; i + 4 < payload.size(); ++i) {
    if (std::memcmp(payload.data() + i, "DDS ", 4) == 0) {
      dds = payload.data() + i;
      found = true;
      break;
    }
  }
  if (!found) return std::nullopt;
}

  uint32_t height = rd_u32(dds + 12);
  uint32_t width  = rd_u32(dds + 16);
  uint32_t mips   = rd_u32(dds + 28);
  uint32_t fourcc = rd_u32(dds + 84);

  EaDxtFormat fmt = EaDxtFormat::UNKNOWN;
  if (fourcc == 0x31545844) fmt = EaDxtFormat::DXT1; // DXT1
  else if (fourcc == 0x35545844) fmt = EaDxtFormat::DXT5; // DXT5
  else if (fourcc == 0x31495441) fmt = EaDxtFormat::BC4; // ATI1
  else if (fourcc == 0x32495441) fmt = EaDxtFormat::BC5; // ATI2

  if (outInfo) {
    outInfo->format = fmt;
    outInfo->width = width;
    outInfo->height = height;
    outInfo->mipCount = mips ? mips : 1;
  }

  // Return a clean DDS blob (header + data)
  size_t remaining = payload.size() - (dds - payload.data());
  std::vector<uint8_t> out(remaining);
  std::memcpy(out.data(), dds, remaining);
  if (isP3R && remaining >= 4) {
    out[0] = 'D'; out[1] = 'D'; out[2] = 'S'; out[3] = ' ';
  }

  const auto validation = inspect_dds(std::span<const uint8_t>(out.data(), out.size()));
  if (!validation.is_valid()) {
    return std::nullopt;
  }
  return out;
}

static void write_fourcc(std::vector<uint8_t>& dds, std::uint32_t fourcc) {
  // Standard DDS header: FourCC is at offset 0x54 from file start.
  if (dds.size() < 0x58) return;
  dds[0x54] = static_cast<uint8_t>(fourcc & 0xFFu);
  dds[0x55] = static_cast<uint8_t>((fourcc >> 8) & 0xFFu);
  dds[0x56] = static_cast<uint8_t>((fourcc >> 16) & 0xFFu);
  dds[0x57] = static_cast<uint8_t>((fourcc >> 24) & 0xFFu);
}

std::optional<std::vector<uint8_t>> rebuild_ea_dds(std::span<const uint8_t> payload, std::uint32_t astFlags, EaDdsInfo* outInfo) {
  EaDdsInfo tmp{};
  EaDdsInfo* infoPtr = outInfo ? outInfo : &tmp;

  auto ddsOpt = rebuild_ea_dds(payload, infoPtr);
  if (!ddsOpt.has_value()) return std::nullopt;

  // If the DDS header didn't confidently identify a usable format, use AST flags
  // as the fallback (EASE-style behavior).
  const EaDxtFormat expected = map_ast_flags_to_format(astFlags);
  if (infoPtr->format == EaDxtFormat::UNKNOWN && expected != EaDxtFormat::UNKNOWN) {
    std::vector<uint8_t>& dds = *ddsOpt;
    switch (expected) {
      case EaDxtFormat::DXT1: write_fourcc(dds, 0x31545844u /* 'DXT1' */); break;
      case EaDxtFormat::DXT5: write_fourcc(dds, 0x35545844u /* 'DXT5' */); break;
      case EaDxtFormat::BC4:  write_fourcc(dds, 0x31495441u /* 'ATI1' */); break;
      case EaDxtFormat::BC5:  write_fourcc(dds, 0x32495441u /* 'ATI2' */); break;
      default: break;
    }
    infoPtr->format = expected;
    const auto validation = inspect_dds(std::span<const uint8_t>(dds.data(), dds.size()));
    if (!validation.is_valid()) return std::nullopt;
  }

  return ddsOpt;
}

}
