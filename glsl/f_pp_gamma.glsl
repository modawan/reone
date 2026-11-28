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

uniform sampler2D sMainTex;
uniform float uGammaExponent;

noperspective in vec2 fragUV1;

out vec4 fragColor;

void main() {
    vec3 color = texture(sMainTex, fragUV1).rgb;
    fragColor = vec4(pow(color, vec3(uGammaExponent)), 1.0);
}
