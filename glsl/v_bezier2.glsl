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

// Calculate a point on a Bezier curve defined by P0, P1, and P2.
//
// See https://en.wikipedia.org/wiki/B%C3%A9zier_curve
// "Quadratic curves" for details.
vec3 bezierPoint(float factor) {
    vec3 q0 = mix(uBezierP0, uBezierP1, factor);
    vec3 q1 = mix(uBezierP1, uBezierP2, factor);
    return mix(q0, q1, vec3(factor));
}

// Returns a transform for a billboard to stretch, rotate and translate it to
// form a segment of a beam from point `begin` to point `end`.
mat4 lineTransform(vec3 begin, vec3 end, float orientation) {
    vec3 seg = end - begin;
    float scaleY = length(seg);
    vec3 dir = seg / vec3(scaleY);

    // Change of basis to align Y-axis to dir.
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

    // Billboard mesh is centered at 0, so translate it to the center of the
    // segment.
    vec3 pos = mix(begin, end, vec3(0.5f, 0.5f, 0.5f));

    // Combine change of basis, scale to length of the segment, and translate.
    mat4 rotateTranslate = mat4(
            vec4(newX, 0.0f),
            vec4(newY * scaleY, 0.0f),
            vec4(newZ, 0.0f),
            vec4(pos, 1.0f));

    // Rotate around `dir` to an angle. We render the same segment
    // uBezierNumOrientations times at different angles to give it some volume.
    float orientCos = cos(orientation);
    float orientSin = sin(orientation);
    mat4 orient = mat4(
            vec4(orientCos, 0.0f, -orientSin, 0.0f),
            vec4(0.0f, 1.0f, 0.0f, 0.0f),
            vec4(orientSin, 0.0f, orientCos, 0.0f),
            vec4(0.0f, 0.0f, 0.0f, 1.0f));

    return rotateTranslate * orient;
}

// Transform instanced meshes to form a "beam" that follows a Bezier path from
// uBezierP0 to uBezierP2 using intermediate point uBezierP1 to define a curve.
//
// We divide the curve into uBezierNumSegments straight segments. For each
// segment, render a quad mesh (billboard) and stretch it to match length of the
// segment. Render more than one quad for each segment at different orientations
// (uBezierNumOrientations) to give it some volume.
//
// Total number of instances must be uBezierNumSegments *
// uBezierNumOrientations.
void main() {
    float segment =  gl_InstanceID / uBezierNumOrientations;
    float orientationStep = PI / uBezierNumOrientations;
    float orientation = orientationStep
        * (gl_InstanceID % uBezierNumOrientations);

    float factorBegin = segment / uBezierNumSegments;
    float factorEnd = (segment + 1.0f) / uBezierNumSegments;
    float factorNextEnd = (segment + 2.0f) / uBezierNumSegments;

    vec3 begin = bezierPoint(factorBegin);
    vec3 end = bezierPoint(factorEnd);
    vec3 nextEnd = bezierPoint(factorNextEnd);

    mat4 transform = lineTransform(begin, end, orientation) * uModel;
    mat4 nextTransform = lineTransform(end, nextEnd, orientation) * uModel;

    fragPos = vec4(aPosition, 1.0);

    // Stitch vertices #2 and #3 to vertices #1 and #2 of the next segment. This
    // avoids gaps and overlaps when segments join at an angle. We expect a mesh
    // from kBillboardVertices, so vertex coordinates are hard-coded.
    if (gl_VertexID == 2) {
        fragPosWorld = nextTransform * vec4(0.5f, -0.5f, 0.0f, 1.0f);
    } else if (gl_VertexID == 3) {
        fragPosWorld = nextTransform * vec4(-0.5f, -0.5f, 0.0f, 1.0f);
    } else {
        fragPosWorld = transform * fragPos;
    }

    fragUV1 = aUV1;
    fragUV2 = aUV2;

    gl_Position = uProjection * uView * fragPosWorld;
}
