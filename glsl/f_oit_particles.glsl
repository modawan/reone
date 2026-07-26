#include "u_globals.glsl"
#include "u_locals.glsl"
#include "u_particles.glsl"

#include "i_luma.glsl"
#include "i_oit.glsl"

uniform sampler2D sMainTex;

in vec2 fragUV1;
flat in int fragInstanceID;

layout(location = 0) out vec4 fragColor1;
layout(location = 1) out vec4 fragColor2;

vec4 cubicWeights(float fraction) {
    float t = clamp(fraction, 0.0, 1.0);
    float t2 = t * t;
    float t3 = t2 * t;
    return vec4(
        -0.5 * t + t2 - 0.5 * t3,
        1.0 - 2.5 * t2 + 1.5 * t3,
        0.5 * t + 2.0 * t2 - 1.5 * t3,
        -0.5 * t2 + 0.5 * t3);
}

void enhancedAtlasUV(
    out vec2 uv,
    out vec2 frameMinUV,
    out vec2 frameMaxUV,
    out ivec2 textureDimensions) {

    textureDimensions = max(textureSize(sMainTex, 0), ivec2(1));
    ivec2 gridSize = clamp(uGridSize, ivec2(1), textureDimensions);
    int frameCount = gridSize.x * gridSize.y;
    int frame = clamp(int(uParticles[fragInstanceID].positionFrame.w), 0, frameCount - 1);
    ivec2 frameCoord = ivec2(frame % gridSize.x, frame / gridSize.x);

    vec2 cellMin = vec2(frameCoord) / vec2(gridSize);
    vec2 cellMax = vec2(frameCoord + ivec2(1)) / vec2(gridSize);
    vec2 cellCenter = 0.5 * (cellMin + cellMax);
    vec2 halfTexel = 0.5 / vec2(textureDimensions);
    frameMinUV = min(cellMin + halfTexel, cellCenter);
    frameMaxUV = max(cellMax - halfTexel, cellCenter);

    vec2 localUV = clamp(fragUV1, vec2(0.0), vec2(1.0));
    uv = (vec2(frameCoord) + localUV) / vec2(gridSize);
    uv = clamp(uv, frameMinUV, frameMaxUV);
}

vec4 sampleCubicAtlas(
    vec2 uv,
    vec2 frameMinUV,
    vec2 frameMaxUV,
    ivec2 textureDimensions) {

    vec2 pixel = uv * vec2(textureDimensions) - 0.5;
    vec2 basePixel = floor(pixel);
    vec2 fraction = fract(pixel);
    vec4 weightsX = cubicWeights(fraction.x);
    vec4 weightsY = cubicWeights(fraction.y);

    vec4 cubicSample = vec4(0.0);
    float weightSum = 0.0;
    vec4 sample00 = vec4(0.0);
    vec4 sample10 = vec4(0.0);
    vec4 sample01 = vec4(0.0);
    vec4 sample11 = vec4(0.0);
    vec4 neighborhoodMin = vec4(1.0);
    vec4 neighborhoodMax = vec4(0.0);

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            vec2 samplePixel = basePixel + vec2(x - 1, y - 1);
            vec2 sampleUV = (samplePixel + 0.5) / vec2(textureDimensions);
            sampleUV = clamp(sampleUV, frameMinUV, frameMaxUV);
            vec4 value = texture(sMainTex, sampleUV);
            float weight = weightsX[x] * weightsY[y];
            cubicSample += value * weight;
            weightSum += weight;
            neighborhoodMin = min(neighborhoodMin, value);
            neighborhoodMax = max(neighborhoodMax, value);

            if (x == 1 && y == 1) {
                sample00 = value;
            } else if (x == 2 && y == 1) {
                sample10 = value;
            } else if (x == 1 && y == 2) {
                sample01 = value;
            } else if (x == 2 && y == 2) {
                sample11 = value;
            }
        }
    }

    vec4 bilinearSample = mix(
        mix(sample00, sample10, fraction.x),
        mix(sample01, sample11, fraction.x),
        fraction.y);
    vec4 reconstructed = cubicSample / max(abs(weightSum), 0.0001);
    reconstructed = clamp(reconstructed, neighborhoodMin, neighborhoodMax);
    return mix(
        bilinearSample,
        reconstructed,
        clamp(uParticleReconstructionStrength, 0.0, 1.0));
}

void decodeParticleSample(
    vec4 sampleValue,
    out vec3 sampleColor,
    out float sampleAlpha) {

    sampleColor = sampleValue.rgb;
    float storedAlpha = clamp(sampleValue.a, 0.0, 1.0);
    float luminance = clamp(rgbToLuma(sampleColor), 0.0, 1.0);

    if (!isFeatureEnabled(FEATURE_PREMULALPHA)) {
        sampleAlpha = storedAlpha;
        return;
    } else if (uParticleAlphaMode == 1) {
        sampleAlpha = storedAlpha;
    } else if (uParticleAlphaMode == 2) {
        sampleAlpha = luminance;
        sampleColor *= 1.0 / max(0.0001, luminance);
    } else if (uParticleAlphaMode == 3) {
        sampleAlpha = min(storedAlpha, luminance);
        if (luminance <= storedAlpha) {
            sampleColor *= 1.0 / max(0.0001, luminance);
        }
    } else {
        sampleAlpha = luminance;
        sampleColor *= 1.0 / max(0.0001, luminance);
    }

    sampleAlpha = pow(
        clamp(sampleAlpha, 0.0, 1.0),
        max(uParticleAlphaExponent, 0.0001));
}

float analyticTrailEnvelope(vec2 localUV) {
    if (uParticleTrailMode != 1 ||
        uMotionBlur == 0 ||
        uParticleTrailCoreIntensity <= 0.0) {
        return 0.0;
    }

    vec2 centered = 2.0 * localUV - 1.0;
    vec2 inside = max(vec2(1.0) - abs(centered), vec2(0.0));
    float crossSection = inside.x * inside.x;
    crossSection *= crossSection;
    float endTaper = inside.y * inside.y;
    return clamp(uParticleTrailCoreIntensity, 0.0, 1.0) * crossSection * endTaper;
}

void main() {
    bool enhancedPolicy =
        uParticleReconstructionMode != 0 ||
        uParticleAlphaMode != 0 ||
        (uParticleTrailMode != 0 && uMotionBlur != 0) ||
        uParticleDiagnosticMode != 0 ||
        abs(uParticleAlphaExponent - 1.0) > 0.0001;
    if (!enhancedPolicy) {
        float oneOverGridX = 1.0 / uGridSize.x;
        float oneOverGridY = 1.0 / uGridSize.y;

        vec2 uv = fragUV1;
        uv.x *= oneOverGridX;
        uv.y *= oneOverGridY;

        int frame = int(uParticles[fragInstanceID].positionFrame.w);
        if (frame > 0) {
            uv.y += oneOverGridY * (frame / uGridSize.x);
            uv.x += oneOverGridX * (frame % uGridSize.x);
        }

        vec4 mainTexSample = texture(sMainTex, uv);
        vec3 mainTexColor = mainTexSample.rgb;
        float mainTexAlpha = mainTexSample.a;
        if (isFeatureEnabled(FEATURE_PREMULALPHA)) {
            mainTexAlpha = rgbToLuma(mainTexSample.rgb);
            mainTexColor *= 1.0 / max(0.0001, mainTexAlpha);
        }
        vec3 objectColor = uParticles[fragInstanceID].color.rgb * mainTexColor;
        float objectAlpha = uParticles[fragInstanceID].color.a * mainTexAlpha;
        if (objectAlpha == 0.0) {
            discard;
        }

        float w = OIT_weight(gl_FragCoord.z, objectAlpha);
        fragColor1 = vec4(objectColor * w, objectAlpha);
        fragColor2 = vec4(w);
        return;
    }

    vec2 enhancedUV;
    vec2 frameMinUV;
    vec2 frameMaxUV;
    ivec2 textureDimensions;
    enhancedAtlasUV(enhancedUV, frameMinUV, frameMaxUV, textureDimensions);

    vec4 mainTexSample = uParticleReconstructionMode == 1
                             ? sampleCubicAtlas(
                                   enhancedUV,
                                   frameMinUV,
                                   frameMaxUV,
                                   textureDimensions)
                             : texture(sMainTex, enhancedUV);
    vec3 mainTexColor;
    float mainTexAlpha;
    decodeParticleSample(mainTexSample, mainTexColor, mainTexAlpha);

    float trailCore = analyticTrailEnvelope(fragUV1);
    mainTexAlpha += trailCore * mainTexAlpha * (1.0 - mainTexAlpha);

    vec3 objectColor;
    float objectAlpha;
    if (uParticleDiagnosticMode == 1) {
        objectColor = mainTexColor;
        objectAlpha = mainTexAlpha;
    } else if (uParticleDiagnosticMode == 2) {
        objectColor = vec3(mainTexAlpha);
        objectAlpha = 1.0;
    } else if (uParticleDiagnosticMode == 3) {
        objectColor = uParticles[fragInstanceID].color.rgb;
        objectAlpha = uParticles[fragInstanceID].color.a;
    } else {
        objectColor = uParticles[fragInstanceID].color.rgb * mainTexColor;
        objectAlpha = uParticles[fragInstanceID].color.a * mainTexAlpha;
    }
    if (objectAlpha == 0.0) {
        discard;
    }

    float w = OIT_weight(gl_FragCoord.z, objectAlpha);
    fragColor1 = vec4(objectColor * w, objectAlpha);
    fragColor2 = vec4(w);
}
