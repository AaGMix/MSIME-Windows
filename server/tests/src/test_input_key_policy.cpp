#include "ipc/input_key_policy.h"
#include "tests/includes/test_framework.h"

TEST_CASE(word_to_character_uses_only_the_selected_unmodified_key_pair)
{
    using FanyImeIpc::WordToCharacterDirection;
    REQUIRE_EQ(WordToCharacterDirection(0xDB, '[', 0, true, false), -1);
    REQUIRE_EQ(WordToCharacterDirection(0xDD, ']', 0, true, false), 1);
    REQUIRE_EQ(WordToCharacterDirection(0xBD, '-', 0, true, true), -1);
    REQUIRE_EQ(WordToCharacterDirection(0xBB, '=', 0, true, true), 1);
    REQUIRE_EQ(WordToCharacterDirection(0xDB, '[', 0, true, true), 0);
    REQUIRE_EQ(WordToCharacterDirection(0xBD, '-', 0, true, false), 0);
    REQUIRE_EQ(WordToCharacterDirection(0xBD, '-', 0, false, true), 0);
    REQUIRE_EQ(WordToCharacterDirection(0xDB, '[', 0, false, false), 0);
    // Unicode U+ entry and shifted punctuation must not select a character.
    REQUIRE_EQ(WordToCharacterDirection(0xBB, '+', 1, true, true), 0);
    REQUIRE_EQ(WordToCharacterDirection(0xBD, '_', 1, true, true), 0);
    REQUIRE_EQ(WordToCharacterDirection(0xDB, '{', 1, true, false), 0);
    for (unsigned modifiers = 1; modifiers < 8; ++modifiers)
        REQUIRE_EQ(WordToCharacterDirection(0xBB, '=', modifiers, true, true), 0);
    // Host-drawn candidates carry an unrelated UI-less flag.
    REQUIRE_EQ(WordToCharacterDirection(0xBB, '=', FanyImeIpc::kModifierUiLess, true, true), 1);
    REQUIRE_EQ(WordToCharacterDirection('A', '=', 0, true, true), 0);
}

TEST_CASE(shift_variants_are_backend_independent_composition_reset_keys)
{
    REQUIRE(FanyImeIpc::IsBackendIndependentCompositionResetKey(0x10));
    REQUIRE(FanyImeIpc::IsBackendIndependentCompositionResetKey(0x1B));
    REQUIRE(FanyImeIpc::IsBackendIndependentCompositionResetKey(0xA0));
    REQUIRE(FanyImeIpc::IsBackendIndependentCompositionResetKey(0xA1));

    REQUIRE(!FanyImeIpc::IsBackendIndependentCompositionResetKey(0));
    REQUIRE(!FanyImeIpc::IsBackendIndependentCompositionResetKey('A'));
    REQUIRE(!FanyImeIpc::IsBackendIndependentCompositionResetKey(0x0D));
    REQUIRE(!FanyImeIpc::IsBackendIndependentCompositionResetKey(0x11));
}

TEST_CASE(english_ime_status_requires_backend_independent_composition_reset)
{
    REQUIRE(FanyImeIpc::ShouldResetCompositionForImeMode(false));
    REQUIRE(!FanyImeIpc::ShouldResetCompositionForImeMode(true));
}

TEST_CASE(enter_english_learning_does_not_conflict_with_shift_letter_special_modes)
{
    REQUIRE(FanyImeIpc::ShouldLearnEnteredEnglishWord(false, false, true, false));
    REQUIRE(FanyImeIpc::ShouldLearnEnteredEnglishWord(true, false, true, true));
    // K/U/T/E/M/J/Y modes and the temporary R-mode session all use this path.
    REQUIRE(FanyImeIpc::ShouldLearnEnteredEnglishWord(false, true, true, true));
    REQUIRE(FanyImeIpc::ShouldLearnEnteredEnglishWord(false, true, false, true));
    REQUIRE(!FanyImeIpc::ShouldLearnEnteredEnglishWord(false, false, true, true));
    REQUIRE(!FanyImeIpc::ShouldLearnEnteredEnglishWord(false, false, false, false));
}

TEST_CASE(numpad_digits_are_normalized_to_candidate_digit_keys)
{
    REQUIRE(FanyImeIpc::NormalizeNumpadDigitKey(0x60) == '0');
    REQUIRE(FanyImeIpc::NormalizeNumpadDigitKey(0x61) == '1');
    REQUIRE(FanyImeIpc::NormalizeNumpadDigitKey(0x69) == '9');

    REQUIRE(FanyImeIpc::NormalizeNumpadDigitKey('1') == '1');
    REQUIRE(FanyImeIpc::NormalizeNumpadDigitKey(0x6A) == 0x6A);
}

TEST_CASE(english_mode_toggle_requires_ctrl_shift_e)
{
    REQUIRE(FanyImeIpc::IsEnglishModeToggleKey('E', 0b00000011u));
    REQUIRE(!FanyImeIpc::IsEnglishModeToggleKey('E', 0b00000111u));
    REQUIRE(!FanyImeIpc::IsEnglishModeToggleKey('E', 0b00000110u));
    REQUIRE(!FanyImeIpc::IsEnglishModeToggleKey('E', 0b00000001u));
    REQUIRE(!FanyImeIpc::IsEnglishModeToggleKey('A', 0b00000011u));
}

TEST_CASE(composition_reply_includes_microsoft_shuangpin_ing_key)
{
    REQUIRE(FanyImeIpc::ShouldSendCompositionReply(false, false, true, false, false, false));
    REQUIRE(FanyImeIpc::ShouldSendCompositionReply(true, false, false, false, false, false));
    REQUIRE(!FanyImeIpc::ShouldSendCompositionReply(false, false, false, false, false, false));
}

TEST_CASE(backspace_retracts_only_the_last_selected_segment_boundary)
{
    using FanyImeIpc::ShouldRetreatCreatingWordSelection;
    // The normal case: one character left, caret at the end, snapshot available,
    // and a client that negotiated the retraction reply.
    REQUIRE(ShouldRetreatCreatingWordSelection(true, false, true, 1, 1, 1));
    // A spelling already emptied by a Ctrl+Backspace segment deletion still owns
    // its snapshots, so the plain Backspace retracts the selected segment.
    REQUIRE(ShouldRetreatCreatingWordSelection(true, false, true, 0, 0, 1));
    // No active word, UILess host, old DLL, or no snapshot.
    REQUIRE(!ShouldRetreatCreatingWordSelection(false, false, true, 1, 1, 1));
    REQUIRE(!ShouldRetreatCreatingWordSelection(true, true, true, 1, 1, 1));
    REQUIRE(!ShouldRetreatCreatingWordSelection(true, false, false, 1, 1, 1));
    REQUIRE(!ShouldRetreatCreatingWordSelection(true, false, true, 1, 1, 0));
    // More than one character left: this Backspace only deletes a character.
    REQUIRE(!ShouldRetreatCreatingWordSelection(true, false, true, 2, 2, 3));
    // Caret at the start of the remaining input cannot delete the character.
    REQUIRE(!ShouldRetreatCreatingWordSelection(true, false, true, 1, 0, 1));
}

TEST_CASE(segment_backspace_is_ctrl_only)
{
    using FanyImeIpc::IsSegmentBackspaceKey;
    REQUIRE(IsSegmentBackspaceKey(FanyImeIpc::kVirtualKeyBackspace, FanyImeIpc::kModifierControl));
    // Shift, Alt, the Windows keys and any extra modifier keep the host meaning.
    REQUIRE(!IsSegmentBackspaceKey(FanyImeIpc::kVirtualKeyBackspace,
                                   FanyImeIpc::kModifierShift | FanyImeIpc::kModifierControl));
    REQUIRE(!IsSegmentBackspaceKey(FanyImeIpc::kVirtualKeyBackspace,
                                   FanyImeIpc::kModifierControl | FanyImeIpc::kModifierAlt));
    REQUIRE(!IsSegmentBackspaceKey(FanyImeIpc::kVirtualKeyBackspace, 0));
    REQUIRE(!IsSegmentBackspaceKey(FanyImeIpc::kVirtualKeyBackspace, FanyImeIpc::kModifierShift));
    REQUIRE(!IsSegmentBackspaceKey(FanyImeIpc::kVirtualKeyBackspace, FanyImeIpc::kModifierAlt));
    REQUIRE(!IsSegmentBackspaceKey(FanyImeIpc::kVirtualKeyBackspace, FanyImeIpc::kModifierUiLess));
    REQUIRE(!IsSegmentBackspaceKey('A', FanyImeIpc::kModifierControl));
}

TEST_CASE(segment_backspace_drops_a_selected_segment_only_at_the_head_of_the_raw)
{
    using FanyImeIpc::ShouldDropCreatingWordSegment;
    REQUIRE(ShouldDropCreatingWordSegment(true, false, true, 0, 1));
    // Nothing before the caret is not enough: the word must be active, the
    // client must be able to apply the restore reply, and a snapshot must exist.
    REQUIRE(!ShouldDropCreatingWordSegment(false, false, true, 0, 1));
    REQUIRE(!ShouldDropCreatingWordSegment(true, true, true, 0, 1));
    REQUIRE(!ShouldDropCreatingWordSegment(true, false, false, 0, 1));
    REQUIRE(!ShouldDropCreatingWordSegment(true, false, true, 0, 0));
    // Raw still in front of the caret: delete that unit instead of a segment.
    REQUIRE(!ShouldDropCreatingWordSegment(true, false, true, 1, 1));
}

TEST_CASE(previous_segment_boundary_stops_at_the_unit_before_the_caret)
{
    using FanyImeIpc::PreviousSegmentBoundary;
    const std::vector<std::size_t> boundaries = {0, 3, 7, 9};
    // End of the spelling deletes the last unit, a caret inside a unit deletes
    // only the part in front of it, and a caret on a boundary deletes the unit
    // before that boundary.
    REQUIRE_EQ(PreviousSegmentBoundary(boundaries, 9), std::size_t(7));
    REQUIRE_EQ(PreviousSegmentBoundary(boundaries, 8), std::size_t(7));
    REQUIRE_EQ(PreviousSegmentBoundary(boundaries, 7), std::size_t(3));
    REQUIRE_EQ(PreviousSegmentBoundary(boundaries, 4), std::size_t(3));
    REQUIRE_EQ(PreviousSegmentBoundary(boundaries, 3), std::size_t(0));
    REQUIRE_EQ(PreviousSegmentBoundary(boundaries, 1), std::size_t(0));
    // Nothing before the caret: the caller falls back to one character.
    REQUIRE_EQ(PreviousSegmentBoundary(boundaries, 0), std::size_t(0));
    REQUIRE_EQ(PreviousSegmentBoundary({}, 4), std::size_t(4));
}

TEST_CASE(segment_deletion_does_not_leave_a_dangling_delimiter)
{
    using FanyImeIpc::DropDanglingSegmentDelimiter;
    // "ni'hao" - "hao" leaves the delimiter of the deleted unit behind.
    std::string trailing = "ni'";
    DropDanglingSegmentDelimiter(trailing, trailing.size());
    REQUIRE_EQ(trailing, std::string("ni"));

    // "ni'hao'ma" - "hao" would otherwise leave two delimiters in a row.
    std::string doubled = "ni''ma";
    DropDanglingSegmentDelimiter(doubled, 3);
    REQUIRE_EQ(doubled, std::string("ni'ma"));

    // "ni'hao" - "ni'" leaves a leading delimiter, and an ordinary letter
    // boundary is left untouched.
    std::string leading = "'hao";
    DropDanglingSegmentDelimiter(leading, 0);
    REQUIRE_EQ(leading, std::string("hao"));
    std::string untouched = "ni'hao";
    DropDanglingSegmentDelimiter(untouched, 3);
    REQUIRE_EQ(untouched, std::string("ni'hao"));
}

TEST_CASE(composition_reply_includes_japanese_long_vowel_key)
{
    // 日语模式下 '-' 打长音符，必须回包刷新候选框。
    REQUIRE(FanyImeIpc::ShouldSendCompositionReply(false, false, false, false, false, true));
}

TEST_CASE(japanese_long_vowel_key_is_not_word_to_character_key)
{
    // 词转字用 -/= 时，日语模式的 '-' 已被长音符占用，不能再触发词转字。
    REQUIRE_EQ(FanyImeIpc::WordToCharacterDirection(0xBD, '-', 0, true, true), -1);
    REQUIRE_EQ(FanyImeIpc::WordToCharacterDirection(0xBD, '-', 0, false, true), 0);
}

TEST_CASE(temporary_r_mode_japanese_session_is_not_replaced_by_config_sync)
{
    REQUIRE(FanyImeIpc::InputSessionMatchesConfig(false, true, true));
    REQUIRE(FanyImeIpc::InputSessionMatchesConfig(true, false, false));
    REQUIRE(!FanyImeIpc::InputSessionMatchesConfig(false, false, true));
    REQUIRE(!FanyImeIpc::InputSessionMatchesConfig(false, true, false));
}
