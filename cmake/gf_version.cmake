# Central versioning: SemVer + optional build metadata
set(GF_VERSION_MAJOR 0)
set(GF_VERSION_MINOR 8)
set(GF_VERSION_PATCH 4)

set(GF_VERSION_REV "alpha")  # e.g. "alpha", "beta", "rc1", or empty for stable

set(GF_VERSION_STRING "${GF_VERSION_MAJOR}.${GF_VERSION_MINOR}.${GF_VERSION_PATCH}.${GF_VERSION_REV}")

# Generate a header with version info
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/templates/version.hpp.in
  ${CMAKE_BINARY_DIR}/generated/gf_core/version.hpp
  @ONLY
)
