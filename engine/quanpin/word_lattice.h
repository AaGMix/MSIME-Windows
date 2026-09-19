#pragma once

#include "../core/word_item.h"
#include "engine/ngram/language_model.h"
#include "quanpin_utils.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace quanpin
{

// Phrase-graph + Viterbi beam search over dictionary spans.
// The graph (which words can cover which syllable spans) comes from the
// dictionary; the path score comes from the kenlm trigram in
// engine/ngram. Each hypothesis carries the language model state, so an edge is
// scored as log10 P(word | previous two words) exactly like libime's decoder.
// Dictionary weight only survives as a bounded tiebreak: it decides the order of
// words the language model cannot distinguish (both unseen), and cannot
// outweigh a real n-gram difference.
//
// Without a model (sc.lm missing or unreadable) the decoder falls back to the
// previous uncalibrated unigram heuristic so the IME still produces sentences.
//
// Ranking when merging into an existing candidate list:
//   1. SQLite rows whose key equals the typed syllables exactly
//      (CandidateSource::Database / UserDatabase)
//   2. Lattice full-cover sentences (CandidateSource::Generated)
//   3. Google-pinyin Fallback, prefix-range rows, and everything else
// Lattice never displaces the leading run of exact-key hits (e.g. 高碳钢 for
// gktjgh). Fallback must not block lattice (e.g.
// 高碳钢镊子 ahead of 高谈刚捏子). Callers must not reorder these two
// afterwards: the dictionary layer used to hoist Fallback back to the front,
// which silently inverted the ranking.
// test_quanpin_lattice_precedes_google_fallback guards the end result.
//
// Both sentence sources share generated_sentence_insert_position, so the
// dictionary layer inserts the Google fallback at the same spot instead of at
// the head of the list. A whole-sentence guess must never outrank a phrase the
// dictionary actually has under the typed key.
//
// merge_lattice_candidates only runs at 2+ complete syllables. Single-syllable
// keys are fully covered by exact SQLite lookup. Abbreviated quanpin segments
// (g'k't) are rejected so WordItem.canonical_pinyin stays a complete
// pronunciation.
//
// Lattice WordItem.weight is log_prob * 1000 and is often negative. List
// order is the ranking; do not sort these rows by weight.

struct LatticeLexeme
{
    std::string key;
    std::string value;
    std::int64_t weight = 0;
};

struct LatticePath
{
    std::string sentence;
    std::string key;
    double log_prob = 0;
    std::vector<std::string> words;
};

struct WordLatticeOptions
{
    int beam = 32;
    int nbest = 5;
    // Cap on lexemes per span (injected lookups). The DB lookup applies the
    // same cap itself, inside query_exact_segmentations_keyed_flat.
    int span_limit = 32;
    int max_phrase_syllables = 7;
    // The trigram that scores paths. Null (or an unloadable model) selects the
    // heuristic below. Borrowed, not owned; must outlive the call.
    const ngram::LanguageModel *language_model = nullptr;
    // Weight of the dictionary tiebreak, in log10 units per decade of
    // msime.db weight. Deliberately small: the largest plausible weight spread
    // is ~7 decades, so the tiebreak tops out around 0.07 and can only reorder
    // words the model scores identically (in practice, two unknown words that
    // both took the -7.78 penalty).
    double dictionary_tiebreak = 0.01;
    // 生僻读音惩罚的权重（log10 单位）。语言模型只看汉字，分不出「卷」读 gun、
    // 「而」读 neng 这种词库里权重为 0 的多音字读音，于是 gun'qi 出「卷七」、
    // neng'fa'sheng 出「而发生」。每一行按「自己的权重 / 该拼音跨度的权重和」
    // 算读音占比，占比低于 reading_prior_share 的部分按 log10 扣分。0 表示关闭。
    double reading_prior = 1.0;
    // 免罚线：占比高于它的读音一分不扣。不能整体按 log10 P(词|拼音) 扣，那等于
    // 把字频算两遍（语言模型已经算过一次），「跑得很快」会被「跑的很快」压掉——
    // 得/地 各占 de 的 7%~9%，本来就是常见读音。真正的生僻读音占比是 1e-5 量级，
    // 离这条线还差好几个数量级。
    double reading_prior_share = 0.01;
    // 惩罚下限（正数，实际按 -reading_prior_floor 截断）。权重 0 的行不至于被判
    // 无穷小，语言模型仍有翻盘余地。
    double reading_prior_floor = 5.0;
    // Only used when language_model is unusable. Heuristic unigram normalizer
    // vs phrase-length bonus; single-char msime.db weights are corpus counts
    // while phrase weights are a smaller scale. Never calibrated.
    double unigram_z = 1e6;
    double phrase_length_bonus = 3.0;
};

using WordLatticeLookup = std::function<std::vector<LatticeLexeme>(const Segments &span)>;

// Index where a whole-sentence candidate belongs: just past the leading run of
// Database/UserDatabase hits whose key equals the typed syllables exactly.
// Prefix-range and fuzzy rows are not exact hits even when they happen to have
// the same syllable count (gun'qi scans up 滚球 = gun'qiu), so they rank below
// the sentence. Shared by the lattice merge and by the dictionary layer's
// Google fallback so both land in the same place.
size_t generated_sentence_insert_position(const std::vector<WordItem> &candidates, const Segments &syllables);

std::vector<LatticePath> decode_word_lattice(const Segments &syllables, const WordLatticeLookup &lookup,
                                             const WordLatticeOptions &options = {});

void merge_lattice_candidates(std::vector<WordItem> &candidates, const Segments &syllables,
                              const WordLatticeLookup &lookup, const std::string &typed_pinyin,
                              const WordLatticeOptions &options = {});

} // namespace quanpin
