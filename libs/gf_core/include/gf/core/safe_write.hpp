#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace gf::core {

// Safe-write helper used by editors (v0.6.0+).
//
// Design goals:
//  - Never partially write the destination file.
//  - Provide predictable, user-friendly backups.
//  - Be std-only and cross-platform.
//
// Strategy:
//  1) Write bytes to a temp file next to the target.
//  2) (Optional) Rename existing target to a timestamped backup.
//  3) Rename temp into place.
//  4) If step 3 fails and a backup exists, attempt rollback.

struct SafeWriteOptions {
  // Create a timestamped backup if the destination already exists.
  bool make_backup = true;

  // Backup file suffix pattern (no path separators). The final backup name is:
  //   <target_filename><backup_suffix>
  // Example: ".bak_20260210_143012"
  std::string backup_suffix = ""; // empty => auto timestamp

  // Maximum bytes to write. Helps protect against accidental huge writes.
  // 0 means "no limit".
  std::uint64_t max_bytes = 0;
};

struct SafeWriteResult {
  bool ok = false;
  std::string message;
  std::optional<std::filesystem::path> backup_path;
  std::optional<std::filesystem::path> temp_path;
};

// Safely writes `data` to `target`.
SafeWriteResult safe_write_bytes(const std::filesystem::path& target,
                                std::span<const std::byte> data,
                                const SafeWriteOptions& opt = {});

// Convenience overload for text.
SafeWriteResult safe_write_text(const std::filesystem::path& target,
                               std::string_view text,
                               const SafeWriteOptions& opt = {});

} // namespace gf::core
