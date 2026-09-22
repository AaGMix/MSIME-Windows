#include "Key/CaretAnchorPolicy.h"

int main()
{
    // Do not use assert: Release builds must execute these checks too.
    if (KeyEventPayloadWriteMask(false, false) != 0b000111u)
        return 1;
    if (KeyEventPayloadWriteMask(true, false) != 0b000111u)
        return 2;
    if (KeyEventPayloadWriteMask(true, true) != 0b001111u)
        return 3;
    if (!IsUsableCaretExtent(true, 10, 20, 10, 40))
        return 4;
    if (IsUsableCaretExtent(true, 10, 20, 10, 20))
        return 5;
    if (IsUsableCaretExtent(false, 0, 0, 0, 0))
        return 6;
    if (!IsUsableCaretExtent(false, 10, 20, 10, 40))
        return 7;
    return 0;
}
