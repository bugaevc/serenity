#include <AK/TestSuite.h>

#include <AK/Utf8View.h>

TEST_CASE(decode_ascii)
{
    Utf8View utf8 { "Hello World!11" };
    u32 expected[] = { 72, 101, 108, 108, 111, 32, 87, 111, 114, 108, 100, 33, 49, 49 };
    size_t expected_size = sizeof(expected) / sizeof(expected[0]);

    size_t i = 0;
    for (u32 codepoint : utf8) {
        ASSERT(i < expected_size);
        EXPECT_EQ(codepoint, expected[i]);
        i++;
    }
    EXPECT_EQ(i, expected_size);
}

TEST_CASE(decode_utf8)
{
    Utf8View utf8 { "Привет, мир! 😀 γειά σου κόσμος こんにちは世界" };
    u32 expected[] = { 1055, 1088, 1080, 1074, 1077, 1090, 44, 32, 1084, 1080, 1088, 33, 32, 128512, 32, 947, 949, 953, 940, 32, 963, 959, 965, 32, 954, 972, 963, 956, 959, 962, 32, 12371, 12435, 12395, 12385, 12399, 19990, 30028 };
    size_t expected_size = sizeof(expected) / sizeof(expected[0]);

    size_t i = 0;
    for (u32 codepoint : utf8) {
        ASSERT(i < expected_size);
        EXPECT_EQ(codepoint, expected[i]);
        i++;
    }
    EXPECT_EQ(i, expected_size);
}

TEST_MAIN(UTF8)
