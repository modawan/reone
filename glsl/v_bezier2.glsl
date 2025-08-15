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


    vec3 vseg = uBezierP2 - uBezierP0;
    vec3 dir = normalize(vseg);

    vec3 newY, newX, newZ;
    if (dir.x == 0.0f && dir.y == 1.0f) {
        // when a direction is parallel to the UP unit vector, the cross product
        // with UP is invalid.
        if (dir.y < 0.0f) {
            newY = -dir;
            newX = vec3(-1.0f, 0.0f, 0.0f);
            newZ = vec3(0.0f, 0.0f, 1.0f);
        } else {
            newY = dir;
            newX = vec3(1.0f, 0.0f, 0.0f);
            newZ = vec3(0.0f, 0.0f, 1.0f);
        }
    } else {
        newY = dir;
        newZ = cross(newY, vec3(0.0f, 1.0f, 0.0f));
        newX = cross(newY, newZ);
    }

    float scaleY = length(vseg);

    mat4 transform = mat4(
            vec4(newX, 0.0f),
            vec4(newY * scaleY, 0.0f),
            vec4(newZ, 0.0f),
            vec4(uBezierP0, 1.0f));

    fragPos = P;
    fragPosWorld = transform * uModel * fragPos;

    mat3 normalMatrix = transpose(mat3(uModelInv));
    fragNormalWorld = normalize(normalMatrix * N.xyz);

    fragUV1 = aUV1;
    fragUV2 = aUV2;

    gl_Position = uProjection * uView * fragPosWorld;
}
