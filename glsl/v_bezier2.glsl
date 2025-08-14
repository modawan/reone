#include "u_globals.glsl"
#include "u_locals.glsl"
#include "u_bezier.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV1;
layout(location = 3) in vec2 aUV2;

out vec4 fragPos;
out vec4 fragPosWorld;
out vec2 fragUV1;
out vec2 fragUV2;

void main() {
    /* int segment = gl_InstanceID; */

    /* vec3 factorBegin = vec3(segment / uBezierNumSegments); */
    /* vec3 factorEnd = vec3((segment + 1) / uBezieNumrSegments); */

    /* vec3 beginQ0 = mix(uBezierP0, uBezierP1, factorBegin); */
    /* vec3 beginQ1 = mix(uBezierP1, uBezierP2, factorBegin); */
    /* vec3 begin = mix(beginQ0, beginQ1, factorBegin); */

    /* vec3 endQ0 = mix(uBezierP0, uBezierP1, factorEnd); */
    /* vec3 endQ1 = mix(uBezierP1, uBezierP2, factorEnd); */
    /* vec3 end = mix(endQ0, endQ1, factorEnd); */

    vec3 begin = uBezierP0;
    vec3 end = uBezierP1;

    vec3 vseg = end - begin;
    vec3 center = vseg * 0.5f + begin;
    vec3 dir = normalize(vseg);

    /* vec3 newY, newX, newZ; */
    /* if (dir.x == 0.0f && dir.y == 0.0f) { */
    /*     if (dir.y < 0.0f) { */
    /*         newY = -dir; */
    /*         newX = vec3(-1.0f, 0.0f, 0.0f); */
    /*         newZ = vec3(0.0f, 0.0f, 1.0f); */
    /*     } else { */
    /*         newY = dir; */
    /*         newX = vec3(1.0f, 0.0f, 0.0f); */
    /*         newZ = vec3(0.0f, 0.0f, 1.0f); */
    /*     } */
    /* } else { */
    /*     newY = dir; */
    /*     newZ = cross(newY, vec3(0.0f, 1.0f, 0.0f)); */
    /*     newX = cross(newY, newZ); */
    /* } */

    /* mat4 rotate = mat4( */
    /*         vec4(newX, 0.0f), */
    /*         vec4(newY, 0.0f), */
    /*         vec4(newZ, 0.0f), */
    /*         vec4(0.0f, 0.0f, 0.0f, 1.0f)); */

    mat4 scale = mat4(
            vec4(1.0f, 0.0f, 0.0f, 0.0f),
            vec4(0.0f, length(vseg), 0.0f, 0.0f),
            vec4(0.0f, 0.0f, 1.0f, 0.0f),
            vec4(0.0f, 0.0f, 0.0f, 1.0f));

    vec4 P = vec4(aPosition, 1.0);
    fragPos = vec4(uBezierP0, 1.0f) + P;
    /* fragPosWorld = vec4(center, 1.0f) + scale * uModel * fragPos; */
    fragUV1 = aUV1;
    fragUV2 = aUV2;
    gl_Position = uProjection * uView * fragPosWorld;
}
