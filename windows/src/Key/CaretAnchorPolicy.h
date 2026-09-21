#pragma once

inline constexpr unsigned KeyEventPayloadWriteMask(bool includeCaretAnchor, bool anchorResolved)
{
    return includeCaretAnchor && anchorResolved ? 0b001111u : 0b000111u;
}
