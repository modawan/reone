#include "u_dangly.glsl"
#include "u_globals.glsl"
#include "u_locals.glsl"
#include "u_bezier.glsl"
#include "i_math.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV1;
layout(location = 3) in vec2 aUV2;

out vec4 fragPos;
out vec4 fragPosWorld;
out vec3 fragNormalWorld;
out vec2 fragUV1;
out vec2 fragUV2;

vec3 bezierPoint(float factor) {
    vec3 q0 = mix(uBezierP0, uBezierP1, factor);
    vec3 q1 = mix(uBezierP1, uBezierP2, factor);
    return mix(q0, q1, vec3(factor));
}

mat4 lineTransform(vec3 begin, vec3 end, float orientation) {
    vec3 seg = end - begin;
    vec3 dir = normalize(seg);
    float scaleY = length(seg);

    vec3 newY, newX, newZ;
    if ((1.0f - abs(dir.z)) < 0.001) {
        // when a direction is parallel to the UP unit vector, the cross product
        // with UP is invalid.
        if (dir.z < 0.0f) {
            newY = -dir;
            newX = vec3(1.0f, 0.0f, 0.0f);
            newZ = vec3(0.0f, 0.0f, -1.0f);
        } else {
            newY = dir;
            newX = vec3(1.0f, 0.0f, 0.0f);
            newZ = vec3(0.0f, 0.0f, 1.0f);
        }
    } else {
        newY = dir;
        newX = normalize(cross(newY, vec3(0.0f, 0.0f, 1.0f)));
        newZ = normalize(cross(newY, newX));
    }

    vec3 pos = mix(begin, end, vec3(0.5f, 0.5f, 0.5f));

    mat4 rotateTranslate = mat4(
            vec4(newX, 0.0f),
            vec4(newY * scaleY, 0.0f),
            vec4(newZ, 0.0f),
            vec4(pos, 1.0f));

    float orientCos = cos(orientation);
    float orientSin = sin(orientation);
    mat4 orient = mat4(
            vec4(orientCos, 0.0f, -orientSin, 0.0f),
            vec4(0.0f, 1.0f, 0.0f, 0.0f),
            vec4(orientSin, 0.0f, orientCos, 0.0f),
            vec4(0.0f, 0.0f, 0.0f, 1.0f));

    return rotateTranslate * orient;
}

void main() {
    float segment =  gl_InstanceID / uBezierNumOrientations;
    float orientationStep = PI / uBezierNumOrientations;
    float orientation = orientationStep * (gl_InstanceID % uBezierNumOrientations);

    float factorBegin = segment / uBezierNumSegments;
    float factorEnd = (segment + 1.0f) / uBezierNumSegments;
    float factorNextEnd = (segment + 2.0f) / uBezierNumSegments;

    vec3 begin = bezierPoint(factorBegin);
    vec3 end = bezierPoint(factorEnd);
    vec3 nextEnd = bezierPoint(factorNextEnd);

    fragPos = vec4(aPosition, 1.0);
    fragPosWorld = transform * uModel * fragPos;

    fragUV1 = aUV1;
    fragUV2 = aUV2;

    gl_Position = uProjection * uView * fragPosWorld;
}
