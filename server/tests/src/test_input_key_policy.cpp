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

TEST_CASE(segment_caret_move_is_ctrl_only)
{
    using FanyImeIpc::IsSegmentCaretKey;
    REQUIRE(IsSegmentCaretKey(FanyImeIpc::kVirtualKeyLeft, FanyImeIpc::kModifierControl));
    REQUIRE(IsSegmentCaretKey(FanyImeIpc::kVirtualKeyRight, FanyImeIpc::kModifierControl));
    // Shift, Alt, the Windows keys and any extra modifier keep the host meaning.
    for (const unsigned extra : {FanyImeIpc::kModifierShift, FanyImeIpc::kModifierAlt})
    {
        REQUIRE(!IsSegmentCaretKey(FanyImeIpc::kVirtualKeyLeft, FanyImeIpc::kModifierControl | extra));
        REQUIRE(!IsSegmentCaretKey(FanyImeIpc::kVirtualKeyRight, FanyImeIpc::kModifierControl | extra));
    }
    REQUIRE(!IsSegmentCaretKey(FanyImeIpc::kVirtualKeyLeft, 0));
    REQUIRE(!IsSegmentCaretKey(FanyImeIpc::kVirtualKeyRight, 0));
    REQUIRE(!IsSegmentCaretKey(FanyImeIpc::kVirtualKeyLeft, FanyImeIpc::kModifierUiLess));
    REQUIRE(!IsSegmentCaretKey(FanyImeIpc::kVirtualKeyBackspace, FanyImeIpc::kModifierControl));
    REQUIRE(!IsSegmentCaretKey('A', FanyImeIpc::kModifierControl));
}

TEST_CASE(segment_caret_boundaries_stop_at_the_unit_next_to_the_caret)
{
    using FanyImeIpc::NextSegmentBoundary;
    using FanyImeIpc::PreviousSegmentBoundary;
    const std::vector<std::size_t> boundaries = {0, 3, 7, 9};
    // Inside a unit, on its first offset, on its last offset and past the end:
    // left lands on the unit start, right on the unit end, and both directions
    // are idempotent at the raw ends.
    struct Case
    {
        std::size_t caret;
        std::size_t previous;
        std::size_t next;
    };
    const Case cases[] = {
        {8, 7, 9}, // inside the last unit
        {7, 3, 9}, // first offset of the last unit
        {9, 7, 9}, // raw end: both directions stay put
        {1, 0, 3}, // inside the first unit
        {0, 0, 3}, // raw start: left stays put
        {4, 3, 7}, // inside the middle unit
    };
    for (const Case &expected : cases)
    {
        REQUIRE_EQ(PreviousSegmentBoundary(boundaries, expected.caret), expected.previous);
        REQUIRE_EQ(NextSegmentBoundary(boundaries, expected.caret), expected.next);
    }
    // No unit model: both directions keep the caret where the caller had it and
    // the caller falls back to the single-character move.
    REQUIRE_EQ(PreviousSegmentBoundary({}, 4), std::size_t(4));
    REQUIRE_EQ(NextSegmentBoundary({}, 4), std::size_t(4));
    // An incomplete tail is a unit of its own, so the jump stops inside the raw.
    const std::vector<std::size_t> partial = {0, 2, 3};
    REQUIRE_EQ(PreviousSegmentBoundary(partial, 3), std::size_t(2));
    REQUIRE_EQ(NextSegmentBoundary(partial, 2), std::size_t(3));
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

TEST_CASE(caret_resegmentation_requires_negotiated_non_uiless_pinyin_composition)
{
    using FanyImeIpc::ShouldResegmentCompositionByCaret;
    // R10/AC8：协商过 CompositionRestore 的非 UILess 客户端才启用前缀重算。
    REQUIRE(ShouldResegmentCompositionByCaret(true, false, false, false));
    // 未协商（旧 DLL 组合）：一切照旧。
    REQUIRE(!ShouldResegmentCompositionByCaret(false, false, false, false));
    // UILess 宿主：候选窗由宿主自绘，回退路径不得变坏。
    REQUIRE(!ShouldResegmentCompositionByCaret(true, true, false, false));
    // 专用英文模式：光标仍是显示层插入点。
    REQUIRE(!ShouldResegmentCompositionByCaret(true, false, true, false));
    // K/U/T/E/M/J/Y 等特殊模式组合：无单元模型语义，不重算。
    REQUIRE(!ShouldResegmentCompositionByCaret(true, false, false, true));
}

TEST_CASE(caret_prefix_empty_requires_non_empty_raw_and_zero_prefix)
{
    using FanyImeIpc::IsCaretPrefixEmpty;
    // R4：光标在串首（量化后前缀为空）：无候选，候选窗隐藏。
    REQUIRE(IsCaretPrefixEmpty(0, 14));
    // 前缀非空：不算空。
    REQUIRE(!IsCaretPrefixEmpty(2, 14));
    // 整串解码（caret 未设置）：prefix_end == 串长，永不判空。
    REQUIRE(!IsCaretPrefixEmpty(14, 14));
    // raw 为空的组合：不属于 R4（避免把空组合误判成「前缀为空」）。
    REQUIRE(!IsCaretPrefixEmpty(0, 0));
}

TEST_CASE(caret_arrow_candidate_publish_rebuilds_from_engine_at_both_prefix_and_tail)
{
    using FanyImeIpc::CaretArrowCandidatePublish;
    using FanyImeIpc::ResolveCaretArrowCandidatePublish;
    // R4：前缀为空（raw 非空）——收起候选窗，与门控无关。
    REQUIRE_EQ(ResolveCaretArrowCandidatePublish(true, 0, 14), CaretArrowCandidatePublish::Hide);
    REQUIRE_EQ(ResolveCaretArrowCandidatePublish(false, 0, 14), CaretArrowCandidatePublish::Hide);
    // 门控开 × 前缀中间：按前缀候选重建页面。
    REQUIRE_EQ(ResolveCaretArrowCandidatePublish(true, 2, 14), CaretArrowCandidatePublish::RebuildFromEngine);
    // 门控开 × 回到串尾：引擎已按整串重算，页面必须从引擎重读重建（真机回归修复点）。
    REQUIRE_EQ(ResolveCaretArrowCandidatePublish(true, 14, 14), CaretArrowCandidatePublish::RebuildFromEngine);
    // 门控关（未协商/UILess/专用英文/特殊模式）：光标从不进会话，维持只刷新页面（AC8）。
    REQUIRE_EQ(ResolveCaretArrowCandidatePublish(false, 2, 14), CaretArrowCandidatePublish::RefreshPageOnly);
    REQUIRE_EQ(ResolveCaretArrowCandidatePublish(false, 14, 14), CaretArrowCandidatePublish::RefreshPageOnly);
    // 门控开但 raw 为空（仅剩已选汉字的中间态）：没有候选内容可重建，保持只刷新。
    REQUIRE_EQ(ResolveCaretArrowCandidatePublish(true, 0, 0), CaretArrowCandidatePublish::RefreshPageOnly);
    REQUIRE_EQ(ResolveCaretArrowCandidatePublish(false, 0, 0), CaretArrowCandidatePublish::RefreshPageOnly);
}

TEST_CASE(create_word_frame_carries_caret_only_for_negotiated_mid_string_caret)
{
    using FanyImeIpc::ShouldCreateWordFrameCarryCaret;
    // R5/AC2：协商侧前缀选词结算后光标归后缀首（0），必须携带让 DLL 镜到后缀首。
    REQUIRE(ShouldCreateWordFrameCarryCaret(true, 0, 14));
    // 协商侧光标在剩余 raw 中间：同样携带。
    REQUIRE(ShouldCreateWordFrameCarryCaret(true, 3, 14));
    // 协商侧串尾造词流：光标恒在末尾，省略字段与现状字节一致。
    REQUIRE(!ShouldCreateWordFrameCarryCaret(true, 14, 14));
    // 空 raw：无位置可表达，也不带。
    REQUIRE(!ShouldCreateWordFrameCarryCaret(true, 0, 0));
    // AC8 回归锚：未协商（旧 DLL）时无条件回 plain 3 字段帧——改动前这里光标
    // 在中间会误追加第 4 字段，旧解析器把尾部当 display_preedit。
    REQUIRE(!ShouldCreateWordFrameCarryCaret(false, 0, 14));
    REQUIRE(!ShouldCreateWordFrameCarryCaret(false, 3, 14));
    REQUIRE(!ShouldCreateWordFrameCarryCaret(false, 14, 14));
TEST_CASE(wubi_unique_four_code_commit_is_unconditional_and_guards_its_preconditions)
{
    using FanyImeIpc::ShouldAutoCommitCompleteWubiCode;
    // A unique complete four-letter code commits on the fourth key without any setting.
    REQUIRE(ShouldAutoCommitCompleteWubiCode(true, false));
    // The engine did not report a complete unique four-letter code.
    REQUIRE(!ShouldAutoCommitCompleteWubiCode(false, false));
    // A word is being created: the raw is a prefix, so the composition stays open.
    REQUIRE(!ShouldAutoCommitCompleteWubiCode(true, true));
}

TEST_CASE(wubi_top_word_commit_ignores_the_setting_and_guards_its_preconditions)
{
    using FanyImeIpc::ShouldCommitCompleteWubiCodeOnNextKey;
    REQUIRE(ShouldCommitCompleteWubiCodeOnNextKey(true, true, true, false));
    // Not a complete table-answered code: nothing to commit, the key belongs to the composition.
    REQUIRE(!ShouldCommitCompleteWubiCodeOnNextKey(false, true, true, false));
    // Not a letter key (Backspace, arrows, space): those edit or commit the code in place.
    REQUIRE(!ShouldCommitCompleteWubiCodeOnNextKey(true, false, true, false));
    // The caret is inside the code, so the user is editing it, not typing past it.
    REQUIRE(!ShouldCommitCompleteWubiCodeOnNextKey(true, true, false, false));
    // A word being created owns the raw as a prefix; committing it would end the word early.
    REQUIRE(!ShouldCommitCompleteWubiCodeOnNextKey(true, true, true, true));
}
