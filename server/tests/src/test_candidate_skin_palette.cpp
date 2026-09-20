#include "tests/includes/test_framework.h"
#include "window/candidate_skin_palette.h"

TEST_CASE(candidate_skin_palette_resolves_builtin_theme_and_text_override)
{
    const CandidateSkinPalette dark = ResolveCandidateSkinPalette("wechat", false, "auto");
    REQUIRE_EQ(FlattenCandidateColor(dark.surface, dark.surface), RGB(21, 21, 21));
    REQUIRE_EQ(FlattenCandidateColor(dark.border, dark.surface), RGB(41, 41, 41));
    REQUIRE_EQ(FlattenCandidateColor(dark.text, dark.surface), RGB(183, 183, 183));

    const CandidateSkinPalette light = ResolveCandidateSkinPalette("graphite", true, "#123456");
    REQUIRE_EQ(FlattenCandidateColor(light.surface, light.surface), RGB(251, 251, 252));
    REQUIRE_EQ(FlattenCandidateColor(light.border, light.surface), RGB(226, 229, 233));
    REQUIRE_EQ(FlattenCandidateColor(light.text, light.surface), RGB(18, 52, 86));
}

TEST_CASE(candidate_skin_palette_applies_custom_colors_with_builtin_fallbacks)
{
    CandidateSkinCatalog::CandidateColors colors;
    colors.surface = "#102030";
    colors.text = "rgba(255, 255, 255, 0.5)";
    const CandidateSkinPalette palette = ResolveCandidateSkinPalette("custom", false, "auto", &colors);

    REQUIRE_EQ(FlattenCandidateColor(palette.surface, palette.surface), RGB(16, 32, 48));
    REQUIRE_EQ(FlattenCandidateColor(palette.border, palette.surface), RGB(41, 54, 67));
    REQUIRE_EQ(FlattenCandidateColor(palette.text, palette.surface), RGB(136, 144, 152));
}

TEST_CASE(candidate_skin_palette_flattens_alpha_for_gdi)
{
    REQUIRE_EQ(FlattenCandidateColor(D2D1::ColorF(1.0f, 0.0f, 0.0f, 0.25f), D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f)),
               RGB(64, 0, 191));
}

TEST_CASE(candidate_skin_palette_flattens_translucent_surface_before_border_and_text)
{
    const CandidateSkinPalette palette{D2D1::ColorF(1.0f, 0.0f, 0.0f, 0.5f), D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.25f),
                                       D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.5f)};
    const CandidateSkinPalette flattened =
        FlattenCandidateSkinPaletteForGdi(palette, D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f));

    REQUIRE_EQ(FlattenCandidateColor(flattened.surface, flattened.surface), RGB(128, 0, 128));
    REQUIRE_EQ(FlattenCandidateColor(flattened.border, flattened.surface), RGB(96, 0, 96));
    REQUIRE_EQ(FlattenCandidateColor(flattened.text, flattened.surface), RGB(191, 128, 191));
}
