#ifndef REONE_ENVMAP_CUBEMAP_LAYERS
#define REONE_ENVMAP_CUBEMAP_LAYERS 4
#endif

uniform samplerCube sIrradianceCubemap0;
uniform samplerCube sIrradianceCubemap1;
uniform samplerCube sIrradianceCubemap2;
uniform samplerCube sIrradianceCubemap3;

uniform samplerCube sPrefilteredCubemap0;
uniform samplerCube sPrefilteredCubemap1;
uniform samplerCube sPrefilteredCubemap2;
uniform samplerCube sPrefilteredCubemap3;

vec3 sampleEnvMapIrradianceCubemap(vec3 R, int envLayer) {
#if REONE_ENVMAP_CUBEMAP_LAYERS > 0
    if (envLayer == 0) {
        return texture(sIrradianceCubemap0, R).rgb;
    }
#endif
#if REONE_ENVMAP_CUBEMAP_LAYERS > 1
    if (envLayer == 1) {
        return texture(sIrradianceCubemap1, R).rgb;
    }
#endif
#if REONE_ENVMAP_CUBEMAP_LAYERS > 2
    if (envLayer == 2) {
        return texture(sIrradianceCubemap2, R).rgb;
    }
#endif
#if REONE_ENVMAP_CUBEMAP_LAYERS > 3
    if (envLayer == 3) {
        return texture(sIrradianceCubemap3, R).rgb;
    }
#endif
    return vec3(0.0);
}

vec3 sampleEnvMapPrefilterCubemap(vec3 R, int envLayer, float lod) {
#if REONE_ENVMAP_CUBEMAP_LAYERS > 0
    if (envLayer == 0) {
        return textureLod(sPrefilteredCubemap0, R, lod).rgb;
    }
#endif
#if REONE_ENVMAP_CUBEMAP_LAYERS > 1
    if (envLayer == 1) {
        return textureLod(sPrefilteredCubemap1, R, lod).rgb;
    }
#endif
#if REONE_ENVMAP_CUBEMAP_LAYERS > 2
    if (envLayer == 2) {
        return textureLod(sPrefilteredCubemap2, R, lod).rgb;
    }
#endif
#if REONE_ENVMAP_CUBEMAP_LAYERS > 3
    if (envLayer == 3) {
        return textureLod(sPrefilteredCubemap3, R, lod).rgb;
    }
#endif
    return vec3(0.0);
}
