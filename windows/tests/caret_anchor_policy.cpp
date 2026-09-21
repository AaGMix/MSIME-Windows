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
    return 0;
}
