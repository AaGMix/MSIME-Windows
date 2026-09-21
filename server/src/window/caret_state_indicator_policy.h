#pragma once

#include <optional>
#include <string>

namespace FanyImeUi
{
inline constexpr int kCaretStatePunctuationSlotWidthDip = 64;
inline constexpr int kCaretStatePunctuationModeGapDip = 0;
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

inline std::optional<int> CaretStateIndicatorPlacementY(bool requestedBelow, int anchorY, int indicatorHeight,
                                                        int caretLineHeight, int gap, int workTop, int workBottom)
{
    const int above = CaretStateIndicatorY(false, anchorY, indicatorHeight, caretLineHeight, gap);
    const int below = CaretStateIndicatorY(true, anchorY, indicatorHeight, caretLineHeight, gap);
    const auto fits = [workTop, workBottom, indicatorHeight](int y) {
        return y >= workTop && y <= workBottom - indicatorHeight;
    };
    if (requestedBelow)
        return fits(below) ? std::optional<int>(below) : (fits(above) ? std::optional<int>(above) : std::nullopt);
    return fits(above) ? std::optional<int>(above) : (fits(below) ? std::optional<int>(below) : std::nullopt);
}

inline int CaretStateIndicatorX(const std::string &position, int anchorX, int indicatorWidth, int gap)
{
    if (position == "top")
        return anchorX - indicatorWidth / 2;
    if (position == "top-right")
        return anchorX + gap;
    return anchorX - indicatorWidth - gap;
}

inline int CaretStateIndicatorTextWidth(int height, int scaledAdditionalCharacterWidth, int extraCharacters)
{
    return extraCharacters == 0 ? height : height + scaledAdditionalCharacterWidth * extraCharacters;
}

inline wchar_t InputModeGlyph(bool imeEnabled, bool japaneseMode)
{
    return imeEnabled ? (japaneseMode ? L'日' : L'中') : L'英';
}

inline wchar_t InputModeEventGlyph(bool imeEnabled, bool japaneseMode, bool capsLockEdge)
{
    return capsLockEdge ? L'英' : InputModeGlyph(imeEnabled, japaneseMode);
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
