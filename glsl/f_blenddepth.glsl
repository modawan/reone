#include "u_locals.glsl"

uniform sampler2D sMainTex;
uniform sampler2DArray sMainTexArray;

in vec2 fragUV1;

void main() {
    vec2 uv = vec2(uUV * vec3(fragUV1, 1.0));
    float alpha = isFeatureEnabled(FEATURE_MAINTEXARRAY)
                      ? texture(sMainTexArray, vec3(uv, uMainTexFrame)).a
                      : texture(sMainTex, uv).a;
    if (alpha * uColor.a == 0.0) {
        discard;
    }
}
