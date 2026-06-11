#include "u_globals.glsl"
#include "u_locals.glsl"

layout(location = 0) in vec3 aPosition;

#ifdef REONE_GLES
out vec4 fragPosWorld;
#endif

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
#ifdef REONE_GLES
    fragPosWorld = worldPos;
    gl_Position = uShadowLightSpace[uShadowLayer] * worldPos;
#else
    gl_Position = worldPos;
#endif
}
