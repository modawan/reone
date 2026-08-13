#include "u_locals.glsl"
#include "u_text.glsl"
#include "i_alphaedge.glsl"

uniform sampler2D sMainTex;

in vec2 fragUV1;
flat in int fragInstanceID;

out vec4 fragColor;

void main() {
    vec2 uv = fragUV1 * uTextChars[fragInstanceID].uv.zw + uTextChars[fragInstanceID].uv.xy;
    vec2 glyphSize = uTextChars[fragInstanceID].uv.zw;
    vec2 halfTexel = 0.5 / vec2(textureSize(sMainTex, 0));
    vec2 inset = min(halfTexel, 0.5 * glyphSize);
    uv = clamp(uv, uTextChars[fragInstanceID].uv.xy + inset,
               uTextChars[fragInstanceID].uv.xy + glyphSize - inset);
    vec4 mainTexSample = texture(sMainTex, uv);
    float alpha = sharpenMagnifiedAlpha(sMainTex, uv, mainTexSample.a);
    vec3 objectColor = uColor.rgb * mainTexSample.rgb;
    fragColor = vec4(objectColor, uColor.a * alpha);
}
