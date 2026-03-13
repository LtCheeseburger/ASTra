#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gf::apt {

// Opcode context in which a string appeared in the bytecode stream.
// Used for conservative classification without executing the script.
enum class AptStringRole : std::uint8_t {
  PushLiteral  = 0, // EA_PUSHSTRING (0xA1): string pushed onto the value stack
  VarName      = 1, // EA_GETSTRINGVAR / EA_SETSTRINGVAR (0xA4/0xA6): variable name
  MemberName   = 2, // EA_GETSTRINGMEMBER / EA_SETSTRINGMEMBER (0xA5/0xA7): member/property
  TargetPath   = 3, // ACTION_SETTARGET (0x8B): target clip path string
  FrameLabel   = 4, // ACTION_GOTOLABEL (0x8C): frame or label name
  UrlTarget    = 5, // ACTION_GETURL (0x83): URL, frame target, or FSCommand string
  FunctionName = 6, // ACTION_DEFINEFUNCTION / 2 (0x9B/0x8E): defined function name
};

struct AptActionString {
  std::string   value;
  AptStringRole role      = AptStringRole::PushLiteral;
  // True when a call opcode was observed within ~8 opcodes AFTER this push.
  // This is the strongest conservative signal that a pushed string was used as
  // a symbol or method name in an attachMovie / gotoAndPlay-style call.
  bool          near_call = false;
};

// A conservative call-pattern detection entry from the action bytecode stream.
// Captured without AVM1 execution — arg_strings are the pushed string literals
// observed in the near-call window before the CALLNAMED* opcode fired.
//
// AVM1 argument order: args are pushed rightmost-first onto the stack, so
// arg_strings[0] is the FIRST (leftmost) argument, e.g. symbolName for
// attachMovie(symbolName, instanceName, depth).
struct AptDetectedCall {
  std::string              call_name;   // function name resolved from local constant pool
  std::vector<std::string> arg_strings; // up to 4 pushed strings before the call
  bool                     from_pool = false; // call_name resolved via CONSTANTPOOL
};

struct AptActionHints {
  std::vector<AptActionString>  strings;        // classified, deduplicated string literals
  std::vector<AptDetectedCall>  detected_calls; // conservative call-pattern detections
  bool has_call_patterns    = false;            // any function/method call opcode seen
  bool likely_attach_movie  = false;            // PushLiteral + near_call pattern observed
  std::uint32_t opcode_count = 0;               // total opcodes walked (0 = none reached)
};

// Walk EA APT action bytecodes starting at aptBuf[bytesOffset] and extract
// inline string references with opcode-context classification and near_call tagging.
//
// Bytecode layout:
//   - Opcode: single byte, pointer advanced by 1.
//   - Most complex opcodes: ALIGN pointer to 4-byte boundary, then read operands.
//   - String operands: ALIGN + 4-byte LE offset into aptBuf → null-terminated C-string.
//   - Terminates at ACTION_END (0x00).
//
// near_call detection:
//   - A "pending push" list accumulates PushLiteral strings as they appear.
//   - When a call opcode is observed within 8 opcodes, all pending pushes are
//     marked near_call=true and likely_attach_movie is set.
//   - The pending list is cleared after 8 non-call opcodes or when a call fires.
//
// constBuf (optional): raw bytes of the companion .const file.
//   When provided, ACTION_CONSTANTPOOL (0x88) builds a local constant pool by
//   resolving CONST item indices to string values, and EA_PUSHCONSTANT (0xA2),
//   EA_PUSHWORDCONSTANT (0xA3), EA_CALLNAMEDFUNCTION* (0xB0-0xB3),
//   EA_PUSHVALUEOFVAR (0xAE), and EA_GETNAMEDMEMBER (0xAF) resolve their
//   pool-index operands to actual string values.  ACTION_PUSHDATA (0x96) pushes
//   CONST strings directly onto the hints as PushLiteral entries.
//
// Returns empty hints if bytesOffset is 0 or out of range.
inline AptActionHints inspect_apt_actions(
    const std::vector<std::uint8_t>& aptBuf,
    std::uint64_t bytesOffset,
    const std::vector<std::uint8_t>& constBuf = {})
{
  AptActionHints hints;
  if (bytesOffset == 0 || bytesOffset >= aptBuf.size()) return hints;

  constexpr std::uint64_t kMaxOpcodes      = 4096;
  constexpr std::size_t   kMaxStrings      = 64;
  constexpr std::uint64_t kMaxStrLen       = 256;
  constexpr std::uint32_t kNearCallWindow  = 8;  // opcode distance for near_call

  auto align4 = [](std::uint64_t off) -> std::uint64_t {
    return (off + 3) & ~std::uint64_t(3);
  };

  // Parse CONST file item table (big-endian on-disk format):
  //   0x00-0x13: "Apt constant file\x1A\0\0" header
  //   0x14: aptdataoffset (u32 BE)
  //   0x18: itemcount (u32 BE)
  //   0x1C: items-array ptr (u32 BE) = 0x20
  //   0x20+: items[i] = type(u32 BE) + value(u32 BE); type 1 = string offset
  // constItems[i] holds the string for item i (empty for non-string / out-of-range).
  std::vector<std::string> constItems;
  if (constBuf.size() >= 0x20) {
    auto rd_be = [&](std::size_t off) -> std::uint32_t {
      if (off + 4 > constBuf.size()) return 0;
      const std::uint8_t* p = constBuf.data() + off;
      return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16)
           | (std::uint32_t(p[2]) <<  8) |  std::uint32_t(p[3]);
    };
    const std::uint32_t itemcount = rd_be(0x18);
    constItems.reserve(std::min(itemcount, std::uint32_t(4096)));
    for (std::uint32_t i = 0; i < itemcount && constItems.size() < 4096; ++i) {
      const std::size_t off = 0x20 + std::size_t(i) * 8;
      if (off + 8 > constBuf.size()) { constItems.emplace_back(); break; }
      const std::uint32_t type = rd_be(off);
      const std::uint32_t val  = rd_be(off + 4);
      if (type == 1 && val > 0 && val < constBuf.size()) {
        std::size_t j = val;
        while (j < constBuf.size() && constBuf[j] != 0 && (j - val) < 256) ++j;
        constItems.emplace_back(
            reinterpret_cast<const char*>(constBuf.data() + val),
            reinterpret_cast<const char*>(constBuf.data() + j));
      } else {
        constItems.emplace_back();
      }
    }
  }

  // Active local constant pool (set by ACTION_CONSTANTPOOL 0x88).
  // Maps pool slot index → string value resolved from constItems.
  std::vector<std::string> localPool;

  auto read_u32le = [&](std::uint64_t off) -> std::uint32_t {
    if (off + 4 > aptBuf.size()) return 0;
    const std::uint8_t* p = aptBuf.data() + off;
    return std::uint32_t(p[0])
         | (std::uint32_t(p[1]) << 8)
         | (std::uint32_t(p[2]) << 16)
         | (std::uint32_t(p[3]) << 24);
  };

  auto read_str = [&](std::uint64_t strOff) -> std::string {
    if (strOff == 0 || strOff >= aptBuf.size()) return {};
    const char* s = reinterpret_cast<const char*>(aptBuf.data() + strOff);
    std::uint64_t i = strOff;
    while (i < aptBuf.size() && (i - strOff) < kMaxStrLen && aptBuf[i] != 0) ++i;
    return std::string(s, reinterpret_cast<const char*>(aptBuf.data() + i));
  };

  // Pending PushLiteral indices (into hints.strings) awaiting near_call confirmation.
  std::vector<std::size_t> pendingPushIndices;
  std::uint32_t opcodesSinceLastPush = kNearCallWindow + 1;

  auto flush_near_calls = [&]() {
    for (auto idx : pendingPushIndices) {
      hints.strings[idx].near_call = true;
    }
    if (!pendingPushIndices.empty()) hints.likely_attach_movie = true;
    pendingPushIndices.clear();
    opcodesSinceLastPush = kNearCallWindow + 1;
  };

  auto push_str = [&](std::string s, AptStringRole role) {
    if (s.empty()) return;
    // Reject non-printable / binary garbage
    int printable = 0;
    for (unsigned char c : s) if (c >= 0x20 && c < 0x7F) ++printable;
    if (printable < static_cast<int>(s.size()) / 2) return;
    // Dedup by value
    for (const auto& e : hints.strings) if (e.value == s) return;
    if (hints.strings.size() >= kMaxStrings) return;
    hints.strings.push_back({std::move(s), role, false});
  };

  std::uint64_t pos = bytesOffset;

  while (hints.opcode_count < kMaxOpcodes && pos < aptBuf.size()) {
    const std::uint8_t op = aptBuf[pos];
    ++pos;
    ++hints.opcode_count;

    if (op == 0x00) break; // ACTION_END

    // Tick near_call window
    if (opcodesSinceLastPush <= kNearCallWindow) {
      ++opcodesSinceLastPush;
      if (opcodesSinceLastPush > kNearCallWindow)
        pendingPushIndices.clear(); // expired without call
    }

    // ── EA_PUSHSTRING (0xA1): ALIGN + string ptr ──────────────────────────
    if (op == 0xA1) {
      pos = align4(pos);
      const std::size_t before = hints.strings.size();
      push_str(read_str(read_u32le(pos)), AptStringRole::PushLiteral);
      pos += 4;
      if (hints.strings.size() > before) {
        pendingPushIndices.push_back(hints.strings.size() - 1);
        opcodesSinceLastPush = 0;
      }
      continue;
    }

    // ── EA_GETSTRINGVAR=0xA4, EA_SETSTRINGVAR=0xA6 ───────────────────────
    if (op == 0xA4 || op == 0xA6) {
      pos = align4(pos);
      push_str(read_str(read_u32le(pos)), AptStringRole::VarName);
      pos += 4;
      continue;
    }

    // ── EA_GETSTRINGMEMBER=0xA5, EA_SETSTRINGMEMBER=0xA7 ─────────────────
    if (op == 0xA5 || op == 0xA7) {
      pos = align4(pos);
      push_str(read_str(read_u32le(pos)), AptStringRole::MemberName);
      pos += 4;
      continue;
    }

    // ── ACTION_SETTARGET=0x8B ─────────────────────────────────────────────
    if (op == 0x8B) {
      pos = align4(pos);
      push_str(read_str(read_u32le(pos)), AptStringRole::TargetPath);
      pos += 4;
      continue;
    }

    // ── ACTION_GOTOLABEL=0x8C ─────────────────────────────────────────────
    if (op == 0x8C) {
      pos = align4(pos);
      push_str(read_str(read_u32le(pos)), AptStringRole::FrameLabel);
      pos += 4;
      continue;
    }

    // ── ACTION_GETURL=0x83: ALIGN + two string ptrs ───────────────────────
    if (op == 0x83) {
      pos = align4(pos);
      push_str(read_str(read_u32le(pos)), AptStringRole::UrlTarget); pos += 4;
      push_str(read_str(read_u32le(pos)), AptStringRole::UrlTarget); pos += 4;
      continue;
    }

    // ── ACTION_DEFINEFUNCTION=0x9B / ACTION_DEFINEFUNCTION2=0x8E ─────────
    if (op == 0x9B || op == 0x8E) {
      pos = align4(pos);
      push_str(read_str(read_u32le(pos)), AptStringRole::FunctionName);
      break; // can't cheaply skip function body
    }

    // ── ACTION_CONSTANTPOOL=0x88 / ACTION_PUSHDATA=0x96 ──────────────────
    // Layout (LE action bytecode operands):
    //   ALIGN; count(u32 LE); pool_data_ptr(u32 LE)
    // pool_data_ptr points into aptBuf to an array of count u32 LE indices
    // into the CONST file item table (constItems[]).
    //
    // 0x88: establishes a local constant pool for subsequent PUSHCONSTANT refs.
    // 0x96: pushes values directly onto the AVM1 stack as PushLiterals.
    if (op == 0x88 || op == 0x96) {
      pos = align4(pos);
      if (pos + 8 > aptBuf.size()) break;
      const std::uint32_t count   = read_u32le(pos);
      const std::uint32_t cpd_off = read_u32le(pos + 4);
      pos += 8;
      if (!constItems.empty() && count > 0 && count < 4096 &&
          std::uint64_t(cpd_off) + std::uint64_t(count) * 4 <= aptBuf.size()) {
        if (op == 0x88) {
          // Establish local constant pool for PUSHCONSTANT/CALLNAMED* opcodes.
          localPool.clear();
          localPool.reserve(count);
          for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t ci = read_u32le(std::uint64_t(cpd_off) + std::uint64_t(i) * 4);
            localPool.push_back(ci < constItems.size() ? constItems[ci] : std::string{});
          }
        } else {
          // PUSHDATA: push each resolved string directly as a PushLiteral.
          for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t ci = read_u32le(std::uint64_t(cpd_off) + std::uint64_t(i) * 4);
            if (ci < constItems.size() && !constItems[ci].empty()) {
              const std::size_t before = hints.strings.size();
              push_str(constItems[ci], AptStringRole::PushLiteral);
              if (hints.strings.size() > before) {
                pendingPushIndices.push_back(hints.strings.size() - 1);
                opcodesSinceLastPush = 0;
              }
            }
          }
        }
      }
      continue;
    }

    // ── ALIGN + 4-byte int operand ─────────────────────────────────────────
    if (op == 0x81 || op == 0x99 || op == 0x9D || op == 0xB8 || op == 0x9F
                   || op == 0x94 || op == 0x87 || op == 0x8A) {
      pos = align4(pos);
      pos += 4;
      continue;
    }

    // ── Call-pattern opcodes (no operands) ────────────────────────────────
    if (op == 0x3D || op == 0x52 || op == 0x53 || op == 0x40
                   || op == 0x5B || op == 0x5C || op == 0x5D || op == 0x5E) {
      hints.has_call_patterns = true;
      flush_near_calls();
      continue;
    }

    // ── EA_CALLNAMEDFUNCTION* (0xB0-0xB3): 1-byte local-pool index ───────
    // The pool entry is the name of the function/method being called.
    // Record a detected call with the pending push strings as arguments BEFORE
    // flush_near_calls clears the pending list.
    if (op == 0xB0 || op == 0xB1 || op == 0xB2 || op == 0xB3) {
      hints.has_call_patterns = true;
      const std::uint8_t idx = (pos < aptBuf.size()) ? aptBuf[pos] : 0;
      pos += 1;
      const std::string callName = (idx < localPool.size()) ? localPool[idx] : std::string{};
      if (!callName.empty()) {
        // Snapshot arg_strings from the pending push queue.
        // AVM1 pushes args rightmost-first, so pendingPushIndices[0] = first (leftmost) arg.
        AptDetectedCall dc;
        dc.call_name = callName;
        dc.from_pool = true;
        for (auto argIdx : pendingPushIndices) {
          if (dc.arg_strings.size() >= 4) break;
          if (argIdx < hints.strings.size())
            dc.arg_strings.push_back(hints.strings[argIdx].value);
        }
        hints.detected_calls.push_back(std::move(dc));
        push_str(callName, AptStringRole::FunctionName);
      }
      flush_near_calls();
      continue;
    }

    // ── EA_PUSHCONSTANT (0xA2): 1-byte local-pool index → PushLiteral ────
    if (op == 0xA2) {
      const std::uint8_t idx = (pos < aptBuf.size()) ? aptBuf[pos] : 0;
      pos += 1;
      if (idx < localPool.size() && !localPool[idx].empty()) {
        const std::size_t before = hints.strings.size();
        push_str(localPool[idx], AptStringRole::PushLiteral);
        if (hints.strings.size() > before) {
          pendingPushIndices.push_back(hints.strings.size() - 1);
          opcodesSinceLastPush = 0;
        }
      }
      continue;
    }

    // ── EA_PUSHVALUEOFVAR (0xAE): 1-byte pool index → variable name ──────
    if (op == 0xAE) {
      const std::uint8_t idx = (pos < aptBuf.size()) ? aptBuf[pos] : 0;
      pos += 1;
      if (idx < localPool.size() && !localPool[idx].empty())
        push_str(localPool[idx], AptStringRole::VarName);
      continue;
    }

    // ── EA_GETNAMEDMEMBER (0xAF): 1-byte pool index → member name ────────
    if (op == 0xAF) {
      const std::uint8_t idx = (pos < aptBuf.size()) ? aptBuf[pos] : 0;
      pos += 1;
      if (idx < localPool.size() && !localPool[idx].empty())
        push_str(localPool[idx], AptStringRole::MemberName);
      continue;
    }

    // ── Other 1-byte operand opcodes ─────────────────────────────────────
    if (op == 0xB5 || op == 0xB9) {
      pos += 1;
      continue;
    }

    // ── EA_PUSHWORDCONSTANT (0xA3): 2-byte LE pool index → PushLiteral ──
    if (op == 0xA3) {
      const std::uint16_t idx =
          (pos + 1 < aptBuf.size())
          ? (std::uint16_t(aptBuf[pos]) | (std::uint16_t(aptBuf[pos + 1]) << 8))
          : std::uint16_t(0);
      pos += 2;
      if (idx < localPool.size() && !localPool[idx].empty()) {
        const std::size_t before = hints.strings.size();
        push_str(localPool[idx], AptStringRole::PushLiteral);
        if (hints.strings.size() > before) {
          pendingPushIndices.push_back(hints.strings.size() - 1);
          opcodesSinceLastPush = 0;
        }
      }
      continue;
    }

    // ── 2-byte operand opcodes ─────────────────────────────────────────────
    if (op == 0xB6) {
      pos += 2;
      continue;
    }

    // ── 4-byte operand opcodes ─────────────────────────────────────────────
    if (op == 0xB4 || op == 0xB7) {
      pos += 4;
      continue;
    }

    // All remaining: no operands.
  }

  return hints;
}

} // namespace gf::apt
