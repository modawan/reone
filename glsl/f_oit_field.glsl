#include "u_globals.glsl"
#include "u_locals.glsl"

#include "i_luma.glsl"
#include "i_math.glsl"
#include "i_normalmap.glsl"
#include "i_oit.glsl"

uniform sampler2DArray sMainArrayTex;
uniform int uMainArrayFrame;

in vec4 fragPosWorld;
in vec3 fragNormalWorld;
in vec2 fragUV1;

layout(location = 0) out vec4 fragColor1;
layout(location = 1) out vec4 fragColor2;

vec3 getNormal(vec2 uv) {
    return normalize(fragNormalWorld);
}

void main() {
    vec2 uv = vec2(uUV * vec3(fragUV1, 1.0));

    vec4 mainTexSample = texture(sMainArrayTex, vec3(uv, uMainArrayFrame));
    vec3 diffuseColor = mainTexSample.rgb;
    float diffuseAlpha = rgbToLuma(mainTexSample.rgb);
    diffuseColor *= 1.0 / max(0.0001, diffuseAlpha);

    vec3 normal = getNormal(uv);

    float objectAlpha = uColor.a * diffuseAlpha;
    if (objectAlpha == 0.0) {
        discard;
    }

    vec3 objectColor = uColor.rgb * diffuseColor;

    float w = OIT_weight(gl_FragCoord.z, objectAlpha);
    fragColor1 = vec4(objectColor * w, objectAlpha);
    fragColor2 = vec4(w);
}
