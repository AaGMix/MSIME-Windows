#include "window/caret_state_indicator.h"
#include "config/ime_config.h"
#include "utils/window_utils.h"
#include "window/caret_state_indicator_policy.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr UINT kHideTimer = 1;
constexpr UINT kHideDelayMs = 1500;
constexpr int kBaseHeightDip = 30;
constexpr int kAdditionalCharacterWidthDip = 20;
constexpr int kPunctuationSlotWidthDip = 72;
constexpr int kPunctuationModeSlotWidthDip = 30;
constexpr int kPunctuationModeGapDip = 4;
constexpr int kCaretGapDip = 6;
constexpr int kCaretLineHeightDip = 24;

struct State
{
    std::wstring text = L"中";
    UINT dpi = 96;
};

State g_state;

int PixelSize(int dip, UINT dpi)
{
    return (std::max)(1, static_cast<int>(std::lround(dip * static_cast<double>(dpi) / 96.0)));
}
} // namespace

namespace CaretStateIndicator
{
void Show(HWND hwnd, const std::wstring &text, POINT caret, bool topmost)
{
    if (!hwnd || !IsWindow(hwnd))
        return;

    HMONITOR monitor = MonitorFromPoint(caret, MONITOR_DEFAULTTONEAREST);
    g_state.text = text;
    g_state.dpi = static_cast<UINT>(std::lround(GetScaleForPoint(caret) * 96.0f));
    const int height = PixelSize(kBaseHeightDip, g_state.dpi);
    const int extraCharacters = (std::max)(0, static_cast<int>(text.size()) - 1);
    const int width =
        text.size() == 5
            ? PixelSize(kPunctuationSlotWidthDip, g_state.dpi) +
                  PixelSize(kPunctuationModeGapDip, g_state.dpi) +
                  PixelSize(kPunctuationModeSlotWidthDip, g_state.dpi)
            : height + PixelSize(kAdditionalCharacterWidthDip * extraCharacters, g_state.dpi);
    const int gap = PixelSize(kCaretGapDip, g_state.dpi);
    const int caretLineHeight = PixelSize(kCaretLineHeightDip, g_state.dpi);
    MONITORINFO info{sizeof(info)};
    RECT work{};
    if (monitor && GetMonitorInfoW(monitor, &info))
        work = info.rcWork;
    else
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    const std::string &position = GetConfiguredCaretStateIndicatorPosition();
    int x = caret.x - width - gap;
    int y = FanyImeUi::CaretStateIndicatorY(position == "bottom", caret.y, height, caretLineHeight, gap);
    if (position == "top")
        x = caret.x - width / 2;
    else if (position == "top-right")
        x = caret.x + gap;
    if (position != "bottom" && y < work.top)
        y = FanyImeUi::CaretStateIndicatorY(true, caret.y, height, caretLineHeight, gap);
    x = static_cast<int>((std::max)(work.left, (std::min)(static_cast<LONG>(x), work.right - width)));
    y = static_cast<int>((std::max)(work.top, (std::min)(static_cast<LONG>(y), work.bottom - height)));
    SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(hwnd, kHideTimer, kHideDelayMs, nullptr);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void Hide(HWND hwnd)
{
    if (!hwnd)
        return;
    KillTimer(hwnd, kHideTimer);
    ShowWindow(hwnd, SW_HIDE);
}

void Paint(HWND hwnd, HDC dc)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const bool light = ResolveConfiguredTheme(GetConfiguredThemeFtb()) == "light";
    HBRUSH bg = CreateSolidBrush(light ? RGB(255, 255, 255) : RGB(32, 32, 32));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    FrameRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(light ? GRAY_BRUSH : WHITE_BRUSH)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, light ? RGB(26, 26, 26) : RGB(255, 255, 255));
    HFONT font =
        CreateFontW(-PixelSize(20, g_state.dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ old = SelectObject(dc, font);
    if (g_state.text.size() == 5)
    {
        RECT modeRect = rc;
        modeRect.left = (std::max)(rc.left, rc.right - PixelSize(kPunctuationModeSlotWidthDip, g_state.dpi));
        RECT punctuationRect = rc;
        punctuationRect.right =
            (std::max)(punctuationRect.left,
                       modeRect.left - PixelSize(kPunctuationModeGapDip, g_state.dpi));
        DrawTextW(dc, g_state.text.c_str(), 2, &punctuationRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(dc, g_state.text.c_str() + 4, 1, &modeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else
    {
        DrawTextW(dc, g_state.text.c_str(), static_cast<int>(g_state.text.size()), &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, old);
    DeleteObject(font);
}
} // namespace CaretStateIndicator
