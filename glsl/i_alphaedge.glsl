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
