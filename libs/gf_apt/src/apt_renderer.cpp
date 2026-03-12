#include <gf/apt/apt_renderer.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace gf::apt {

// ─────────────────────────────────────────────────────────────────────────────
// renderAptFrame
// ─────────────────────────────────────────────────────────────────────────────
//
// Recursion model (Flash display-list, column-vector math):
//
//   worldTransform = M_parent * M_local
//     (parent applied AFTER child's local transform)
//
// Using Transform2D::composeWith:
//   worldTransform = localTransform.composeWith(parentTransform)
//
// Depth ordering: placements are sorted ascending by their `depth` field so
// lower depths (further back) are added first; the scene then draws them in
// the order returned here (back-to-front / painter's algorithm).

std::vector<RenderNode> renderAptFrame(
    const AptFile&                   aptFile,
    const AptFrame&                  frame,
    const std::vector<AptCharacter>& characterTable,
    const Transform2D&               parentTransform,
    const RenderOptions&             opts,
    int                              currentDepth,
    int                              rootPlacementIndex,
    const std::string&               parentChainLabel)
{
  std::vector<RenderNode> nodes;

  if (currentDepth > opts.maxRecursionDepth)
    return nodes;

  // Sort a local index list by placement depth (ascending = back first).
  const std::size_t count = frame.placements.size();
  std::vector<std::size_t> order(count);
  for (std::size_t i = 0; i < count; ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(),
    [&](std::size_t x, std::size_t y) {
      return frame.placements[x].depth < frame.placements[y].depth;
    });

  for (const std::size_t pi : order) {
    const AptPlacement& pl = frame.placements[pi];

    // Track the root-frame placement index for highlight and editing.
    const int rootIdx = (currentDepth == 0)
        ? static_cast<int>(pi)
        : rootPlacementIndex;

    // Build the world transform: local placement first, then parent.
    // This is the key Flash-style composition step.
    const Transform2D localTransform = toTransform2D(pl.transform);
    const Transform2D worldTransform = localTransform.composeWith(parentTransform);

    // Character lookup: try the active characterTable first.
    // If the ID is out-of-range or a null slot, fall back to the root movie's character
    // table — nested Movie tables often reference characters defined in the root.
    // If still not found, emit an import-placeholder node.
    const AptCharacter* chPtr = nullptr;
    if (pl.character < characterTable.size() && characterTable[pl.character].type != 0) {
      chPtr = &characterTable[pl.character];
    } else if (&characterTable != &aptFile.characters
               && pl.character < aptFile.characters.size()
               && aptFile.characters[pl.character].type != 0) {
      chPtr = &aptFile.characters[pl.character];
    }

    if (!chPtr) {
      RenderNode node;
      node.kind             = RenderNode::Kind::Unknown;  // import / unresolved
      node.characterId      = pl.character;
      node.placementDepth   = pl.depth;
      node.instanceName     = pl.instance_name;
      node.worldTransform   = worldTransform;
      node.rootPlacementIndex = rootIdx;
      if (opts.collectParentChain) node.parentChainLabel = parentChainLabel;
      nodes.push_back(std::move(node));
      continue;
    }
    const AptCharacter& ch = *chPtr;

    // ── Sprite / Movie ─────────────────────────────────────────────────────
    if (ch.type == 5 || ch.type == 9) {
      if (!ch.frames.empty()) {
        std::string childLabel;
        if (opts.collectParentChain) {
          const std::string sprName = pl.instance_name.empty()
              ? ("Sprite C" + std::to_string(pl.character))
              : pl.instance_name;
          childLabel = parentChainLabel + " \xE2\x86\x92 " + sprName; // →
        }
        // Movie (type 9) characters carry their own nested character table.
        // When populated, prefer it for resolving this movie's placements.
        // The lookup above will fall back to the root table for any IDs not covered.
        const std::vector<AptCharacter>& childTable =
            (ch.type == 9 && !ch.nested_characters.empty())
                ? ch.nested_characters
                : characterTable;

        auto sub = renderAptFrame(aptFile, ch.frames[0], childTable,
                                   worldTransform, opts, currentDepth + 1,
                                   rootIdx, childLabel);
        if (!sub.empty()) {
          nodes.insert(nodes.end(),
                       std::make_move_iterator(sub.begin()),
                       std::make_move_iterator(sub.end()));
        } else {
          // Recursion produced no leaf nodes (depth-limited, empty hierarchy, or
          // all characters unresolvable). Emit a Sprite placeholder so the
          // placement origin is still visible in the preview.
          RenderNode plh;
          plh.kind             = RenderNode::Kind::Sprite;
          plh.characterId      = pl.character;
          plh.placementDepth   = pl.depth;
          plh.instanceName     = pl.instance_name;
          plh.worldTransform   = worldTransform;
          plh.localBounds      = ch.bounds;
          plh.rootPlacementIndex = rootIdx;
          if (opts.collectParentChain) plh.parentChainLabel = parentChainLabel;
          nodes.push_back(std::move(plh));
        }
      } else {
        // No frames parsed — emit a Sprite placeholder.
        RenderNode node;
        node.kind             = RenderNode::Kind::Sprite;
        node.characterId      = pl.character;
        node.placementDepth   = pl.depth;
        node.instanceName     = pl.instance_name;
        node.worldTransform   = worldTransform;
        node.localBounds      = ch.bounds;
        node.rootPlacementIndex = rootIdx;
        if (opts.collectParentChain) node.parentChainLabel = parentChainLabel;
        nodes.push_back(std::move(node));
      }
      continue;
    }

    // ── Leaf character (Shape, Image, EditText, Button, …) ─────────────────
    RenderNode node;
    node.kind             = kindFromCharType(ch.type);
    node.characterId      = pl.character;
    node.placementDepth   = pl.depth;
    node.instanceName     = pl.instance_name;
    node.worldTransform   = worldTransform;
    node.localBounds      = ch.bounds;
    node.rootPlacementIndex = rootIdx;
    if (opts.collectParentChain) node.parentChainLabel = parentChainLabel;
    nodes.push_back(std::move(node));
  }

  return nodes;
}

} // namespace gf::apt
