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
//   1. Exact SQLite full-key hits (CandidateSource::Database / UserDatabase)
//   2. Lattice full-cover sentences (CandidateSource::Generated)
//   3. Google-pinyin Fallback, prefixes, and other remaining items
// Lattice never displaces a leading exact Database/UserDatabase full-cover
// (e.g. 高碳钢 for gktjgh). Fallback must not block lattice (e.g. 高碳钢镊子
// ahead of 高谈刚捏子).
//
// merge_lattice_candidates only runs at 3+ complete syllables. One- and
// two-syllable keys are already covered by exact SQLite lookup. Abbreviated
// quanpin segments (g'k't) are rejected so WordItem.canonical_pinyin stays a
// complete pronunciation.
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
    // Cap on lexemes per span (injected lookups). DB lookup already applies
    // the same cap in query_segments_keyed_flat.
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
    // Only used when language_model is unusable. Heuristic unigram normalizer
    // vs phrase-length bonus; single-char msime.db weights are corpus counts
    // while phrase weights are a smaller scale. Never calibrated.
    double unigram_z = 1e6;
    double phrase_length_bonus = 3.0;
};

using WordLatticeLookup = std::function<std::vector<LatticeLexeme>(const Segments &span)>;

std::vector<LatticePath> decode_word_lattice(const Segments &syllables, const WordLatticeLookup &lookup,
                                             const WordLatticeOptions &options = {});

void merge_lattice_candidates(std::vector<WordItem> &candidates, const Segments &syllables,
                              const WordLatticeLookup &lookup, const std::string &typed_pinyin,
                              const WordLatticeOptions &options = {});

} // namespace quanpin
