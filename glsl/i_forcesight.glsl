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

// Force Sight: every surface turns grey (the luminance of its lit colour,
// darkened), and glowing bodies take their glow colour brightened by the
// luminance of their texture.
const vec3 FORCE_SIGHT_LUMINANCE = vec3(0.30, 0.59, 0.11);
const float FORCE_SIGHT_GREY = 0.65;
const float FORCE_SIGHT_GLOW_ALPHA = 0.8;

vec3 forceSightGrey(vec3 litColor) {
    return vec3(dot(litColor, FORCE_SIGHT_LUMINANCE) * FORCE_SIGHT_GREY);
}

// The glow colour to add over the scene, already weighted by its alpha and
// kept in range.
vec3 forceSightGlow(vec3 texColor, vec4 glow) {
    return clamp(glow.rgb * (glow.a + dot(texColor, FORCE_SIGHT_LUMINANCE)), 0.0, 1.0);
}
