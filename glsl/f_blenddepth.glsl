#include "u_locals.glsl"

uniform sampler2D sMainTex;

in vec2 fragUV1;

void main() {
    vec2 uv = vec2(uUV * vec3(fragUV1, 1.0));
    if (texture(sMainTex, uv).a * uColor.a == 0.0) {
        discard;
    }
}
