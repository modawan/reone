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

// Coverage reconstruction is only valid while magnifying cutout artwork.
// At native size or during minification the stored coverage is already the
// best available signal, and thresholding it would discard detail.
float sharpenMagnifiedAlpha(sampler2D image, vec2 uv, float alpha) {
    vec2 sourceFootprint = fwidth(uv) * vec2(textureSize(image, 0));
    float largestFootprint = max(sourceFootprint.x, sourceFootprint.y);
    float sharpenAmount = 1.0 - smoothstep(0.75, 1.0, largestFootprint);

    float alphaWidth = max(0.5 * fwidth(alpha), 1.0 / 255.0);
    float sharpened = smoothstep(0.5 - alphaWidth, 0.5 + alphaWidth, alpha);
    return mix(alpha, sharpened, sharpenAmount);
}
