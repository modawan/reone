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

noperspective in vec2 fragUV1;

out vec4 fragColor;

// Luminance weights the desaturation pulls toward, and how many times the
// noise texture repeats down the screen.
const vec3 SATURATION_LUMINANCE = vec3(0.3086, 0.6094, 0.0820);
const float SCAN_NOISE_REPEATS = 7.5;

// The finished frame (sMainTex) with scan noise added and then desaturated
// and tinted. Scan noise is the first column of the noise texture (sLightmap)
// stretched across the screen and repeated down it; the sum is kept in range.
// Each output channel is its modulation times the mix of luminance and that
// channel by the saturation, with every weight capped at 1 except blue's own.
void main() {
    vec4 frame = texture(sMainTex, fragUV1);
    vec3 color = frame.rgb;
    if (uVideoScanNoiseEnabled > 0.5) {
        vec2 noiseUV = vec2(0.0, SCAN_NOISE_REPEATS * (1.0 - fragUV1.y));
        color = clamp(color + texture(sLightmap, noiseUV).rgb, 0.0, 1.0);
    }
    if (uVideoSaturationEnabled > 0.5) {
        vec3 gray = (1.0 - uVideoSaturation) * SATURATION_LUMINANCE;
        vec3 red = min(uVideoModulationR * (gray + vec3(uVideoSaturation, 0.0, 0.0)), vec3(1.0));
        vec3 green = min(uVideoModulationG * (gray + vec3(0.0, uVideoSaturation, 0.0)), vec3(1.0));
        vec3 blue = uVideoModulationB * (gray + vec3(0.0, 0.0, uVideoSaturation));
        blue.xy = min(blue.xy, vec2(1.0));
        color = vec3(dot(color, red), dot(color, green), dot(color, blue));
    }
    fragColor = vec4(color, frame.a);
}
