#include "u_bones.glsl"
#include "u_dangly.glsl"
#include "u_globals.glsl"
#include "u_locals.glsl"
#include "u_saber.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV1;
layout(location = 3) in vec2 aUV2;
layout(location = 4) in vec3 aBitangent;
layout(location = 5) in vec3 aTangent;
layout(location = 6) in vec3 aTanSpaceNormal;
layout(location = 7) in vec4 aBoneIndices;
layout(location = 8) in vec4 aBoneWeights;

out vec4 fragPos;
out vec4 fragPosWorld;
out vec3 fragNormalWorld;
out vec2 fragUV1;
out vec2 fragUV2;
out mat3 fragTBN;

// A blended mesh's depth, written before the mesh is blended, must match
// the depth it is blended at.
invariant gl_Position;

void main() {
    vec4 P = vec4(aPosition, 1.0);
    vec4 N = vec4(aNormal, 0.0);

    if (isFeatureEnabled(FEATURE_SKIN)) {
        int i1 = max(0, int(aBoneIndices[0]));
        int i2 = max(0, int(aBoneIndices[1]));
        int i3 = max(0, int(aBoneIndices[2]));
        int i4 = max(0, int(aBoneIndices[3]));

        float w1 = aBoneWeights[0];
        float w2 = aBoneWeights[1];
        float w3 = aBoneWeights[2];
        float w4 = aBoneWeights[3];

        P =
            (uBones[i1] * P) * w1 +
            (uBones[i2] * P) * w2 +
            (uBones[i3] * P) * w3 +
            (uBones[i4] * P) * w4;

        N =
            (uBones[i1] * N) * w1 +
            (uBones[i2] * N) * w2 +
            (uBones[i3] * N) * w3 +
            (uBones[i4] * N) * w4;

        fragPos = P;

    } else if (isFeatureEnabled(FEATURE_DANGLY)) {
        fragPos = uDanglyPositions[gl_VertexID];

    } else {
        fragPos = P;
    }

    if (isFeatureEnabled(FEATURE_SHELL)) {
        fragPos.xyz += normalize(N.xyz) * uShellOffset;
    }

    fragPosWorld = uModel * fragPos;

    if (isFeatureEnabled(FEATURE_SABER)) {
        // Each side of the blade is a strip of rows of four along its core,
        // from the blade now to an end of its trail, then a half-blade at that
        // end. The second side runs back to the trail's end; the first runs as
        // far ahead.
        float trail = min((gl_VertexID % 88) / 4, 20) / 20.0;
        if (gl_VertexID < 88) {
            trail = -trail;
        }
        fragPosWorld = mix(fragPosWorld, uSaberTrailEnd * fragPos, trail);
    }

    mat3 normalMatrix = transpose(mat3(uModelInv));
    fragNormalWorld = normalize(normalMatrix * N.xyz);

    fragUV1 = aUV1;
    fragUV2 = aUV2;

    if (isFeatureEnabled(FEATURE_NORMALMAP) || isFeatureEnabled(FEATURE_BUMPMAP)) {
        vec3 T = normalize(normalMatrix * aTangent);
        vec3 B = normalize(normalMatrix * aBitangent);
        vec3 TSN = normalize(normalMatrix * aTanSpaceNormal);
        fragTBN = mat3(T, B, TSN);
    }

    gl_Position = uProjection * uView * fragPosWorld;
}
