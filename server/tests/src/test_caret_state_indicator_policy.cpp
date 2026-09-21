#include "tests/includes/test_framework.h"
#include "window/caret_state_indicator_policy.h"

TEST_CASE(caret_state_indicator_visibility_is_complementary)
{
    REQUIRE(FanyImeUi::ShouldShowCaretStateIndicator(true, false, true, 50, 100));
    REQUIRE(!FanyImeUi::ShouldShowCaretStateIndicator(false, false, true, 50, 100));
    REQUIRE(!FanyImeUi::ShouldShowCaretStateIndicator(true, true, true, 50, 100));
    REQUIRE(!FanyImeUi::ShouldShowCaretStateIndicator(true, false, false, 50, 100));
    REQUIRE(!FanyImeUi::ShouldShowCaretStateIndicator(true, false, true, 0, 0));
    REQUIRE(!FanyImeUi::ShouldShowCaretStateIndicator(true, false, true, 50, -10000));
    REQUIRE(!FanyImeUi::ShouldShowCaretStateIndicator(true, false, true, 50, -100000));
}

TEST_CASE(caret_state_indicator_upper_positions_clear_the_caret_line)
{
    REQUIRE_EQ(FanyImeUi::CaretStateIndicatorY(false, 200, 30, 24, 6), 140);
    REQUIRE_EQ(FanyImeUi::CaretStateIndicatorY(false, 400, 60, 48, 12), 280);
    REQUIRE_EQ(FanyImeUi::CaretStateIndicatorY(true, 200, 30, 24, 6), 206);
    REQUIRE_EQ(FanyImeUi::CaretStateIndicatorY(true, 200, 30, 200, 6), 206);
}

TEST_CASE(caret_state_indicator_flips_away_from_work_area_and_suppresses_when_neither_side_fits)
{
    using FanyImeUi::CaretStateIndicatorPlacementY;
    REQUIRE_EQ(*CaretStateIndicatorPlacementY(true, 170, 30, 24, 6, 0, 200), 110);
    REQUIRE_EQ(*CaretStateIndicatorPlacementY(false, 30, 30, 24, 6, 0, 200), 36);
    REQUIRE(!CaretStateIndicatorPlacementY(true, 50, 80, 24, 6, 0, 100));
    REQUIRE(!CaretStateIndicatorPlacementY(false, 50, 80, 24, 6, 0, 100));
}

TEST_CASE(caret_state_indicator_horizontal_positions_follow_badge_width)
{
    REQUIRE_EQ(FanyImeUi::kCaretStatePunctuationSlotWidthDip, 64);
    REQUIRE_EQ(FanyImeUi::kCaretStatePunctuationModeGapDip, 0);
    REQUIRE_EQ(FanyImeUi::kCaretStatePunctuationModeSlotWidthDip, 30);
    REQUIRE_EQ(FanyImeUi::kCaretStatePunctuationBadgeWidthDip, 94);
    constexpr int punctuationBadgeWidth = FanyImeUi::kCaretStatePunctuationBadgeWidthDip;
    REQUIRE_EQ(FanyImeUi::CaretStateIndicatorX("top-left", 200, punctuationBadgeWidth, 6), 100);
    REQUIRE_EQ(FanyImeUi::CaretStateIndicatorX("top", 200, punctuationBadgeWidth, 6), 153);
    REQUIRE_EQ(FanyImeUi::CaretStateIndicatorX("top-right", 200, punctuationBadgeWidth, 6), 206);
    REQUIRE_EQ(FanyImeUi::CaretStateIndicatorX("bottom", 200, punctuationBadgeWidth, 6), 100);
}

TEST_CASE(caret_state_indicator_single_glyph_is_square)
{
    constexpr int height = 30;
    REQUIRE_EQ(FanyImeUi::CaretStateIndicatorTextWidth(height, 20, 0), height);
    REQUIRE_EQ(FanyImeUi::CaretStateIndicatorTextWidth(height, 20, 2), 70);
}

TEST_CASE(caret_state_indicator_maps_input_mode_to_one_glyph)
{
    using FanyImeUi::InputModeGlyph;
    REQUIRE_EQ(InputModeGlyph(true, false), L'中');
    REQUIRE_EQ(InputModeGlyph(false, false), L'英');
    REQUIRE_EQ(InputModeGlyph(true, true), L'日');
    REQUIRE_EQ(FanyImeUi::InputModeEventGlyph(true, false, true), L'英');
    REQUIRE_EQ(FanyImeUi::InputModeEventGlyph(false, false, true), L'英');
    REQUIRE_EQ(FanyImeUi::InputModeEventGlyph(true, true, false), L'日');
}

TEST_CASE(caret_state_indicator_caps_lock_masks_ordinary_ime_switches_only)
{
    using FanyImeUi::ShouldShowInputModeEvent;
    REQUIRE(ShouldShowInputModeEvent(true, true));
    REQUIRE(ShouldShowInputModeEvent(true, false));
    REQUIRE(ShouldShowInputModeEvent(false, false));
    REQUIRE(!ShouldShowInputModeEvent(false, true));
    // Punctuation mapping still reflects the authoritative IME mode, even
    // when Caps Lock changes the handling of fresh alphabetic input.
    REQUIRE_EQ(FanyImeUi::PunctuationInputModeText(true, true, false), L"，。  中");
}

TEST_CASE(caret_state_indicator_combines_punctuation_and_input_mode)
{
    using FanyImeUi::PunctuationInputModeText;
    REQUIRE_EQ(PunctuationInputModeText(true, true, false), L"，。  中");
    REQUIRE_EQ(PunctuationInputModeText(false, true, false), L",.  中");
    REQUIRE_EQ(PunctuationInputModeText(true, false, false), L"，。  英");
    REQUIRE_EQ(PunctuationInputModeText(false, false, false), L",.  英");
    REQUIRE_EQ(PunctuationInputModeText(true, true, true), L"，。  日");
    REQUIRE_EQ(PunctuationInputModeText(false, true, true), L",.  日");
    REQUIRE_EQ(PunctuationInputModeText(true, true, false).size(), PunctuationInputModeText(false, true, false).size());
}

TEST_CASE(caret_state_indicator_maps_single_state_switches_to_one_glyph)
{
    using FanyImeUi::CaretStateGlyph;
    using FanyImeUi::CaretStateKind;
    REQUIRE_EQ(CaretStateGlyph(CaretStateKind::Width, true), L'全');
    REQUIRE_EQ(CaretStateGlyph(CaretStateKind::Width, false), L'半');
    REQUIRE_EQ(CaretStateGlyph(CaretStateKind::CharacterSet, false), L'简');
    REQUIRE_EQ(CaretStateGlyph(CaretStateKind::CharacterSet, true), L'繁');
}
