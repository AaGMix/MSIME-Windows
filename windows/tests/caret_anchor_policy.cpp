#include "Tf/CaretAnchorPolicy.h"

int main()
{
    const CaretAnchorRect previous{10, 20, 18, 40};
    const CaretAnchorRect next{18, 20, 27, 40};
    CaretAnchorPoint anchor{};

    if (!SelectAdjacentCaretAnchor(&previous, &next, &anchor) || anchor.x != 18 || anchor.y != 40)
        return 1;
    if (!SelectAdjacentCaretAnchor(&previous, nullptr, &anchor) || anchor.x != 18 || anchor.y != 40)
        return 2;
    if (!SelectAdjacentCaretAnchor(nullptr, &next, &anchor) || anchor.x != 18 || anchor.y != 40)
        return 3;

    const CaretAnchorRect nextLine{0, 42, 9, 62};
    if (SelectAdjacentCaretAnchor(&previous, &nextLine, &anchor))
        return 4;

    const CaretAnchorRect bidiNext{4, 20, 9, 40};
    if (SelectAdjacentCaretAnchor(&previous, &bidiNext, &anchor))
        return 5;
    if (SelectAdjacentCaretAnchor(nullptr, nullptr, &anchor))
        return 6;
    if (SelectAdjacentCaretAnchor(&previous, &next, nullptr))
        return 7;

    const CaretAnchorRect degenerateWidth{10, 20, 10, 40};
    const CaretAnchorRect degenerateHeight{10, 20, 18, 20};
    const CaretAnchorRect inverted{18, 40, 10, 20};
    if (IsUsableCaretExtent(degenerateWidth, false) || IsUsableCaretExtent(degenerateHeight, false) ||
        IsUsableCaretExtent(inverted, false))
        return 8;
    if (IsUsableCaretExtent(previous, true) || !IsUsableCaretExtent(previous, false))
        return 9;
    if (SelectAdjacentCaretAnchor(&degenerateWidth, nullptr, &anchor) ||
        SelectAdjacentCaretAnchor(nullptr, &degenerateHeight, &anchor))
        return 10;

    const CaretAnchorRect separatedNext{21, 20, 27, 40};
    if (!SelectAdjacentCaretAnchor(nullptr, &separatedNext, &anchor) || anchor.x != 21 || anchor.y != 40)
        return 11;

    return 0;
}
