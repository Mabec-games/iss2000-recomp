// Unit test for the netplay room-code helpers in src/iss_words.h.
// Build: c++ -std=c++20 -Isrc tools/test_netplay_engine.cpp -o build/test_netplay_engine
#include "iss_words.h"

#include <cstdio>
#include <cstdlib>
#include <string>

// Unlike assert(), CHECK stays active in Release (NDEBUG) builds, so the test can't pass silently.
static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static void test_word_generation() {
    std::printf("[1/1] Testing word generator and normalizer...\n");
    std::string w1 = iss::net::generate_words();
    std::printf("  Generated word code: %s\n", w1.c_str());
    CHECK(!w1.empty());

    // generate_words() returns 4 soccer words: 3 hyphens.
    int hyphens = 0;
    for (char c : w1) if (c == '-') hyphens++;
    CHECK(hyphens == 3);

    // A generated code must survive normalization unchanged.
    CHECK(iss::net::normalize_words(w1) == w1);

    // normalize_words() collapses separators and uppercases.
    CHECK(iss::net::normalize_words("   Falcon  _ Orbit - Tempo  ") == "FALCON-ORBIT-TEMPO");
    CHECK(iss::net::normalize_words("goal,pass\tcorner keeper") == "GOAL-PASS-CORNER-KEEPER");
    CHECK(iss::net::normalize_words("") == "");
}

int main() {
    std::printf("=========================================\n");
    std::printf(" ISS 2000 Netplay Engine Unit Test\n");
    std::printf("=========================================\n");

    test_word_generation();

    if (g_failures) {
        std::printf("\n>>> %d CHECK(S) FAILED <<<\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("\n>>> ALL ENGINE UNIT TESTS PASSED! <<<\n");
    return EXIT_SUCCESS;
}
