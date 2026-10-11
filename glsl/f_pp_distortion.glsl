/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "u_screeneffect.glsl"

uniform sampler2D sMainTex;
uniform sampler2D sLightmap;
uniform sampler2D sEnvMap;
uniform sampler2D sNormalMap;

noperspective in vec2 fragUV1;

out vec4 fragColor;

// The overlay reaches this many pixels past every edge of the screen, and the
// distortion texture shifts the shrunken frame by up to this fraction of it.
const float OVERLAY_MARGIN = 25.0;
const float DISTORTION_REACH = 0.15625;

// The finished frame (sMainTex) with an overlay texture (sNormalMap) laid over
// it by the overlay's alpha. Wherever the overlay is not fully transparent it
// also shows the 128x128 copy of the frame (sLightmap), displaced by the
// signed normals of the distortion texture (sEnvMap).
void main() {
    vec4 frame = texture(sMainTex, fragUV1);
    vec2 pixel = fragUV1 * uScreenResolution;
    vec2 uv = (pixel + OVERLAY_MARGIN) / (uScreenResolution + 2.0 * OVERLAY_MARGIN);
    vec3 normal = 2.0 * texture(sEnvMap, uv).rgb - 1.0;
    vec2 distortedUV = DISTORTION_REACH * normal.xy + uv * normal.z;
    vec3 shrunk = texture(sLightmap, clamp(distortedUV, 0.0, 1.0)).rgb;
    vec4 overlay = texture(sNormalMap, uv);
    float shown = step(0.500001, overlay.a + 0.5);
    vec3 color = clamp(shrunk * shown + overlay.rgb, 0.0, 1.0);
    fragColor = vec4(mix(frame.rgb, color, overlay.a), frame.a);
}
