#pragma once
#include <cstdint>
#include <vector>
#include <span>
#include <optional>

namespace gf::textures {

enum class EaDxtFormat {
  DXT1,
  DXT5,
  BC4, // ATI1
  BC5, // ATI2
  UNKNOWN
};

struct EaDdsInfo {
  EaDxtFormat format;
  uint32_t width;
  uint32_t height;
  uint32_t mipCount;
};

// Rebuilds a *standard* DDS blob from an EA-wrapped DDS payload
// `payload` must be the decompressed AST entry bytes
std::optional<std::vector<uint8_t>>
rebuild_ea_dds(std::span<const uint8_t> payload, EaDdsInfo* outInfo);

// Maps AST entry flags -> expected EA DXT format
EaDxtFormat map_ast_flags_to_format(uint32_t astFlags);

// Convenience overload used by the GUI preview path.
//
// EASE determines the intended DXT format primarily from the AST entry flags.
// For preview, we keep this logic here so callers have a single entry point:
//   AST payload -> rebuild DDS -> decode.
//
// Note: `rebuild_ea_dds(payload, outInfo)` will still validate/override fields
// based on the EA header when present; this overload just seeds the format.
std::optional<std::vector<uint8_t>>
rebuild_ea_dds(std::span<const uint8_t> payload, uint32_t astFlags, EaDdsInfo* outInfo);

}
