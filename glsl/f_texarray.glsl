#include "u_locals.glsl"

uniform sampler2DArray sMainTexArray;

in vec2 fragUV1;

out vec4 fragColor;

void main() {
    vec2 uv = vec2(uUV * vec3(fragUV1, 1.0));
    vec4 mainTexSample = texture(sMainTexArray, vec3(uv, uMainTexFrame));
    fragColor = vec4(uColor.rgb * mainTexSample.rgb, uColor.a * mainTexSample.a);
}
