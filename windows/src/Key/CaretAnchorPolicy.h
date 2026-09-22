#pragma once

inline constexpr unsigned KeyEventPayloadWriteMask(bool includeCaretAnchor, bool anchorResolved)
{
    return includeCaretAnchor && anchorResolved ? 0b001111u : 0b000111u;
}

inline constexpr bool IsUsableCaretExtent(bool clipped, int left, int top, int right, int bottom)
{
    // A collapsed TSF range is a vertical caret and may legitimately have zero width.
    return !clipped || (right >= left && bottom > top);
}
