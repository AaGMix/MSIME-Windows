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

bool Show(HWND hwnd, const std::wstring &text, POINT caret, bool topmost);
bool Reposition(HWND hwnd, POINT caret, bool topmost);
void Hide(HWND hwnd);
void Paint(HWND hwnd, HDC dc);
} // namespace CaretStateIndicator
