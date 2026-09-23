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
inline constexpr uint32_t kVirtualKeyLeft = 0x25;
inline constexpr uint32_t kVirtualKeyRight = 0x27;
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

// A complete four-letter wubi code the table answered with exactly one candidate is committed as
// soon as the fourth letter lands, so the user never has to press space. This is unconditional:
// there is no user setting for it (industry wubi IMEs default this on). The wubi engine's own
// report that the code is complete, table-answered and unique is the only gate. A word being
// created keeps the composition open: the raw belongs to the prefix the user is still assembling,
// and committing it would end that word early.
constexpr bool ShouldAutoCommitCompleteWubiCode(bool unique_four_code, bool creating_word_active)
{
    return unique_four_code && !creating_word_active;
}

// The user is typing past a complete four-letter wubi code (a letter key with the caret at the end
// of a four-letter table-answered code). The first candidate is committed and the key that was just
// typed starts the next composition instead of being dropped. This one is deliberately not gated by
// the setting: the setting decides whether a unique code commits without an extra key, never whether
// an extra key loses input. Committing the first candidate matches the user, who is already typing
// the next word and is not looking at the candidate window.
constexpr bool ShouldCommitCompleteWubiCodeOnNextKey(bool four_code_is_complete, bool key_is_letter, bool caret_at_end,
                                                     bool creating_word_active)
{
    return four_code_is_complete && key_is_letter && caret_at_end && !creating_word_active;
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

// Ctrl+Left / Ctrl+Right move the caret by the same segmentation unit that
// Ctrl+Backspace deletes. They mirror IsSegmentBackspaceKey: only the bare Ctrl
// chord is the IME's, so Shift, Alt and the Windows keys keep their host
// meaning (PRD R1). The unit model itself is the engine's and stays Server-side.
constexpr bool IsSegmentCaretKey(uint32_t keycode, uint32_t modifiers_down)
{
    return (keycode == kVirtualKeyLeft || keycode == kVirtualKeyRight) &&
           (modifiers_down & kKeyModifierMask) == kModifierControl;
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

// The offset a unit deletion starts from and a unit jump to the left lands on:
// the greatest boundary strictly before the caret, or `caret` itself when no
// unit boundary precedes it. Boundaries are raw offsets in ascending order. A
// result equal to `caret` means "no unit here": the caller then falls back to
// deleting / moving one character.
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

// The offset a unit jump to the right lands on: the smallest boundary strictly
// after the caret, or `caret` itself when no unit boundary follows it. A result
// equal to `caret` means the caret already sits at the end of the last unit and
// the caller must not move it.
inline std::size_t NextSegmentBoundary(const std::vector<std::size_t> &boundaries, std::size_t caret)
{
    for (const std::size_t boundary : boundaries)
    {
        if (boundary > caret)
        {
            return boundary;
        }
    }
    return caret;
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
