#include <gtest/gtest.h>

#include <array>

namespace {

// GL_FUNC_ADD with RGB=(ONE,ONE), alpha=(ZERO,ONE_MINUS_SRC_ALPHA).
// These are arbitrary already-encoded fragments: this checks the documented
// accumulation/resolve contract, not a CPU copy of any material shader.
// Actual shader output and model routing are covered by ShaderOutputTests.
struct Fragment {
    std::array<double, 3> weightedRGB;
    double alpha;
    double weight;
};

std::array<double, 3> composite(const std::array<Fragment, 2> &fragments) {
    std::array<double, 3> accumulated {0.0, 0.0, 0.0};
    double revealage = 1.0;
    double weight = 0.0;
    for (const auto &fragment : fragments) {
        for (int channel = 0; channel < 3; ++channel) {
            accumulated[channel] += fragment.weightedRGB[channel];
        }
        revealage *= 1.0 - fragment.alpha;
        weight += fragment.weight;
    }
    const std::array<double, 3> background {0.1, 0.2, 0.3};
    for (int channel = 0; channel < 3; ++channel) {
        accumulated[channel] = (1.0 - revealage) * accumulated[channel] / weight
                               + revealage * background[channel];
    }
    return accumulated;
}

} // namespace

TEST(OITBlendContract, combines_encoded_fragments_and_background_independently_of_order) {
    Fragment first {{{0.2, 0.1, 0.05}}, 0.25, 0.5};
    Fragment second {{{0.0, 0.3, 0.4}}, 0.5, 0.75};
    auto forward = composite({first, second});
    auto reverse = composite({second, first});
    const std::array<double, 3> expected {0.1375, 0.275, 0.3375};
    for (int channel = 0; channel < 3; ++channel) {
        EXPECT_NEAR(forward[channel], expected[channel], 1e-12);
        EXPECT_NEAR(reverse[channel], expected[channel], 1e-12);
    }
}
