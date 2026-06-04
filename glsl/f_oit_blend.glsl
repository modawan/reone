uniform sampler2D sMainTex;
uniform sampler2D sHilights;
uniform sampler2D sOITAccum;
uniform sampler2D sOITRevealage;

#ifndef REONE_WEB
noperspective in vec2 fragUV1;
#else
in vec2 fragUV1;
#endif

out vec4 fragColor;

void main() {
    vec4 mainTexSample = texture(sMainTex, fragUV1);
    vec4 hilightsSample = texture(sHilights, fragUV1);
    vec4 oitAccumSample = texture(sOITAccum, fragUV1);
    vec4 oitRevealageSample = texture(sOITRevealage, fragUV1);

    vec3 accumColor = oitAccumSample.rgb;
    float accumWeight = oitRevealageSample.r;
    float revealage = oitAccumSample.a;
    vec3 opaqueColor = mainTexSample.rgb + hilightsSample.rgb;
    float opaqueLuma = dot(opaqueColor, vec3(0.299, 0.587, 0.114));

    // Uninitialized transparent targets (alpha/weight zero): show opaque G-buffer.
    if (accumWeight < 0.0001 && revealage < 0.0001) {
        fragColor = vec4(opaqueColor, max(mainTexSample.a, 1.0));
        return;
    }

    // Surfaces composited only through OIT (opaque G-buffer empty here).
    if (accumWeight > 0.0001 && opaqueLuma < 0.001) {
        fragColor = vec4(accumColor.rgb / max(0.0001, accumWeight), 1.0 - revealage + mainTexSample.a);
        return;
    }

    float alpha = 1.0 - revealage;
    vec3 color = alpha * (accumColor.rgb / max(0.0001, accumWeight)) + (1.0 - alpha) * opaqueColor;

    fragColor = vec4(color, alpha + mainTexSample.a);
}
