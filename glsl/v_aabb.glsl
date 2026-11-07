#include "u_aabb.glsl"
#include "u_globals.glsl"

layout(location = 0) in vec3 aPosition;

out vec4 fragPosWorld;

void main() {
    fragPosWorld = uCorners[gl_VertexID];

    gl_Position = uProjection * uView * fragPosWorld;
}
