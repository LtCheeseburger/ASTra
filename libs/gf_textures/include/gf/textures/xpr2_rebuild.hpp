#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gf::textures {

// Rebuild a standard DDS blob ("DDS "+header+data) from an Xbox 360 XPR2 container.
//
// Notes:
// - XPR2 commonly stores block-compressed data (DXT1/DXT3/DXT5/ATI2) in Xbox 360 tiled layout.
// - This function extracts the *first* texture entry in the container.
// - If outName is provided, it will be filled with the texture name when present.
std::optional<std::vector<std::uint8_t>> rebuild_xpr2_dds_first(std::span<const std::uint8_t> payload,
                                                               std::string* outName = nullptr);

}
