#include "global/globals.h"
#include "tests/includes/test_framework.h"

namespace
{
GlobalIme::CreatingWordState MakeCreatingWord(const char *pinyin, const char *word)
{
    GlobalIme::CreatingWordState state;
    state.pinyin = pinyin;
    state.word = word;
    state.active = true;
    return state;
}
} // namespace

TEST_CASE(composition_selection_snapshots_restore_raw_and_previous_word)
{
    GlobalIme::CompositionState composition;

    // First selection ("te" -> 特) has nothing accumulated before it.
    composition.push_selection_snapshot("te");
    REQUIRE_EQ(composition.selection_history.size(), std::size_t(1));

    // Second selection ("le" -> 乐) has the earlier 特 already accumulated.
    composition.creating_word = MakeCreatingWord("te", "特");
    composition.push_selection_snapshot("le");
    REQUIRE_EQ(composition.selection_history.size(), std::size_t(2));

    // Retracting "le" returns to candidates for "le" with the earlier 特 kept.
    REQUIRE(composition.restore_last_selection());
    REQUIRE_EQ(composition.raw_input_with_cases, std::string("le"));
    REQUIRE_EQ(composition.caret_position, std::size_t(2));
    REQUIRE(composition.creating_word.active);
    REQUIRE_EQ(composition.creating_word.word, std::string("特"));
    REQUIRE_EQ(composition.creating_word.pinyin, std::string("te"));

    // Retracting "te" leaves a plain pinyin composition with no accumulated word.
    REQUIRE(composition.restore_last_selection());
    REQUIRE_EQ(composition.raw_input_with_cases, std::string("te"));
    REQUIRE_EQ(composition.caret_position, std::size_t(2));
    REQUIRE(!composition.creating_word.active);
    REQUIRE(composition.creating_word.word.empty());
    REQUIRE(composition.creating_word.pinyin.empty());

    // Nothing left to retract.
    REQUIRE(!composition.restore_last_selection());
}

TEST_CASE(composition_selection_snapshot_requires_a_consumed_spelling)
{
    GlobalIme::CompositionState composition;
    composition.push_selection_snapshot("");
    REQUIRE(composition.selection_history.empty());
    REQUIRE(!composition.restore_last_selection());
}

TEST_CASE(composition_clear_drops_history_while_clear_creating_word_keeps_it)
{
    GlobalIme::CompositionState composition;
    composition.push_selection_snapshot("te");

    // Finishing a word inside a still-running composition keeps the history.
    composition.clear_creating_word();
    REQUIRE_EQ(composition.selection_history.size(), std::size_t(1));

    // Ending the composition drops everything a later retraction could misuse.
    composition.clear();
    REQUIRE(composition.selection_history.empty());
    REQUIRE(!composition.restore_last_selection());
}
