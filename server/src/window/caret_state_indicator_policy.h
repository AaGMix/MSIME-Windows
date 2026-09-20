#pragma once

#include <string>

namespace FanyImeUi
{
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
