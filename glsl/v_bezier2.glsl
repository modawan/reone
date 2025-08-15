#include "u_dangly.glsl"
#include "u_globals.glsl"
#include "u_locals.glsl"
#include "u_bezier.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV1;
layout(location = 3) in vec2 aUV2;

out vec4 fragPos;
out vec4 fragPosWorld;
out vec3 fragNormalWorld;
out vec2 fragUV1;
out vec2 fragUV2;

void main() {
    vec4 P = vec4(aPosition, 1.0);
    vec4 N = vec4(aNormal, 0.0);

    fragPos = P;

    fragPosWorld = vec4(uBezierP0, 0.0f) + uModel * fragPos;

    mat3 normalMatrix = transpose(mat3(uModelInv));
    fragNormalWorld = normalize(normalMatrix * N.xyz);

    fragUV1 = aUV1;
    fragUV2 = aUV2;

    gl_Position = uProjection * uView * fragPosWorld;
}
