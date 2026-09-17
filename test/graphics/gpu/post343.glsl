#include "u_globals.glsl"
#include "u_locals.glsl"

#include "i_luma.glsl"
#include "i_math.glsl"
#include "i_lighting.glsl"
#include "i_normalmap.glsl"
#include "i_oit.glsl"

#include "i_envmap.glsl"

uniform sampler2D sMainTex;
uniform sampler2D sLightmap;
uniform sampler2D sEnvMap;
uniform sampler2D sNormalMap;
uniform sampler2DArray sBumpMapArray;
uniform samplerCube sEnvMapCube;

in vec4 fragPosWorld;
in vec3 fragNormalWorld;
in vec2 fragUV1;
in vec2 fragUV2;
in mat3 fragTBN;

layout(location = 0) out vec4 fragColor1;
layout(location = 1) out vec4 fragColor2;

vec3 getNormal(vec2 uv) {
    if (isFeatureEnabled(FEATURE_NORMALMAP)) {
        return normalFromNormalMap(sNormalMap, uv, fragTBN);
    } else if (isFeatureEnabled(FEATURE_BUMPMAP)) {
        return normalFromBumpMap(sBumpMapArray, uv, fragTBN);
    } else {
        return normalize(fragNormalWorld);
    }
}

void main() {
    vec2 uv = vec2(uUV * vec3(fragUV1, 1.0));

    vec4 mainTexSample = texture(sMainTex, uv);
    vec3 diffuseColor = mainTexSample.rgb;
    float diffuseAlpha = mainTexSample.a;

    vec3 normal = getNormal(uv);

    float objectAlpha = uColor.a;
    if (!isFeatureEnabled(FEATURE_ENVMAP)) {
        objectAlpha *= diffuseAlpha;
    }
    if (objectAlpha == 0.0) {
        discard;
    }

    vec3 ambient = vec3(0.0);
    vec3 diffuse = uSelfIllumColor.rgb;
    if (isFeatureEnabled(FEATURE_LIGHTMAP)) {
        vec4 lightmapSample = texture(sLightmap, fragUV2);
        diffuse += lightmapSample.rgb;
        if (isFeatureEnabled(FEATURE_WATER)) {
            diffuse = mix(vec3(1.0), diffuse, 0.2);
        }
    } else if (!isFeatureEnabled(FEATURE_STATIC)) {
        ambient += uAmbientColor.rgb * uWorldAmbientColor.rgb;
    }
    for (int i = 0; i < uNumLights; ++i) {
        if (isFeatureEnabled(FEATURE_STATIC) && uLights[i].dynamicType != LIGHT_DYNAMIC_TYPE_ALL) {
            continue;
        }
        vec3 lightPos = uLights[i].position.xyz - fragPosWorld.xyz;
        float lightDist = length(lightPos);
        if (lightDist > uLights[i].radius * uLights[i].radius) {
            continue;
        }
        vec3 lightDir = lightPos / max(1e-4, lightDist);
        float diff = max(0.0, dot(normal, lightDir));
        float attenuation = lightAttenuationQuadratic(uLights[i], lightDist);
        vec3 lightColor = uLights[i].color.rgb;
        if (uLights[i].ambientOnly) {
            ambient += uLights[i].multiplier * attenuation * uAmbientColor.rgb * lightColor;
        } else {
            diffuse += uLights[i].multiplier * diff * attenuation * uDiffuseColor.rgb * lightColor;
        }
    }
    vec3 lighting = min(vec3(1.0), ambient + max(vec3(0.0), diffuse));

    vec3 objectColor = lighting * uColor.rgb * diffuseColor;
    if (isFeatureEnabled(FEATURE_ENVMAP)) {
        vec3 I = normalize(fragPosWorld.xyz - uCameraPosition.xyz);
        vec3 R = reflect(I, normal);
        vec4 envmapSample = sampleEnvMap(sEnvMap, sEnvMapCube, R);
        objectColor += envmapSample.rgb * (1.0 - diffuseAlpha);
    }
    if (isFeatureEnabled(FEATURE_WATER)) {
        objectColor *= uWaterAlpha;
    }

    if (isFeatureEnabled(FEATURE_PREMULALPHA)) {
        // Convert the lit SRC_ALPHA contribution, not unlit texture RGB.
        // Zero-light additive surfaces must contribute neither color nor opacity.
        objectColor *= objectAlpha;
        objectAlpha = clamp(rgbToLuma(objectColor), 0.0, 1.0);
        if (objectAlpha == 0.0) {
            discard;
        }
        objectColor /= objectAlpha;
    }

    float w = OIT_weight(gl_FragCoord.z, objectAlpha);
    fragColor1 = vec4(objectColor * w, objectAlpha);
    fragColor2 = vec4(w);
}
