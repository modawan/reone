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

// The current frame (sMainTex) blended with the up-sampled history of the
// earlier output (sLightmap): history weighted by the ratio, the frame by the
// rest.
void main() {
    vec4 current = texture(sMainTex, fragUV1);
    vec3 history = texture(sLightmap, fragUV1).rgb;
    fragColor = vec4(mix(current.rgb, history, uSpeedBlurRatio), current.a);
}
