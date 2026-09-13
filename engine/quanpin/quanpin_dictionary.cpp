#include "fuzzy_pinyin.h"
#include "quanpin_dictionary.h"
#include "../user_dictionary/user_dictionary_journal.h"

#include "../common/helpcode_utils.h"
#include "quanpin_query.h"
#include "quanpin_utils.h"
#include "../shuangpin/shuangpin_utils.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <fmt/format.h>
#include <unordered_set>
#include <utf8/cpp17.h>

namespace
{
constexpr size_t kSparsePinyinFallbackThreshold = 8;
constexpr size_t kSyllableGraphPathLimit = 32;
constexpr size_t kMaxSyllablesForMultipleSegmentations = 4;
constexpr int kAlternativeSegmentationCandidateLimit = 128;
constexpr size_t kBestAlternativeSegmentationMaxIndex = 1;

bool is_alpha_vk(ImeKeyCode vk)
{
    return vk >= 'A' && vk <= 'Z';
}

std::string remove_delimiters(const std::string &segmented)
{
    std::string normalized = segmented;
    normalized.erase(std::remove(normalized.begin(), normalized.end(), '\''), normalized.end());
    return normalized;
}

// 正字法别名归一：把非标准 ü 拼写改写成词库标准键，输出即进入查询管线的权威拼写
// （查库键、pinyin_segmentation_、调频/造词键因此全部落在标准拼法上）。标准拼音约定：
// ü 保留鱼眼的音节（n/l 系）键写 v（nv/nve/lv/lve），省略鱼眼的（j/q/x/y 系）键写 u
// （ju/jue/qu/.../yue）。nu/lu 是真实音节（怒/路），所以 n/l 系只按段精确匹配 nue/lue，
// 绝不改写 nv/lv/nu/lu。
quanpin::Segments normalize_umlaut_aliases(quanpin::Segments segments)
{
    for (auto &segment : segments)
    {
        if (segment.size() == 3 && segment[1] == 'u' && segment[2] == 'e' && (segment[0] == 'n' || segment[0] == 'l'))
        {
            // nue/lue → nve/lve：词库正键用 v，混拼 u 是它的别名。
            segment[1] = 'v';
        }
        else if (segment.size() >= 2 && segment[1] == 'v' &&
                 (segment[0] == 'j' || segment[0] == 'q' || segment[0] == 'x' || segment[0] == 'y'))
        {
            // j/q/x/y 系没有保留鱼眼的合法音节，v 一律是 u 的别名。
            segment[1] = 'u';
        }
    }
    return segments;
}

std::string series_cache_key(const std::string &raw_input, const std::string &segmentation)
{
    const char *prefix = raw_input.find('\'') == std::string::npos ? "A:" : "M:";
    return prefix + (segmentation.empty() ? raw_input : segmentation);
}

// Folds letters for autocorrect comparisons: lowercases and strips manual
// delimiters. v and u must compare as distinct letters — the ü-style alias
// rewrite is a first-class marking source (the trailing star badge), so
// 'nve' (primary segmentation) differing from typed 'nue' is exactly what
// produces the corrected_from label. The old contract ("aliases never look
// rewritten, hence never marked") was flipped by product decision.
std::string fold_autocorrect_letters(const std::string &text)
{
    std::string folded;
    folded.reserve(text.size());
    for (const char ch : text)
    {
        if (ch == '\'')
        {
            continue;
        }
        folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return folded;
}

// How many ranked correction cuts feed the query pipeline: the primary cut
// becomes the query key and the rest ride along as alternative segmentations
// for query-time disambiguation (patent CN 101133411 B M3). k=9 is set by the
// phase-4/5 evaluation data: dropped-initial keys such as uan have 10-15
// equally-weighted deletion targets (cuan/duan/guan/.../zuan), and k=3 cut the
// list by table order before reaching the correct reading (quan'li for uanli).
// k=9 lifts R@1/R@3 across the deletion and mixed models with no p95 change.
constexpr std::size_t kAutocorrectCutKBest = 9;

quanpin::Segments cut_syllables(const quanpin::AutocorrectCut &cut)
{
    quanpin::Segments syllables;
    syllables.reserve(cut.segments.size());
    for (const auto &segment : cut.segments)
    {
        syllables.push_back(segment.syllable);
    }
    return syllables;
}

SeriesQueryResolution resolve_series_query(const std::string &raw_input, const quanpin::Segments &segments,
                                           unsigned autocorrect_types)
{
    SeriesQueryResolution result;
    // Guard order matters: the jianpin-shape predicate runs before autocorrect_cut
    // so a guarded input never pays for the BFS. Both guards express "the user did
    // not mistype" and either one disables the rewrite entirely. The base
    // segmentation is deliberately NOT part of the gate: the BFS works on the raw
    // letters, so an empty base cut (nothing segmentable, e.g. a dropped initial
    // such as uanli for quan'li) is often exactly the input that needs correction.
    // With no correction reading the search returns nothing and the plain path is
    // kept unchanged.
    const bool eligible = autocorrect_types != 0 && raw_input.find('\'') == std::string::npos &&
                          !quanpin::has_only_complete_pinyin_segments(segments) &&
                          !quanpin::looks_like_syllable_with_jianpin_tail(raw_input);
    if (eligible)
    {
        // Fill corrected_segments and the two alternative tiers from cost-ranked
        // cuts, optionally appending a trailing jianpin segment (used by the
        // head-correction + jianpin-tail composition below). cuts are ranked
        // cheapest-first (edge count, then weight): the primary defines the cost
        // tier, same-cost readings frequency-compete with it, costlier readings
        // stay behind it (see the two vectors' contracts in the header).
        const auto populate_from_cuts = [&](const std::vector<quanpin::AutocorrectCut> &cuts,
                                            const std::string &jianpin_tail) {
            const auto to_segments = [&](const quanpin::AutocorrectCut &cut) {
                quanpin::Segments segs = cut_syllables(cut);
                if (!jianpin_tail.empty())
                {
                    segs.push_back(jianpin_tail);
                }
                return segs;
            };
            const auto &primary_cut = cuts.front();
            result.corrected_segments = to_segments(primary_cut);
            for (std::size_t i = 1; i < cuts.size(); ++i)
            {
                if (cuts[i].same_cost_as(primary_cut))
                {
                    result.alternative_corrected_cuts.push_back(to_segments(cuts[i]));
                }
                else
                {
                    result.costlier_corrected_cuts.push_back(to_segments(cuts[i]));
                }
            }
        };

        const auto cuts = quanpin::autocorrect_cut_kbest(raw_input, autocorrect_types, kAutocorrectCutKBest);
        if (!cuts.empty())
        {
            result.corrected_input = true;
            populate_from_cuts(cuts, "");
        }
        else
        {
            // Compose a correction with a trailing jianpin tail. The k-best search
            // only reaches the end when every segment is a complete syllable, so an
            // input like "hauzh" (hau typo + zh jianpin) or "hauz" fails outright.
            // Retry on the head with the trailing incomplete-syllable prefix removed;
            // query_series then handles the tail exactly as it does for the correctly
            // spelled "huazh" (hua + zh). Shortest tail first, so the correction
            // explains as much of the input as possible.
            //
            // Neighbor corrections are excluded from the head: they carry the widest
            // false-positive surface (largest table), and this path already relaxes
            // the "whole input is a legal cut" constraint by trusting a speculative
            // jianpin boundary. Stacking the two turns deletion-shaped input into
            // noise (e.g. "shng" -> "sun" + "g" -> 笋干). Structural typos
            // (transposition / deletion / insertion) are confident enough to compose.
            const unsigned head_types = autocorrect_types & ~quanpin::kAutocorrectNeighbor;
            const auto &prefixes = quanpin::prefix_pinyin_set();
            const auto &intact = quanpin::intact_pinyin_set();
            for (std::size_t tail_len = 1; head_types != 0 && tail_len <= 2 && tail_len < raw_input.size(); ++tail_len)
            {
                const std::string tail = raw_input.substr(raw_input.size() - tail_len);
                // A jianpin tail is an incomplete syllable: a valid pinyin prefix
                // that is not itself a complete syllable (e.g. "zh", "z", "h").
                if (prefixes.count(tail) == 0 || intact.count(tail) != 0)
                {
                    continue;
                }
                const std::string head = raw_input.substr(0, raw_input.size() - tail_len);
                const auto head_cuts = quanpin::autocorrect_cut_kbest(head, head_types, kAutocorrectCutKBest);
                if (head_cuts.empty())
                {
                    continue;
                }
                result.corrected_input = true;
                populate_from_cuts(head_cuts, tail);
                break;
            }
        }
    }
    // Both branches rebuild the segmentation string from segments: they already
    // carry the canonical (alias-normalised) spelling, while a caller-passed
    // explicit string could keep the alias spelling — the query would succeed
    // yet nothing would look rewritten, so alias inputs with manual delimiters
    // ("nue'hao") could never get marked. Delimiters sit on syllable boundaries,
    // so split/join round-trips them unchanged ("nu'e" stays "nu'e").
    result.segmentation = result.corrected_input ? quanpin::join_segments(result.corrected_segments)
                                                 : (segments.empty() ? raw_input : quanpin::join_segments(segments));
    result.cache_key = (result.corrected_input ? "C:" : "") + series_cache_key(raw_input, result.segmentation);
    return result;
}

std::string escape_sql_text(std::string text)
{
    size_t pos = 0;
    while ((pos = text.find('\'', pos)) != std::string::npos)
    {
        text.insert(pos, 1, '\'');
        pos += 2;
    }
    return text;
}

} // namespace

QuanpinDictionary::QuanpinDictionary(std::string db_path, metasequoia::RuntimePaths paths)
    : cache_(128), series_cache_(128), segmentation_cache_(128), resolution_cache_(128), paths_(std::move(paths)),
      decoder_(paths_.resource(metasequoia::assets::pinyin_model),
               paths_.user(metasequoia::assets::pinyin_user_dictionary)),
      db_path_(db_path.empty() ? metasequoia::path_to_utf8(paths_.dictionary(metasequoia::assets::main_dictionary))
                               : std::move(db_path))
{

    const int exit = sqlite3_open(db_path_.c_str(), &db_);
    if (exit != SQLITE_OK)
    {
        (void)0;
    }

    quanpin::warm_up(db_, statement_cache_);
    reset_cache_if_database_changed();
}

QuanpinDictionary::~QuanpinDictionary()
{
    for (auto &[sql, stmt] : statement_cache_)
    {
        if (stmt != nullptr)
        {
            sqlite3_finalize(stmt);
        }
    }
    if (db_ != nullptr)
    {
        sqlite3_close(db_);
    }
}

std::vector<WordItem> QuanpinDictionary::query_exact(const std::string &raw_input, const std::string &segmentation,
                                                     unsigned autocorrect_types)
{
    if (raw_input.empty())
    {
        current_candidate_list_.clear();
        return {};
    }

    pinyin_sequence_ = raw_input;
    const auto segments = resolve_segments(raw_input, segmentation);

    // Typing autocorrection: when the spelling is not a legal pinyin
    // combination (the correction cut already fell back to greedy), try to
    // rewrite the whole string into legal syllables. The corrected
    // segmentation becomes the primary key so that selection and weight
    // updates land on the right dictionary entries, and the original
    // (garbage-leaning) candidates stay behind as a fallback tail.
    // Memoize the k-best correction search: it is the expensive part of every
    // keystroke, yet a pure function of this input tuple, so a re-typed or
    // backspaced prefix should reuse it instead of re-running the k=9 beam.
    const std::string resolution_key = std::to_string(autocorrect_types) + '\x1f' + raw_input + '\x1f' + segmentation;
    SeriesQueryResolution resolution;
    if (const auto cached_resolution = resolution_cache_.get(resolution_key))
    {
        resolution = cached_resolution.value();
    }
    else
    {
        resolution = resolve_series_query(raw_input, segments, autocorrect_types);
        resolution_cache_.insert(resolution_key, resolution);
    }
    pinyin_segmentation_ = resolution.segmentation;
    // The alternative readings must be published even on the cache-hit path:
    // mark_autocorrect_candidates runs after every query, cached or not.
    pinyin_alternative_segmentations_.clear();
    for (const auto &alternative : resolution.alternative_corrected_cuts)
    {
        pinyin_alternative_segmentations_.push_back(quanpin::join_segments(alternative));
    }
    // Costlier readings are still corrections, so they must be marked as such
    // even though they rank below the primary tier (see the tail append below).
    for (const auto &alternative : resolution.costlier_corrected_cuts)
    {
        pinyin_alternative_segmentations_.push_back(quanpin::join_segments(alternative));
    }

    // Autocorrected results get their own cache slot so they never leak the
    // fallback tail into plain (correct) spellings sharing the same key.
    if (series_cache_.get(resolution.cache_key))
    {
        reset_cache_if_database_changed();
        if (const auto cached = series_cache_.get(resolution.cache_key))
        {
            current_candidate_list_ = cached.value();
            return current_candidate_list_;
        }
    }

    std::vector<quanpin::Segments> alternative_segmentations;
    std::unordered_set<std::string> seen_segmentations = {pinyin_segmentation_};
    // Keep costlier readings out of the frequency-competing merge tier; they are
    // appended after it below so dictionary frequency never lifts a dearer
    // correction above the cheaper primary. Seeding "seen" here also blocks the
    // greedy correction paths from re-introducing them into the merge tier.
    for (const auto &costlier : resolution.costlier_corrected_cuts)
    {
        seen_segmentations.insert(quanpin::join_segments(costlier));
    }
    const auto append_alternative = [&](const quanpin::Segments &candidate) {
        const std::string key = quanpin::join_segments(candidate);
        if (!key.empty() && seen_segmentations.insert(key).second &&
            alternative_segmentations.size() < kSyllableGraphPathLimit)
        {
            alternative_segmentations.push_back(candidate);
        }
    };

    // Correction-mode segmentation is part of autocorrection and must follow the
    // same switch. query() defaults autocorrect_types to 0, so running this
    // unconditionally changed the default behaviour for every caller: a misspelled
    // input such as "sahng" started offering the corrected candidate even with
    // autocorrection explicitly turned off.
    if (autocorrect_types != 0)
    {
        // Correction alternatives first: they explain the letters the user
        // actually typed, so they outrank the phonetic-shape alias readings that
        // follow (the alias layer rewrites legal-looking spellings regardless).
        for (const auto &candidate : resolution.alternative_corrected_cuts)
        {
            append_alternative(candidate);
        }
        const auto correction_paths = quanpin::cut_pinyin_by_mode(raw_input, "correction");
        for (const auto &candidate : correction_paths)
        {
            append_alternative(candidate);
        }
    }

    if (raw_input.find('\'') == std::string::npos && segments.size() <= kMaxSyllablesForMultipleSegmentations &&
        quanpin::has_only_complete_pinyin_segments(segments))
    {
        const auto complete_paths = quanpin::enumerate_complete_segmentations(quanpin::build_syllable_graph(raw_input),
                                                                              kSyllableGraphPathLimit);
        for (const auto &candidate : complete_paths)
        {
            append_alternative(candidate);
        }
    }

    std::vector<WordItem> result;
    if (resolution.corrected_input)
    {
        result = query_series(raw_input, pinyin_segmentation_, resolution.corrected_segments);
        const std::string fallback_segmentation =
            segmentation.empty() ? quanpin::join_segments(segments) : segmentation;
        append_unique_words(result, query_series(raw_input, fallback_segmentation, segments));
        // Query-time disambiguation (patent M3): the alternative readings of the
        // same typo compete with the primary cut under dictionary word
        // frequency; the best one keeps a protected slot near the top.
        if (!alternative_segmentations.empty())
        {
            result = merge_alternative_segmentations(raw_input, pinyin_segmentation_, resolution.corrected_segments,
                                                     alternative_segmentations, std::move(result));
        }
        // Costlier readings sit below the whole primary cost tier: append them
        // last so a high-frequency dearer correction (gau -> gai, weight 13)
        // can never precede the cheaper one (gau -> gua, weight 10).
        for (const auto &costlier : resolution.costlier_corrected_cuts)
        {
            append_unique_words(result, query_series(raw_input, quanpin::join_segments(costlier), costlier));
        }
    }
    else
    {
        result = query_series(raw_input, pinyin_segmentation_, segments);
        if (!alternative_segmentations.empty())
        {
            result = merge_alternative_segmentations(raw_input, pinyin_segmentation_, segments,
                                                     alternative_segmentations, std::move(result));
        }
    }
    series_cache_.insert(resolution.cache_key, result);
    current_candidate_list_ = result;
    return current_candidate_list_;
}

std::optional<WordItem> QuanpinDictionary::find_candidate(const std::string &key, const std::string &value)
{
    const std::string table = quanpin::build_table_name(quanpin::split_segments(key));
    if (!db_ || table.empty())
        return std::nullopt;
    sqlite3_stmt *stmt = nullptr;
    const std::string sql = "SELECT weight FROM \"" + table + "\" WHERE key=?1 AND value=?2 LIMIT 1";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return std::nullopt;
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> guard(stmt, sqlite3_finalize);
    if (sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(stmt) != SQLITE_ROW)
        return std::nullopt;
    return WordItem(key, value, sqlite3_column_int64(stmt, 0), CandidateSource::Database, key);
}

bool QuanpinDictionary::expand_initial_candidates(const std::string &code, std::vector<WordItem> &candidates)
{
    if (code.size() != 1)
    {
        return false;
    }

    const auto is_limited_initial = [&](const WordItem &item) {
        return item.source == CandidateSource::Database && item.pinyin == code;
    };
    const size_t limited_count =
        static_cast<size_t>(std::count_if(candidates.begin(), candidates.end(), is_limited_initial));
    constexpr size_t kInitialCandidateLimit = 24;
    if (limited_count != kInitialCandidateLimit)
    {
        return false;
    }

    auto expanded = query_initial(code, INT_MAX);
    if (expanded.size() <= limited_count)
    {
        return false;
    }
    for (auto &item : expanded)
    {
        item.canonical_pinyin = item.pinyin;
        item.pinyin = code;
    }

    std::vector<WordItem> merged;
    merged.reserve(candidates.size() - limited_count + expanded.size());
    bool inserted = false;
    for (auto &item : candidates)
    {
        if (is_limited_initial(item))
        {
            if (!inserted)
            {
                merged.insert(merged.end(), expanded.begin(), expanded.end());
                inserted = true;
            }
            continue;
        }
        merged.push_back(std::move(item));
    }

    candidates = std::move(merged);
    cache_.insert(code, expanded);
    series_cache_.insert(series_cache_key(pinyin_sequence_, pinyin_segmentation_), candidates);
    current_candidate_list_ = candidates;
    return true;
}

std::vector<WordItem> QuanpinDictionary::query_series(const std::string &raw_input, const std::string &segmentation,
                                                      const quanpin::Segments &segments)
{
    if (segments.empty())
    {
        return query_single_path(raw_input, segmentation, segments);
    }

    std::vector<WordItem> result;
    for (size_t count = segments.size(); count > 0; --count)
    {
        quanpin::Segments partial_segments(segments.begin(), segments.begin() + static_cast<std::ptrdiff_t>(count));
        const std::string partial_segmentation = quanpin::join_segments(partial_segments);
        const std::string partial_input = remove_delimiters(partial_segmentation);
        auto partial_result = query_single_path(partial_input, partial_segmentation, partial_segments);
        result.insert(result.end(), partial_result.begin(), partial_result.end());
    }

    if (segments.size() >= 3 && quanpin::has_only_complete_pinyin_segments(segments))
    {
        // Keep one local Google-Pinyin sentence as the primary whole-sentence
        // suggestion.  The lattice is a secondary source; it should add only
        // its best path rather than filling the candidate page with near-
        // duplicate low-quality sentences.
        const std::string normalized = remove_delimiters(segmentation.empty() ? raw_input : segmentation);
        const std::string google_sentence = search_sentence_from_ime_engine(normalized);
        if (!google_sentence.empty())
        {
            const auto duplicate = std::find_if(result.begin(), result.end(),
                                                [&](const WordItem &item) { return item.word == google_sentence; });
            if (duplicate == result.end())
                // 整句 fallback 必须带上 canonical quanpin，否则以它结尾的造词无法落库：
                // update_creating_word_progress 依赖 canonical_pinyin 才能拼出完整读音。
                // segmentation 为空时保持为空，交由既有逻辑判定为不可落库。
                result.insert(result.begin(), WordItem(segmentation.empty() ? raw_input : segmentation, google_sentence,
                                                       1, CandidateSource::Fallback, segmentation));
        }

        quanpin::WordLatticeOptions lattice_options;
        lattice_options.nbest = 1;
        quanpin::merge_lattice_candidates(result, segments,
                                          quanpin::make_lattice_db_lookup(db_, statement_cache_,
                                                                          quanpin::QuerySource::Quanpin,
                                                                          lattice_options.span_limit),
                                          segmentation.empty() ? raw_input : segmentation, lattice_options);
        if (!google_sentence.empty())
        {
            const auto google = std::find_if(result.begin(), result.end(), [&](const WordItem &item) {
                return item.word == google_sentence && item.source == CandidateSource::Fallback;
            });
            if (google != result.end() && google != result.begin())
            {
                WordItem preferred = std::move(*google);
                result.erase(google);
                result.insert(result.begin(), std::move(preferred));
            }
        }
    }

    if (result.size() < kSparsePinyinFallbackThreshold)
    {
        result = append_sparse_pinyin_fallbacks(segments, std::move(result));
    }

    return result;
}

std::vector<WordItem> QuanpinDictionary::query_single_path(const std::string &raw_input,
                                                           const std::string &segmentation,
                                                           const quanpin::Segments &segments)
{
    const std::string cache_key = segmentation.empty() ? raw_input : segmentation;
    if (auto cached = cache_.get(cache_key))
    {
        return cached.value();
    }

    std::vector<WordItem> result = query_database(segments, segmentation);
    result = append_ime_fallback(raw_input, segmentation, std::move(result));
    cache_.insert(cache_key, result);
    return result;
}

quanpin::Segments QuanpinDictionary::resolve_segments(const std::string &raw_input, const std::string &segmentation)
{
    // 单一归一咽喉点：显式 segmentation 与自动切分两条路径都过这里，进入查询
    // 管线的 segments 由此保证已归一恰一次。查库、调频、造词各调用点因此
    // 不必各自处理别名。
    quanpin::Segments segments =
        segmentation.empty() ? get_or_compute_segments(raw_input) : quanpin::split_segments(segmentation);
    return normalize_umlaut_aliases(std::move(segments));
}

quanpin::Segments QuanpinDictionary::get_or_compute_segments(const std::string &raw_input)
{
    if (auto cached = segmentation_cache_.get(raw_input))
    {
        return cached.value();
    }

    const auto cuts = quanpin::cut_pinyin_by_mode(raw_input, "correction");
    const auto segments = cuts.empty() ? quanpin::Segments{} : cuts.front();
    segmentation_cache_.insert(raw_input, segments);
    return segments;
}

int QuanpinDictionary::handleVkCode(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    (void)modifiers_down;

    if (vk == ImeKey::Backspace)
    {
        if (!pinyin_sequence_.empty())
        {
            pinyin_sequence_.pop_back();
        }
    }
    else if (vk == ImeKey::Escape || vk == ImeKey::Return || vk == ImeKey::Space)
    {
        reset_state();
        return OK;
    }
    else if (vk == ImeKey::Apostrophe)
    {
        pinyin_sequence_.push_back('\'');
    }
    else if (is_alpha_vk(vk))
    {
        if (wch >= u'A' && wch <= u'Z')
        {
            pinyin_sequence_.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(wch))));
        }
        else if (wch >= u'a' && wch <= u'z')
        {
            pinyin_sequence_.push_back(static_cast<char>(wch));
        }
        else
        {
            pinyin_sequence_.push_back(static_cast<char>(vk + ('a' - 'A')));
        }
    }

    query(pinyin_sequence_);
    return OK;
}

std::vector<WordItem> QuanpinDictionary::query_database(const quanpin::Segments &segments,
                                                        const std::string &segmentation)
{
    if (db_ == nullptr)
    {
        return {};
    }

    try
    {
        if (segments.size() == 1 && segments.front().size() == 1)
        {
            constexpr int kInitialCandidateLimit = 24;
            auto result = query_initial(segments.front(), kInitialCandidateLimit);
            const std::string matched_code = segmentation.empty() ? segments.front() : segmentation;
            for (auto &item : result)
            {
                item.canonical_pinyin = item.pinyin;
                item.pinyin = matched_code;
            }
            return result;
        }

        // segments 已在 resolve_segments 归一，直接按标准键查库。
        const auto flat_items = quanpin::query_segments_keyed_flat(segments, db_, statement_cache_, INT_MAX);
        std::vector<WordItem> result;
        result.reserve(flat_items.size());
        const std::string code = segmentation.empty() ? quanpin::join_segments(segments) : segmentation;
        for (const auto &item : flat_items)
        {
            result.emplace_back(code, item.value, item.weight, CandidateSource::Database, item.key);
        }
        return result;
    }
    catch (const std::exception &)
    {
        (void)0;
        return {};
    }
}

std::vector<WordItem> QuanpinDictionary::query_initial(const std::string &code, int limit)
{
    if (db_ == nullptr || code.size() != 1)
    {
        return {};
    }

    const auto rows = quanpin::query_initial(db_, code, limit);
    std::vector<WordItem> result;
    result.reserve(rows.size());
    for (const auto &item : rows)
    {
        result.emplace_back(item.key, item.value, item.weight, CandidateSource::Database, item.key);
    }
    return result;
}

std::vector<WordItem> QuanpinDictionary::merge_alternative_segmentations(
    const std::string &raw_input, const std::string &primary_segmentation, const quanpin::Segments &primary_segments,
    const std::vector<quanpin::Segments> &alternative_segmentations, std::vector<WordItem> result)
{
    const auto alternative_items = quanpin::query_exact_segmentations_keyed_flat(
        alternative_segmentations, db_, statement_cache_, kAlternativeSegmentationCandidateLimit);
    if (alternative_items.empty())
    {
        return result;
    }

    const auto primary_full = query_single_path(raw_input, primary_segmentation, primary_segments);
    std::vector<WordItem> alternative_full;
    alternative_full.reserve(alternative_items.size());
    for (const auto &item : alternative_items)
    {
        alternative_full.emplace_back(item.key, item.value, item.weight, CandidateSource::Database, item.key);
    }

    std::vector<WordItem> merged_full = primary_full;
    merged_full.insert(merged_full.end(), alternative_full.begin(), alternative_full.end());
    std::stable_sort(merged_full.begin(), merged_full.end(),
                     [](const WordItem &lhs, const WordItem &rhs) { return lhs.weight > rhs.weight; });
    std::unordered_set<std::string> seen_full_words;
    merged_full.erase(std::remove_if(merged_full.begin(), merged_full.end(),
                                     [&](const WordItem &item) { return !seen_full_words.insert(item.word).second; }),
                      merged_full.end());

    // Weights rank candidates reliably within one pinyin key, but are not directly comparable across
    // different segmentations. Keep the best alternative interpretation visible without letting every
    // segmentation occupy a protected slot on the first page.
    const std::string &best_alternative_word = alternative_items.front().value;
    const auto best_alternative = std::find_if(merged_full.begin(), merged_full.end(), [&](const WordItem &item) {
        return item.word == best_alternative_word;
    });
    if (best_alternative != merged_full.end() &&
        static_cast<size_t>(std::distance(merged_full.begin(), best_alternative)) >
            kBestAlternativeSegmentationMaxIndex)
    {
        WordItem promoted = std::move(*best_alternative);
        merged_full.erase(best_alternative);
        merged_full.insert(merged_full.begin() + static_cast<std::ptrdiff_t>(kBestAlternativeSegmentationMaxIndex),
                           std::move(promoted));
    }

    std::vector<WordItem> merged = std::move(merged_full);
    const size_t primary_full_count = primary_full.size() < result.size() ? primary_full.size() : result.size();
    std::vector<WordItem> remaining(result.begin() + static_cast<std::ptrdiff_t>(primary_full_count), result.end());
    append_unique_words(merged, remaining);
    return merged;
}

std::vector<WordItem> QuanpinDictionary::append_ime_fallback(const std::string &raw_input,
                                                             const std::string &segmentation,
                                                             std::vector<WordItem> result)
{
    if (!result.empty())
    {
        return result;
    }

    const std::string normalized = remove_delimiters(segmentation.empty() ? raw_input : segmentation);
    const std::string sentence = search_sentence_from_ime_engine(normalized);
    if (sentence.empty())
    {
        return result;
    }

    const auto exists =
        std::find_if(result.begin(), result.end(), [&](const WordItem &item) { return item.word == sentence; });
    if (exists == result.end())
    {
        // 同上，整句 fallback 需要 canonical quanpin 才能参与造词落库。
        result.emplace_back(segmentation.empty() ? raw_input : segmentation, sentence, 1, CandidateSource::Fallback,
                            segmentation);
    }
    return result;
}

std::vector<WordItem> QuanpinDictionary::append_sparse_pinyin_fallbacks(const quanpin::Segments &segments,
                                                                        std::vector<WordItem> result)
{
    for (const auto &fallback_segments : quanpin::sparse_pinyin_fallback_segments(segments))
    {
        if (fallback_segments.empty())
        {
            continue;
        }

        const std::string fallback_segmentation = quanpin::join_segments(fallback_segments);
        const std::string fallback_input = remove_delimiters(fallback_segmentation);
        const auto fallback_result = query_single_path(fallback_input, fallback_segmentation, fallback_segments);
        append_unique_words(result, fallback_result);
    }
    return result;
}

void QuanpinDictionary::append_unique_words(std::vector<WordItem> &result, const std::vector<WordItem> &extra)
{
    for (const auto &item : extra)
    {
        const auto exists = std::find_if(result.begin(), result.end(),
                                         [&](const WordItem &existing) { return existing.word == item.word; });
        if (exists == result.end())
        {
            result.push_back(item);
        }
    }
}

void QuanpinDictionary::mark_autocorrect_candidates(std::vector<WordItem> &candidates, const std::string &raw_input)
{
    // A candidate comes from a corrected interpretation exactly when its code
    // letters equal any correction cut's letters (primary or ranked
    // alternative) while those differ from the typed letters. The letters-only
    // comparison alone would also sweep up prefix candidates (keneng -> ke,
    // single-letter jianpin expansions) that the user spelled correctly; both
    // rules together keep those unmarked. The ü-style alias rewrite (nue->nve,
    // jv->ju) is a first-class marking source: it runs in resolve_segments
    // regardless of the autocorrect switches, so an alias-rewritten query marks
    // its candidates with the typed spelling, while a standard spelling exits
    // at the all-equal gate because every set equals the typed letters.
    std::vector<std::string> corrected_letter_sets;
    corrected_letter_sets.reserve(1 + pinyin_alternative_segmentations_.size());
    corrected_letter_sets.push_back(fold_autocorrect_letters(pinyin_segmentation_));
    for (const auto &alternative : pinyin_alternative_segmentations_)
    {
        corrected_letter_sets.push_back(fold_autocorrect_letters(alternative));
    }
    corrected_letter_sets.erase(std::remove_if(corrected_letter_sets.begin(), corrected_letter_sets.end(),
                                               [](const std::string &letters) { return letters.empty(); }),
                                corrected_letter_sets.end());
    const std::string raw_letters = fold_autocorrect_letters(raw_input);
    if (corrected_letter_sets.empty() ||
        std::all_of(corrected_letter_sets.begin(), corrected_letter_sets.end(),
                    [&](const std::string &letters) { return letters == raw_letters; }))
    {
        return;
    }
    for (auto &item : candidates)
    {
        if (!item.corrected_from.empty())
        {
            continue;
        }
        const std::string item_letters = fold_autocorrect_letters(item.pinyin);
        // Match only against readings that actually differ from the typed
        // letters. An alternative cut can fold back to exactly raw_letters
        // (letters removed by the fold but rewritten by the cut); matching that
        // set would stamp corrected_from onto a candidate the user spelled
        // correctly. The all-equal gate above only guards the case where EVERY
        // set equals raw_letters, so this per-set filter is needed.
        if (std::any_of(corrected_letter_sets.begin(), corrected_letter_sets.end(),
                        [&](const std::string &letters) { return letters != raw_letters && item_letters == letters; }))
        {
            item.corrected_from = raw_letters;
        }
    }
}

int QuanpinDictionary::create_word(std::string pinyin, std::string word)
{
    pinyin = remove_delimiters(pinyin);
    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    if (cuts.empty())
    {
        return ERROR_CODE;
    }

    pinyin = quanpin::join_segments(cuts.front());
    const std::string jp = quanpin::segments_to_jianpin(cuts.front());
    if (!do_validate(pinyin, jp, word))
    {
        return ERROR_CODE;
    }

    if (check_data(build_sql_for_checking_word(pinyin, word)))
    {
        return OK;
    }

    if (insert_data(build_sql_for_inserting_word(pinyin, jp, word)) != OK)
    {
        return ERROR_CODE;
    }
    (void)user_dictionary::record_user_insert(metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal)),
                                              user_dictionary::DictionaryKind::Pinyin, pinyin, word, 10000);
    reset_cache();
    return OK;
}

int QuanpinDictionary::create_word_from_canonical_pinyin(std::string pinyin, std::string word)
{
    const auto segments = quanpin::split_segments(pinyin);
    const size_t han_count = HelpcodeUtils::count_han_chars(word);
    if (segments.empty() || segments.size() != han_count ||
        std::any_of(segments.begin(), segments.end(), [](const std::string &segment) {
            return segment.empty() || !quanpin::is_complete_pinyin_input(segment);
        }))
    {
        return ERROR_CODE;
    }

    pinyin = quanpin::join_segments(segments);
    const std::string jp = quanpin::segments_to_jianpin(segments);
    if (!do_validate(pinyin, jp, word))
    {
        return ERROR_CODE;
    }
    if (check_data(build_sql_for_checking_word(pinyin, word)))
    {
        return OK;
    }
    if (insert_data(build_sql_for_inserting_word(pinyin, jp, word)) != OK)
    {
        return ERROR_CODE;
    }
    (void)user_dictionary::record_user_insert(metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal)),
                                              user_dictionary::DictionaryKind::Pinyin, pinyin, word, 10000);
    reset_cache();
    return OK;
}

int QuanpinDictionary::update_weight_by_word(std::string word)
{
    return update_weight_by_pinyin_and_word(remove_delimiters(pinyin_segmentation_), std::move(word));
}

int QuanpinDictionary::update_weight_by_pinyin_and_word(std::string pinyin, std::string word)
{
    pinyin = remove_delimiters(pinyin);
    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    if (cuts.empty())
        return ERROR_CODE;
    auto segments = cuts.front();
    // 别名拼写（nue/lue 等词库不存在的键）不得承载用户调频数据：改写为标准键，
    // 保证权重更新落在标准字典行上（PRD R3）。
    segments = normalize_umlaut_aliases(std::move(segments));
    const size_t han_count = HelpcodeUtils::count_han_chars(word);
    if (segments.size() > han_count)
        segments.resize(han_count);
    const std::string normalized = quanpin::join_segments(segments);
    if (update_data(build_sql_for_updating_word(normalized, word)) != OK)
    {
        return ERROR_CODE;
    }
    (void)user_dictionary::record_pinyin_upsert_from_database(
        db_path_, normalized, word, metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal)));
    reset_cache();
    return OK;
}

int QuanpinDictionary::delete_by_pinyin_and_word(std::string pinyin, std::string word)
{
    pinyin = remove_delimiters(pinyin);
    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    if (cuts.empty())
        return ERROR_CODE;
    const std::string normalized = quanpin::join_segments(cuts.front());
    if (!user_dictionary::delete_dictionary_candidate(
            db_path_, metasequoia::path_to_utf8(paths_.user(metasequoia::assets::user_journal)),
            user_dictionary::DictionaryKind::Pinyin, normalized, word))
        return ERROR_CODE;
    reset_cache();
    return OK;
}

int QuanpinDictionary::insert_word_to_series_cache(const std::string &pinyin, const std::string &word,
                                                   CandidateSource source)
{
    if (pinyin.empty() || word.empty())
    {
        return ERROR_CODE;
    }

    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    const std::string segmentation = cuts.empty() ? pinyin : quanpin::join_segments(cuts.front());
    const std::string cache_key = series_cache_key(pinyin, segmentation);
    return insert_word_to_series_cache_key(cache_key, pinyin, word, source);
}

int QuanpinDictionary::insert_word_to_series_cache(const std::string &raw_input, const std::string &segmentation,
                                                   unsigned autocorrect_types, const std::string &word,
                                                   CandidateSource source)
{
    if (raw_input.empty() || word.empty())
    {
        return ERROR_CODE;
    }

    const auto segments = resolve_segments(raw_input, segmentation);
    const auto resolution = resolve_series_query(raw_input, segments, autocorrect_types);
    return insert_word_to_series_cache_key(resolution.cache_key, raw_input, word, source);
}

int QuanpinDictionary::insert_word_to_series_cache_key(const std::string &cache_key, const std::string &pinyin,
                                                       const std::string &word, CandidateSource source)
{
    auto list = series_cache_.get(cache_key).value_or(std::vector<WordItem>{});

    // Keep at most one cloud/AI suggestion in the series cache for this key.
    if (source == CandidateSource::AiSuggestion || source == CandidateSource::CloudSuggestion)
    {
        list.erase(
            std::remove_if(list.begin(), list.end(), [source](const WordItem &item) { return item.source == source; }),
            list.end());
    }

    const auto exists = std::find_if(list.begin(), list.end(), [&](const WordItem &item) { return item.word == word; });
    if (exists == list.end())
    {
        if (list.empty())
        {
            list.emplace_back(pinyin, word, 1, source);
        }
        else
        {
            const size_t index = source == CandidateSource::AiSuggestion ? std::min<size_t>(2, list.size()) : 1;
            list.insert(list.begin() + index, WordItem(pinyin, word, 1, source));
        }
    }

    if (source == CandidateSource::CloudSuggestion)
    {
        const auto ai = std::find_if(list.begin(), list.end(),
                                     [](const WordItem &item) { return item.source == CandidateSource::AiSuggestion; });
        if (ai != list.end())
        {
            WordItem ai_item = std::move(*ai);
            list.erase(ai);
            list.insert(list.begin() + std::min<size_t>(2, list.size()), std::move(ai_item));
        }
    }

    series_cache_.insert(cache_key, list);
    return OK;
}

std::string QuanpinDictionary::search_sentence_from_ime_engine(const std::string &user_pinyin)
{
    return decoder_.sentence(user_pinyin);
}

void QuanpinDictionary::reset_state()
{
    pinyin_sequence_.clear();
    pinyin_segmentation_.clear();
    pinyin_alternative_segmentations_.clear();
    current_candidate_list_.clear();
}

void QuanpinDictionary::reset_cache()
{
    cache_.clear();
    series_cache_.clear();
    segmentation_cache_.clear();
}

void QuanpinDictionary::reset_cache_if_database_changed()
{
    if (db_ == nullptr)
    {
        return;
    }
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA data_version", -1, &statement, nullptr) != SQLITE_OK)
    {
        return;
    }
    if (sqlite3_step(statement) != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        return;
    }
    const sqlite3_int64 current_version = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    if (data_version_ >= 0 && current_version != data_version_)
    {
        reset_cache();
    }
    data_version_ = current_version;
}

std::vector<std::string> QuanpinDictionary::select_data(const std::string &sql_str)
{
    std::vector<std::string> candidate_list;
    if (db_ == nullptr)
    {
        return candidate_list;
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql_str.c_str(), -1, &stmt, 0) != SQLITE_OK)
    {
        (void)0;
        return candidate_list;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        candidate_list.push_back(std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2))));
    }
    sqlite3_finalize(stmt);
    return candidate_list;
}

std::vector<WordItem> QuanpinDictionary::select_complete_data(const std::string &sql_str)
{
    std::vector<WordItem> candidate_list;
    if (db_ == nullptr)
    {
        return candidate_list;
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql_str.c_str(), -1, &stmt, 0) != SQLITE_OK)
    {
        (void)0;
        return candidate_list;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        candidate_list.emplace_back(std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))),
                                    std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2))),
                                    sqlite3_column_int64(stmt, 3), CandidateSource::Database,
                                    std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))));
    }
    sqlite3_finalize(stmt);
    return candidate_list;
}

int QuanpinDictionary::check_data(const std::string &sql_str)
{
    if (db_ == nullptr)
    {
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql_str.c_str(), -1, &stmt, 0) != SQLITE_OK)
    {
        (void)0;
        return false;
    }

    const bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

int QuanpinDictionary::insert_data(const std::string &sql_str)
{
    if (db_ == nullptr)
    {
        return ERROR_CODE;
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql_str.c_str(), -1, &stmt, 0) != SQLITE_OK)
    {
        (void)0;
        return ERROR_CODE;
    }
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok ? OK : ERROR_CODE;
}

int QuanpinDictionary::update_data(const std::string &sql_str)
{
    if (db_ == nullptr)
    {
        return ERROR_CODE;
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql_str.c_str(), -1, &stmt, 0) != SQLITE_OK)
    {
        (void)0;
        return ERROR_CODE;
    }
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok ? OK : ERROR_CODE;
}

int QuanpinDictionary::delete_data(const std::string &sql_str)
{
    if (db_ == nullptr)
    {
        return ERROR_CODE;
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql_str.c_str(), -1, &stmt, 0) != SQLITE_OK)
    {
        (void)0;
        return ERROR_CODE;
    }
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok ? OK : ERROR_CODE;
}

std::string QuanpinDictionary::build_sql_for_creating_word(const std::string &pinyin)
{
    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    if (cuts.empty())
    {
        return "";
    }

    std::string sql;
    for (size_t i = 1; i <= cuts.front().size(); ++i)
    {
        std::vector<std::string> partial(cuts.front().begin(), cuts.front().begin() + i);
        const std::string key = quanpin::join_segments(partial);
        const std::string table = quanpin::build_table_name(partial);
        const std::string each =
            fmt::format("select * from(select * from {} where key = '{}' order by weight desc)", table, key);
        sql = sql.empty() ? each : each + " union all " + sql;
    }
    return sql;
}

std::string QuanpinDictionary::build_sql_for_checking_word(const std::string &key, const std::string &value)
{
    const auto cuts = quanpin::cut_pinyin_by_mode(key, "correction");
    if (cuts.empty())
    {
        return "";
    }
    const std::string table = quanpin::build_table_name(cuts.front());
    return fmt::format("select 1 from {} where key = '{}' and value = '{}';", table, escape_sql_text(key),
                       escape_sql_text(value));
}

std::string QuanpinDictionary::build_sql_for_inserting_word(const std::string &key, const std::string &jp,
                                                            const std::string &value)
{
    const auto cuts = quanpin::cut_pinyin_by_mode(key, "correction");
    if (cuts.empty())
    {
        return "";
    }
    const std::string table = quanpin::build_table_name(cuts.front());
    return fmt::format("insert into {} (key, jp, value, weight) values ('{}', '{}', '{}', '{}');", table,
                       escape_sql_text(key), escape_sql_text(jp), escape_sql_text(value), 10000);
}

std::string QuanpinDictionary::build_sql_for_updating_word(const std::string &word)
{
    return build_sql_for_updating_word(remove_delimiters(pinyin_segmentation_), word);
}

std::string QuanpinDictionary::build_sql_for_updating_word(std::string pinyin, const std::string &word)
{
    pinyin = remove_delimiters(pinyin);
    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    if (cuts.empty())
    {
        return "";
    }

    size_t han_cnt = HelpcodeUtils::count_han_chars(word);
    auto segments = cuts.front();
    if (segments.size() > han_cnt)
    {
        segments.resize(han_cnt);
    }

    pinyin = quanpin::join_segments(segments);
    const std::string jp = quanpin::segments_to_jianpin(segments);
    if (!do_validate(pinyin, jp, word))
    {
        return "";
    }

    const std::string table = quanpin::build_table_name(segments);
    return fmt::format("update {0} set weight = ( select MAX(weight) + 1 from {0} AS sub where sub.key = '{1}') "
                       "where key = '{1}' and value = '{2}';",
                       table, escape_sql_text(pinyin), escape_sql_text(word));
}

std::string QuanpinDictionary::build_sql_for_deleting_word(std::string pinyin, const std::string &word)
{
    pinyin = remove_delimiters(pinyin);
    const auto cuts = quanpin::cut_pinyin_by_mode(pinyin, "correction");
    if (cuts.empty())
    {
        return "";
    }

    const std::string normalized = quanpin::join_segments(cuts.front());
    const std::string jp = quanpin::segments_to_jianpin(cuts.front());
    if (!do_validate(normalized, jp, word))
    {
        return "";
    }

    return fmt::format("delete from {} where key = '{}' and value = '{}';", quanpin::build_table_name(cuts.front()),
                       escape_sql_text(normalized), escape_sql_text(word));
}

bool QuanpinDictionary::do_validate(const std::string &key, const std::string &jp, const std::string &value)
{
    const std::string pure_key = remove_delimiters(key);
    if (pure_key.empty())
    {
        return false;
    }

    const size_t han_count = HelpcodeUtils::count_han_chars(value);
    if (jp.size() != han_count)
    {
        return false;
    }

    const auto cuts = quanpin::cut_pinyin_by_mode(pure_key, "correction");
    if (cuts.empty())
    {
        return false;
    }

    return cuts.front().size() == han_count;
}

std::vector<WordItem> QuanpinDictionary::fuzzy_candidates(const std::string &segmentation,
                                                          metasequoia::FuzzyPinyinOptions options)
{
    std::vector<WordItem> result;
    if (!options.rules || !db_)
        return result;
    reset_cache_if_database_changed();
    const auto cache_key = "fuzzy:" + std::to_string(options.rules) + ":" + segmentation;
    if (const auto cached = series_cache_.get(cache_key))
        return *cached;
    const auto segments = quanpin::split_segments(segmentation);
    std::size_t budget = 128;
    for (std::size_t count = segments.size(); count > 0 && budget > 1; --count)
    {
        const quanpin::Segments prefix(segments.begin(), segments.begin() + count);
        const auto paths = quanpin::fuzzy_segmentations(prefix, options, std::min<std::size_t>(64, budget));
        if (paths.empty())
            continue;
        budget -= paths.size();
        const auto rows = quanpin::query_exact_segmentations_keyed_flat(paths, db_, statement_cache_, 128);
        for (const auto &row : rows)
        {
            WordItem item(quanpin::join_segments(prefix), row.value, row.weight, CandidateSource::Database, row.key);
            item.fuzzy = true;
            result.push_back(std::move(item));
        }
    }
    series_cache_.insert(cache_key, result);
    return result;
}

std::vector<WordItem> QuanpinDictionary::query(const std::string &raw_input, const std::string &segmentation,
                                               unsigned autocorrect, metasequoia::FuzzyPinyinOptions fuzzy)
{
    auto result = query_exact(raw_input, segmentation, autocorrect);
    if (fuzzy.rules && !raw_input.empty())
    {
        // Keep ordinary cache slots free of preference-specific candidates.
        const auto typed =
            segmentation.empty() ? quanpin::join_segments(resolve_segments(raw_input, segmentation)) : segmentation;
        append_unique_words(result, fuzzy_candidates(typed, fuzzy));
        std::stable_sort(result.begin(), result.end(),
                         [](const auto &a, const auto &b) { return a.pinyin.size() > b.pinyin.size(); });
    }
    // Labeling runs after every mutation (including the fuzzy merge) so the
    // returned list and the published candidate list always agree.
    mark_autocorrect_candidates(result, raw_input);
    current_candidate_list_ = result;
    return result;
}
