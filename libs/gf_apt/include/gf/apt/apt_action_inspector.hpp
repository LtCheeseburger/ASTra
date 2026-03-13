#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gf::apt {

struct AptActionHints {
  std::vector<std::string> strings;  // deduplicated string literals found in bytecodes
  bool has_call_patterns = false;    // true if any function/method call opcode was seen
  std::uint32_t opcode_count = 0;   // total opcodes walked (0 = no bytecodes or not reached)
};

// Walk EA APT action bytecodes starting at aptBuf[bytesOffset] and extract
// inline string references and call-pattern flags.
//
// Bytecode layout follows the EA APT format (as used in ActionHelper.cpp):
//   - Opcode is a single byte, read and pointer advanced by 1.
//   - Most complex opcodes: ALIGN pointer to 4-byte boundary, then read operands.
//   - String operands: ALIGN + 4-byte LE offset into aptBuf → null-terminated C-string.
//   - Terminates at ACTION_END (0x00).
//
// Pointer operands in the bytecode stream are stored little-endian (native EA tool
// output on x86 Windows), while the outer APT container structures are big-endian.
//
// Returns empty hints if bytesOffset is 0 or out of range.
inline AptActionHints inspect_apt_actions(
    const std::vector<std::uint8_t>& aptBuf,
    std::uint64_t bytesOffset)
{
  AptActionHints hints;
  if (bytesOffset == 0 || bytesOffset >= aptBuf.size()) return hints;

  constexpr std::uint64_t kMaxOpcodes = 4096;
  constexpr std::size_t   kMaxStrings = 64;
  constexpr std::uint64_t kMaxStrLen  = 256;

  // Align offset to next 4-byte boundary (mirrors ALIGN macro in ActionHelper.cpp).
  auto align4 = [](std::uint64_t off) -> std::uint64_t {
    return (off + 3) & ~std::uint64_t(3);
  };

  // Read 4-byte little-endian value — used for pointer operands in bytecodes.
  auto read_u32le = [&](std::uint64_t off) -> std::uint32_t {
    if (off + 4 > aptBuf.size()) return 0;
    const std::uint8_t* p = aptBuf.data() + off;
    return std::uint32_t(p[0])
         | (std::uint32_t(p[1]) << 8)
         | (std::uint32_t(p[2]) << 16)
         | (std::uint32_t(p[3]) << 24);
  };

  // Read null-terminated string from aptBuf at absolute offset.
  auto read_str = [&](std::uint64_t strOff) -> std::string {
    if (strOff == 0 || strOff >= aptBuf.size()) return {};
    const char* s = reinterpret_cast<const char*>(aptBuf.data() + strOff);
    std::uint64_t i = strOff;
    while (i < aptBuf.size() && (i - strOff) < kMaxStrLen && aptBuf[i] != 0) ++i;
    return std::string(s, reinterpret_cast<const char*>(aptBuf.data() + i));
  };

  auto push_str = [&](std::string s) {
    if (s.empty()) return;
    // Reject non-printable / binary garbage (at least half chars must be printable ASCII)
    int printable = 0;
    for (unsigned char c : s) if (c >= 0x20 && c < 0x7F) ++printable;
    if (printable < static_cast<int>(s.size()) / 2) return;
    for (const auto& e : hints.strings) if (e == s) return;  // dedup
    if (hints.strings.size() < kMaxStrings) hints.strings.push_back(std::move(s));
  };

  std::uint64_t pos = bytesOffset;

  while (hints.opcode_count < kMaxOpcodes && pos < aptBuf.size()) {
    const std::uint8_t op = aptBuf[pos];
    ++pos;
    ++hints.opcode_count;

    if (op == 0x00) break; // ACTION_END

    // ── ALIGN + 4-byte LE string pointer ──────────────────────────────────
    // EA_PUSHSTRING=0xA1, EA_GETSTRINGVAR=0xA4, EA_GETSTRINGMEMBER=0xA5
    // EA_SETSTRINGVAR=0xA6, EA_SETSTRINGMEMBER=0xA7
    // ACTION_SETTARGET=0x8B, ACTION_GOTOLABEL=0x8C
    if (op == 0xA1 || op == 0xA4 || op == 0xA5 || op == 0xA6 || op == 0xA7
                   || op == 0x8B || op == 0x8C) {
      pos = align4(pos);
      push_str(read_str(read_u32le(pos)));
      pos += 4;
      continue;
    }

    // ACTION_GETURL=0x83: ALIGN + two string ptrs
    if (op == 0x83) {
      pos = align4(pos);
      push_str(read_str(read_u32le(pos)));       pos += 4;
      push_str(read_str(read_u32le(pos)));       pos += 4;
      continue;
    }

    // ACTION_DEFINEFUNCTION=0x9B: ALIGN + name_ptr(4) + count(4) + args_off(4) + size(4) + x(4)+x(4)
    // ACTION_DEFINEFUNCTION2=0x8E: ALIGN + name_ptr(4) + count(4) + flags(4) + args_off(4) + size(4) + x(4)+x(4)
    // Don't walk into function body (can't cheaply skip without knowing the size value).
    if (op == 0x9B || op == 0x8E) {
      pos = align4(pos);
      push_str(read_str(read_u32le(pos)));  pos += 4;  // function name
      // skip remaining fixed header; stop walking (body follows inline)
      break;
    }

    // ACTION_CONSTANTPOOL=0x88: ALIGN + count(4) + cpd_off(4)
    // ACTION_PUSHDATA=0x96: ALIGN + count(4) + pid_off(4)
    // (string values live in CONST data — not accessible here)
    if (op == 0x88 || op == 0x96) {
      pos = align4(pos);
      pos += 8;
      continue;
    }

    // ── ALIGN + 4-byte int operand ─────────────────────────────────────────
    // ACTION_GOTOFRAME=0x81, ACTION_BRANCHALWAYS=0x99, ACTION_BRANCHIFTRUE=0x9D
    // EA_BRANCHIFFALSE=0xB8, ACTION_GOTOEXPRESSION=0x9F
    // ACTION_WITH=0x94, ACTION_SETREGISTER=0x87, ACTION_WAITFORFRAME=0x8A
    if (op == 0x81 || op == 0x99 || op == 0x9D || op == 0xB8 || op == 0x9F
                   || op == 0x94 || op == 0x87 || op == 0x8A) {
      pos = align4(pos);
      pos += 4;
      continue;
    }

    // ── Call-pattern opcodes (no operands) ────────────────────────────────
    // ACTION_CALLFUNCTION=0x3D, ACTION_CALLMETHOD=0x52, ACTION_NEWMETHOD=0x53
    // ACTION_NEW=0x40
    // EA_CALLFUNCTIONPOP=0x5B, EA_CALLFUNCTION=0x5C
    // EA_CALLMETHODPOP=0x5D, EA_CALLMETHOD=0x5E
    if (op == 0x3D || op == 0x52 || op == 0x53 || op == 0x40
                   || op == 0x5B || op == 0x5C || op == 0x5D || op == 0x5E) {
      hints.has_call_patterns = true;
      continue;
    }

    // EA_CALLNAMEDFUNCTIONPOP=0xB0, EA_CALLNAMEDFUNCTION=0xB1
    // EA_CALLNAMEDMETHODPOP=0xB2, EA_CALLNAMEDMETHOD=0xB3: 1-byte constant index
    if (op == 0xB0 || op == 0xB1 || op == 0xB2 || op == 0xB3) {
      hints.has_call_patterns = true;
      pos += 1;
      continue;
    }

    // ── 1-byte operand opcodes ─────────────────────────────────────────────
    // EA_PUSHBYTE=0xB5, EA_PUSHREGISTER=0xB9, EA_PUSHCONSTANT=0xA2
    // EA_PUSHVALUEOFVAR=0xAE, EA_GETNAMEDMEMBER=0xAF
    if (op == 0xB5 || op == 0xB9 || op == 0xA2 || op == 0xAE || op == 0xAF) {
      pos += 1;
      continue;
    }

    // ── 2-byte operand opcodes ─────────────────────────────────────────────
    // EA_PUSHSHORT=0xB6, EA_PUSHWORDCONSTANT=0xA3
    if (op == 0xB6 || op == 0xA3) {
      pos += 2;
      continue;
    }

    // ── 4-byte operand opcodes ─────────────────────────────────────────────
    // EA_PUSHFLOAT=0xB4, EA_PUSHLONG=0xB7
    if (op == 0xB4 || op == 0xB7) {
      pos += 4;
      continue;
    }

    // All remaining opcodes have no operands (single byte).
    // Includes all simple stack/arithmetic/comparison ops (0x04-0x6F range).
    // Unknown opcodes also treated as no-operand to avoid misparse.
  }

  return hints;
}

} // namespace gf::apt
