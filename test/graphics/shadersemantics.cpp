#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::string readShader(std::string_view name) {
    auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "glsl" / name;
    std::ifstream stream(path);
    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

} // namespace

TEST(OITModelShader, transparent_material_keeps_authored_alpha_and_lighting) {
    auto shader = readShader("f_oit_model.glsl");
    ASSERT_FALSE(shader.empty());

    auto textureAlpha = shader.find("float diffuseAlpha = mainTexSample.a;");
    auto applyTextureAlpha = shader.find("objectAlpha *= diffuseAlpha;");
    auto lightLoop = shader.find("for (int i = 0; i < uNumLights; ++i)");
    auto litColor = shader.find("vec3 objectColor = lighting * uColor.rgb * diffuseColor;");
    auto applyAuthoredAlpha = shader.find("objectColor *= objectAlpha;");
    auto encodeContribution = shader.find("objectAlpha = clamp(rgbToLuma(objectColor), 0.0, 1.0);");

    EXPECT_NE(std::string::npos, textureAlpha);
    EXPECT_NE(std::string::npos, applyTextureAlpha);
    EXPECT_NE(std::string::npos, lightLoop);
    EXPECT_NE(std::string::npos, litColor);
    EXPECT_NE(std::string::npos, applyAuthoredAlpha);
    EXPECT_NE(std::string::npos, encodeContribution);
    EXPECT_LT(textureAlpha, applyTextureAlpha);
    EXPECT_LT(lightLoop, litColor);
    EXPECT_LT(litColor, applyAuthoredAlpha);
    EXPECT_LT(applyAuthoredAlpha, encodeContribution);

    // The old path replaced texture alpha with RGB luminance before lighting,
    // which made a blue additive texture visible even under zero light.
    EXPECT_EQ(std::string::npos, shader.find("diffuseAlpha = rgbToLuma(mainTexSample.rgb);"));
}
