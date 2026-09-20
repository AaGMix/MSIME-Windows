#pragma once

#include <string>
#include <windows.h>

namespace CaretStateIndicator
{
struct ShowRequest
{
    std::wstring text;
    POINT caret;
};

void Show(HWND hwnd, const std::wstring &text, POINT caret, bool topmost);
void Hide(HWND hwnd);
void Paint(HWND hwnd, HDC dc);
} // namespace CaretStateIndicator
