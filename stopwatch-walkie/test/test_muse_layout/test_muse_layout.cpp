#include "muse_layout.h"

#include <unity.h>

using namespace stopwatch;

namespace {

// A monospace stand-in for the VLW fonts: CJK and full-width = px, ASCII = px / 2, "X" has no glyph.
int advance(int px, uint32_t cp)
{
    if (cp == 'X') return 0;
    return cp < 0x80 ? px / 2 : px;
}

std::string join(const MuseTextBlock& block)
{
    std::string out;
    for (const std::string& line : block.lines) out += line;
    return out;
}

}  // namespace

void test_chord_is_widest_at_the_centre()
{
    const MuseRound round;
    TEST_ASSERT_EQUAL(376, museChordWidth(round, 213, 253));  // 2·√(190²−20²)
    TEST_ASSERT_TRUE(museChordWidth(round, 112, 152) < museChordWidth(round, 193, 233));
    TEST_ASSERT_EQUAL(0, museChordWidth(round, 0, 30));
}

void test_short_text_is_one_large_centred_line()
{
    const MuseTextBlock block = museLayout("今天的提问次数用完了", {32, 24}, 112, 366, MuseRound(), advance);
    TEST_ASSERT_EQUAL(32, block.px);
    TEST_ASSERT_EQUAL(1, block.lines.size());
    TEST_ASSERT_EQUAL(320, block.widths[0]);
    TEST_ASSERT_FALSE(block.truncated);
    TEST_ASSERT_EQUAL((112 + 366) / 2 - 20, block.top);  // vertically centred in the band
}

void test_lines_are_balanced_without_an_orphan()
{
    // 13 characters: the first fitting split would leave one character alone on line two.
    const MuseTextBlock block = museLayout("我这个月的订阅有哪些要续费", {32, 24}, 112, 366, MuseRound(), advance);
    TEST_ASSERT_EQUAL(2, block.lines.size());
    TEST_ASSERT_TRUE(block.widths[1] >= 4 * 32);
    TEST_ASSERT_EQUAL_STRING("我这个月的订阅有哪些要续费", join(block).c_str());
}

void test_punctuation_never_starts_a_line()
{
    const MuseTextBlock block =
        museLayout("明天上午九点有组会，十点前要交周报，下午三点体检。", {32, 24}, 112, 366, MuseRound(), advance);
    for (const std::string& line : block.lines) {
        TEST_ASSERT_FALSE(line.compare(0, 3, "，") == 0);
        TEST_ASSERT_FALSE(line.compare(0, 3, "。") == 0);
    }
}

void test_long_text_drops_to_the_small_size_before_truncating()
{
    std::string text;
    for (int i = 0; i < 9; ++i) text += "会员订阅将于月底自动续费";  // 108 characters
    const MuseTextBlock block = museLayout(text, {32, 24}, 112, 366, MuseRound(), advance);
    TEST_ASSERT_EQUAL(24, block.px);
    TEST_ASSERT_FALSE(block.truncated);
    TEST_ASSERT_EQUAL_STRING(text.c_str(), join(block).c_str());
}

void test_overflow_truncates_with_an_ellipsis_on_the_last_line()
{
    std::string text;
    for (int i = 0; i < 30; ++i) text += "很长的一段回答";
    const MuseTextBlock block = museLayout(text, {32, 24}, 112, 366, MuseRound(), advance);
    TEST_ASSERT_TRUE(block.truncated);
    TEST_ASSERT_EQUAL(24, block.px);
    const std::string& last = block.lines.back();
    TEST_ASSERT_TRUE(last.compare(last.size() - 3, 3, "…") == 0);
    for (size_t i = 0; i < block.lines.size(); ++i) {
        const int top = block.top + static_cast<int>(i) * block.line_height;
        TEST_ASSERT_TRUE(block.widths[i] <= museChordWidth(MuseRound(), top, top + block.line_height) + 24);
    }
}

void test_ascii_words_stay_whole_and_missing_glyphs_show_a_box()
{
    const MuseTextBlock block = museLayout("回复 PlayStation 退款 X", {32, 24}, 112, 366, MuseRound(), advance);
    bool whole = false;
    for (const std::string& line : block.lines) whole = whole || line.find("PlayStation") != std::string::npos;
    TEST_ASSERT_TRUE(whole);
    TEST_ASSERT_TRUE(join(block).find("□") != std::string::npos);
}

void test_label_is_clipped_to_the_width()
{
    int width = 0;
    const std::string label = museClipLine("问：总结一下这周所有邮件里提到的截止日期", 24, 240, advance, &width);
    TEST_ASSERT_TRUE(width <= 240);
    TEST_ASSERT_TRUE(label.compare(label.size() - 3, 3, "…") == 0);
    TEST_ASSERT_EQUAL_STRING("问：明天有什么安排", museClipLine("问：明天有什么安排", 24, 240, advance).c_str());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_chord_is_widest_at_the_centre);
    RUN_TEST(test_short_text_is_one_large_centred_line);
    RUN_TEST(test_lines_are_balanced_without_an_orphan);
    RUN_TEST(test_punctuation_never_starts_a_line);
    RUN_TEST(test_long_text_drops_to_the_small_size_before_truncating);
    RUN_TEST(test_overflow_truncates_with_an_ellipsis_on_the_last_line);
    RUN_TEST(test_ascii_words_stay_whole_and_missing_glyphs_show_a_box);
    RUN_TEST(test_label_is_clipped_to_the_width);
    return UNITY_END();
}
