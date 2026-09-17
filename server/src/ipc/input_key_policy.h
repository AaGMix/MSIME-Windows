#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace FanyImeIpc
{
inline constexpr uint32_t kVirtualKeyShift = 0x10;
inline constexpr uint32_t kVirtualKeyEscape = 0x1B;
inline constexpr uint32_t kVirtualKeyBackspace = 0x08;
inline constexpr uint32_t kVirtualKeyLeftShift = 0xA0;
inline constexpr uint32_t kVirtualKeyRightShift = 0xA1;
inline constexpr uint32_t kVirtualKeyNumpad0 = 0x60;
inline constexpr uint32_t kVirtualKeyNumpad9 = 0x69;
inline constexpr uint32_t kModifierShift = 0b00000001u;
inline constexpr uint32_t kModifierControl = 0b00000010u;
inline constexpr uint32_t kModifierAlt = 0b00000100u;
inline constexpr uint32_t kModifierUiLess = 0x80000000u;
inline constexpr uint32_t kEnglishModeToggleModifiers = kModifierShift | kModifierControl;
inline constexpr uint32_t kKeyModifierMask = kModifierShift | kModifierControl | kModifierAlt;

constexpr bool IsEnglishModeToggleKey(uint32_t keycode, uint32_t modifiers_down)
{
    return keycode == static_cast<uint32_t>('E') && (modifiers_down & kKeyModifierMask) == kEnglishModeToggleModifiers;
}

// Return -1 / +1 for the first / last Han character, or zero for an ordinary key.
constexpr int WordToCharacterDirection(uint32_t keycode, uint32_t character, uint32_t modifiers, bool enabled,
                                       bool minus_equal)
{
    if (!enabled || (modifiers & kKeyModifierMask) != 0)
        return 0;
    if (minus_equal)
        return keycode == 0xBD && character == '-' ? -1 : keycode == 0xBB && character == '=' ? 1 : 0;
    return keycode == 0xDB && character == '[' ? -1 : keycode == 0xDD && character == ']' ? 1 : 0;
}

// The TSF side treats numpad digits exactly like the corresponding candidate
// digit. Canonicalize them at the Server boundary so every downstream policy
// sees the same key code and, crucially, produces a reply for the request.
constexpr uint32_t NormalizeNumpadDigitKey(uint32_t keycode)
{
    return keycode >= kVirtualKeyNumpad0 && keycode <= kVirtualKeyNumpad9
               ? static_cast<uint32_t>('0') + (keycode - kVirtualKeyNumpad0)
               : keycode;
}

// TSF locally consumes these keys and completes/cancels its composition. The
// Server must reset every backend without producing a reply.
constexpr bool IsBackendIndependentCompositionResetKey(uint32_t keycode)
{
    return keycode == kVirtualKeyShift || keycode == kVirtualKeyEscape || keycode == kVirtualKeyLeftShift ||
           keycode == kVirtualKeyRightShift;
}

constexpr bool ShouldResetCompositionForImeMode(bool chinese_mode)
{
    return !chinese_mode;
}

// Enter commits the raw composition instead of choosing a special-mode
// candidate. Therefore a Shift+letter wake key must not by itself prevent an
// otherwise non-pinyin English word (for example "Metasequoia") from being
// learned. The database layer still validates the final string.
constexpr bool ShouldLearnEnteredEnglishWord(bool dedicated_english_mode, bool shift_letter_special_mode,
                                             bool chinese_scheme, bool all_complete_pinyin)
{
    return dedicated_english_mode || shift_letter_special_mode || (chinese_scheme && !all_complete_pinyin);
}

constexpr bool InputSessionMatchesConfig(bool configured_scheme_matches, bool temporary_r_mode_active,
                                         bool session_is_japanese)
{
    return configured_scheme_matches || (temporary_r_mode_active && session_is_japanese);
}

constexpr bool ShouldSendCompositionReply(bool is_alpha_key, bool is_manual_pinyin_separator,
                                          bool is_microsoft_shuangpin_ing_key, bool is_unicode_hex_digit,
                                          bool is_unicode_plus, bool is_japanese_long_vowel)
{
    return is_alpha_key || is_manual_pinyin_separator || is_microsoft_shuangpin_ing_key || is_unicode_hex_digit ||
           is_unicode_plus || is_japanese_long_vowel;
}

// The Backspace that would delete the last remaining pinyin character of an
// in-progress word retracts the last selected segment instead of deleting the
// character and dropping the whole composition. Retraction needs a caret that
// could actually delete the character -- which is the caret sitting at the end
// of a spelling holding at most one character, including the spelling a
// segment Backspace already emptied -- a snapshot to restore, and a client that
// negotiated the CompositionRestore capability: UILess hosts draw their own
// candidate UI, and a DLL without the capability treats the reply as a
// transport fault rather than ignoring it.
constexpr bool ShouldRetreatCreatingWordSelection(bool creating_word_active, bool ui_less, bool client_supports_restore,
                                                  std::size_t raw_length, std::size_t caret_position,
                                                  std::size_t selection_history_size)
{
    return creating_word_active && !ui_less && client_supports_restore && raw_length <= 1 &&
           caret_position == raw_length && selection_history_size > 0;
}

// Ctrl+Backspace inside a composition deletes one segmentation unit instead of
// one character. Only the bare Ctrl chord is the IME's: Shift, Alt and the
// Windows keys keep their host meaning (PRD R1).
constexpr bool IsSegmentBackspaceKey(uint32_t keycode, uint32_t modifiers_down)
{
    return keycode == kVirtualKeyBackspace && (modifiers_down & kKeyModifierMask) == kModifierControl;
}

// A segment Backspace with nothing left before the caret deletes the last
// selected segment of the word being created: the accumulated word returns to
// its pre-selection state and the spelling that segment consumed is discarded
// rather than restored (PRD R3). Like the retraction above it needs a reply the
// client can apply, because TSF cannot mirror the deletion locally.
constexpr bool ShouldDropCreatingWordSegment(bool creating_word_active, bool ui_less, bool client_supports_restore,
                                             std::size_t caret_position, std::size_t selection_history_size)
{
    return creating_word_active && !ui_less && client_supports_restore && caret_position == 0 &&
           selection_history_size > 0;
}

// The offset a unit deletion starts from: the greatest boundary strictly before
// the caret, or `caret` itself when no unit boundary precedes it. Boundaries are
// raw offsets in ascending order. A result equal to `caret` means "no unit here"
// and the caller falls back to deleting one character.
inline std::size_t PreviousSegmentBoundary(const std::vector<std::size_t> &boundaries, std::size_t caret)
{
    std::size_t result = caret;
    for (const std::size_t boundary : boundaries)
    {
        if (boundary >= caret)
        {
            break;
        }
        result = boundary;
    }
    return result;
}

// After erasing [start, caret), the truncation point can sit next to a
// separator that no longer separates anything: "ni'hao" - "hao" leaves "ni'",
// "ni'hao'ma" - "hao" leaves "ni''ma". Drop exactly the one dangling
// separator so no empty segment or doubled delimiter survives the deletion.
inline void DropDanglingSegmentDelimiter(std::string &raw, std::size_t start)
{
    if (start < raw.size() && raw[start] == '\'' && (start == 0 || raw[start - 1] == '\''))
    {
        raw.erase(start, 1);
    }
    else if (start == raw.size() && start > 0 && raw[start - 1] == '\'')
    {
        raw.erase(start - 1, 1);
    }
}
} // namespace FanyImeIpc
