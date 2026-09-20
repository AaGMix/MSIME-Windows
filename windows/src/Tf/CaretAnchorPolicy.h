#pragma once

struct CaretAnchorRect
{
    long left;
    long top;
    long right;
    long bottom;
};

struct CaretAnchorPoint
{
    long x;
    long y;
};

inline bool IsUsableCaretExtent(const CaretAnchorRect &rect, bool clipped)
{
    return !clipped && rect.right > rect.left && rect.bottom > rect.top;
}

inline bool SelectAdjacentCaretAnchor(const CaretAnchorRect *previous, const CaretAnchorRect *next,
                                      CaretAnchorPoint *anchor)
{
    if (!anchor || (!previous && !next))
        return false;
    if ((previous && !IsUsableCaretExtent(*previous, false)) || (next && !IsUsableCaretExtent(*next, false)))
        return false;

    if (previous && next)
    {
        const bool sameLine = previous->top < next->bottom && next->top < previous->bottom;
        const bool leftToRight = previous->right <= next->left;
        if (!sameLine || !leftToRight)
            return false;
    }

    const CaretAnchorRect &rect = previous ? *previous : *next;
    anchor->x = previous ? rect.right : rect.left;
    anchor->y = rect.bottom;
    return true;
}
