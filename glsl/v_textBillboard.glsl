#include "u_globals.glsl"
#include "u_locals.glsl"
#include "u_text.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUV1;

out vec2 fragUV1;
flat out int fragInstanceID;

void main() {
    vec3 right = vec3(uView[0][0], uView[1][0], uView[2][0]);
    vec3 up = vec3(uView[0][1], uView[1][1], uView[2][1]);

    vec3 pos = aPosition;
    pos.x += uTextChars[gl_InstanceID].posScale[0] + aPosition.x * uTextChars[gl_InstanceID].posScale[2];
    pos.y += uTextChars[gl_InstanceID].posScale[1] + aPosition.y * uTextChars[gl_InstanceID].posScale[3];

    // Apply scale transform and rotate to face the camera.
    vec3 modelPos = vec3(uModel[3]) +  right * uModel[0][0] * pos.x + up * uModel[1][1] * pos.y;
    vec4 P = vec4(modelPos, 1.0f);

    gl_Position = uProjection * uView * P;
    fragUV1 = aUV1;
    fragInstanceID = gl_InstanceID;
}
