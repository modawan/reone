#include "u_locals.glsl"
#include "i_alphaedge.glsl"

uniform sampler2D sMainTex;

in vec2 fragUV1;

out vec4 fragColor;

void main() {
    vec2 uv = vec2(uUV * vec3(fragUV1, 1.0));
    vec4 mainTexSample = texture(sMainTex, uv);
    float alpha = sharpenMagnifiedAlpha(sMainTex, uv, mainTexSample.a);
    fragColor = vec4(uColor.rgb * mainTexSample.rgb, uColor.a * alpha);
}
