#include <catch2/catch_test_macros.hpp>

#include <gf/textures/dds_validate.hpp>
#include <gf/textures/ea_dds_rebuild.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

void wr32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t x) {
  v[off + 0] = static_cast<std::uint8_t>(x & 0xffu);
  v[off + 1] = static_cast<std::uint8_t>((x >> 8) & 0xffu);
  v[off + 2] = static_cast<std::uint8_t>((x >> 16) & 0xffu);
  v[off + 3] = static_cast<std::uint8_t>((x >> 24) & 0xffu);
}

std::vector<std::uint8_t> make_dxt1_dds(std::uint32_t width = 4,
                                        std::uint32_t height = 4,
                                        std::uint32_t mips = 1,
                                        std::size_t payloadBytes = 8) {
  std::vector<std::uint8_t> out(128 + payloadBytes, 0);
  out[0] = 'D'; out[1] = 'D'; out[2] = 'S'; out[3] = ' ';
  wr32(out, 4, 124);
  wr32(out, 8, 0x00021007u); // caps|height|width|pixelformat|linearsize|mipmapcount
  wr32(out, 12, height);
  wr32(out, 16, width);
  wr32(out, 20, static_cast<std::uint32_t>(payloadBytes));
  wr32(out, 24, 0);
  wr32(out, 28, mips);
  wr32(out, 76, 32);
  wr32(out, 80, 0x00000004u);
  wr32(out, 84, 0x31545844u); // DXT1
  wr32(out, 108, mips > 1 ? 0x00401008u : 0x00001000u);
  wr32(out, 112, 0);
  return out;
}

} // namespace

TEST_CASE("valid DDS passes validation", "[dds]") {
  const auto dds = make_dxt1_dds();
  const auto result = gf::textures::inspect_dds(dds);
  REQUIRE(result.status == gf::textures::DdsValidationStatus::Valid);
  REQUIRE(result.width == 4);
  REQUIRE(result.height == 4);
  REQUIRE(result.mipCount == 1);
  REQUIRE(result.expectedPayloadSizeMin == 8);
}

TEST_CASE("malformed DDS magic is rejected", "[dds]") {
  auto dds = make_dxt1_dds();
  dds[0] = 'B';
  const auto result = gf::textures::inspect_dds(dds);
  REQUIRE(result.status == gf::textures::DdsValidationStatus::Invalid);
}

TEST_CASE("bad header size is rejected", "[dds]") {
  auto dds = make_dxt1_dds();
  wr32(dds, 4, 120);
  const auto result = gf::textures::inspect_dds(dds);
  REQUIRE(result.status == gf::textures::DdsValidationStatus::Invalid);
}

TEST_CASE("bad pixel format size is rejected", "[dds]") {
  auto dds = make_dxt1_dds();
  wr32(dds, 76, 24);
  const auto result = gf::textures::inspect_dds(dds);
  REQUIRE(result.status == gf::textures::DdsValidationStatus::Invalid);
}

TEST_CASE("DX10 FourCC requires extension header", "[dds]") {
  auto dds = make_dxt1_dds();
  wr32(dds, 84, 0x30315844u); // DX10
  const auto result = gf::textures::inspect_dds(dds);
  REQUIRE(result.status == gf::textures::DdsValidationStatus::Invalid);
}

TEST_CASE("truncated payload is rejected", "[dds]") {
  auto dds = make_dxt1_dds(8, 8, 1, 8);
  const auto result = gf::textures::inspect_dds(dds);
  REQUIRE(result.status == gf::textures::DdsValidationStatus::Invalid);
  REQUIRE(result.payloadTruncated);
}

TEST_CASE("bad mip count versus payload is rejected", "[dds]") {
  auto dds = make_dxt1_dds(4, 4, 3, 8);
  const auto result = gf::textures::inspect_dds(dds);
  REQUIRE(result.status == gf::textures::DdsValidationStatus::Invalid);
}

TEST_CASE("BC1 and BC3 payload expectations are computed", "[dds]") {
  auto bc1 = make_dxt1_dds(8, 8, 1, 32);
  auto bc3 = make_dxt1_dds(8, 8, 1, 64);
  wr32(bc3, 84, 0x35545844u); // DXT5
  wr32(bc3, 20, 64);

  const auto bc1Result = gf::textures::inspect_dds(bc1);
  const auto bc3Result = gf::textures::inspect_dds(bc3);

  REQUIRE(bc1Result.expectedPayloadSizeMin == 32);
  REQUIRE(bc3Result.expectedPayloadSizeMin == 64);
}

TEST_CASE("wrapped DDS header is detected", "[dds]") {
  auto dds = make_dxt1_dds();
  std::vector<std::uint8_t> wrapped(16, 0xAB);
  wrapped.insert(wrapped.end(), dds.begin(), dds.end());
  const auto result = gf::textures::inspect_dds(wrapped);
  REQUIRE(result.status == gf::textures::DdsValidationStatus::Valid);
  REQUIRE(result.wrappedHeader);
  REQUIRE(result.dataOffset == 16);
}

TEST_CASE("P3R rebuild produces valid DDS", "[dds]") {
  auto p3r = make_dxt1_dds();
  p3r[0] = 'P'; p3r[1] = '3'; p3r[2] = 'R'; p3r[3] = 0x02;
  gf::textures::EaDdsInfo info{};
  const auto rebuilt = gf::textures::rebuild_ea_dds(p3r, &info);
  REQUIRE(rebuilt.has_value());
  const auto result = gf::textures::inspect_dds(*rebuilt);
  REQUIRE(result.status == gf::textures::DdsValidationStatus::Valid);
  REQUIRE(info.width == 4);
  REQUIRE(info.height == 4);
}
