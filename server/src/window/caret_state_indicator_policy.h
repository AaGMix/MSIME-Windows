#pragma once

#include <string>

namespace FanyImeUi
{
inline constexpr int kCaretStatePunctuationSlotWidthDip = 64;
inline constexpr int kCaretStatePunctuationModeGapDip = 2;
inline constexpr int kCaretStatePunctuationModeSlotWidthDip = 30;
inline constexpr int kCaretStatePunctuationBadgeWidthDip =
    kCaretStatePunctuationSlotWidthDip + kCaretStatePunctuationModeGapDip + kCaretStatePunctuationModeSlotWidthDip;

enum class CaretStateKind
{
    Width,
    CharacterSet,
};

inline bool ShouldShowCaretStateIndicator(bool indicatorEnabled, bool floatingToolbarEnabled, bool imeActive,
                                          int anchorX, int anchorY)
{
    return indicatorEnabled && !floatingToolbarEnabled && imeActive && anchorY > -10000 &&
           (anchorX != 0 || anchorY != 0);
}

inline int CaretStateIndicatorY(bool belowCaret, int anchorY, int indicatorHeight, int caretLineHeight, int gap)
{
    // The TSF anchor is GetTextExt.bottom, so an upper badge must also clear
    // the caret's text line. The lower position already starts below it.
    return belowCaret ? anchorY + gap : anchorY - indicatorHeight - caretLineHeight - gap;
}

inline int CaretStateIndicatorX(const std::string &position, int anchorX, int indicatorWidth, int gap)
{
    if (position == "top")
        return anchorX - indicatorWidth / 2;
    if (position == "top-right")
        return anchorX + gap;
    return anchorX - indicatorWidth - gap;
}

inline wchar_t InputModeGlyph(bool imeEnabled, bool japaneseMode)
{
    return imeEnabled ? (japaneseMode ? L'日' : L'中') : L'英';
}

inline std::wstring PunctuationInputModeText(bool punctuationEnabled, bool imeEnabled, bool japaneseMode)
{
    std::wstring text = punctuationEnabled ? L"，。  " : L",.  ";
    text += InputModeGlyph(imeEnabled, japaneseMode);
    return text;
}

inline wchar_t CaretStateGlyph(CaretStateKind kind, bool enabled)
{
    switch (kind)
    {
    case CaretStateKind::Width:
        return enabled ? L'全' : L'半';
    case CaretStateKind::CharacterSet:
        return enabled ? L'繁' : L'简';
    }
    return L'\0';
}
} // namespace FanyImeUi
