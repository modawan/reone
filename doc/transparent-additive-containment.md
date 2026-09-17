# Selective additive-shader containment

This change is a selective rollback, not a newly established retail material
contract. The pre-#343 transparent-model shader is a Reone compatibility
reference, not proof that every additive material should be unlit in KotOR.

## Why this boundary

PR #343 added scene lighting/self-illumination and changed additive contribution
encoding in `f_oit_model.glsl`. Both changes dull the demonstrated lightsaber.
The shipped single-blade model includes ordinary additive planes as well as
specialized saber geometry. Restricting restoration to `FEATURE_SABER` therefore
misses visible blade contributions. That geometry flag must not be applied to
ordinary planes: the vertex shader uses it for indexed displacement.

The bounded public-source/content audit did not establish lost blade emission
metadata or a supported classifier that distinguishes the ordinary blade planes
from every other additive mesh. In particular, additive blending, model class,
texture sharing and names alone do not establish such a classifier.

Instead, restore the earlier lighting and contribution encoding for the existing
`FEATURE_PREMULALPHA` material mode. Keep current lighting/self-illumination for
non-additive transparency. Do not introduce flags, mutate shared resources,
change vertices, alter the OIT consumer, or revert emitter/particle behavior.

## Material contract and limits

| Draw | Resulting bounded rule | Evidence and limitation |
| --- | --- | --- |
| Dedicated additive saber mesh | Earlier additive contribution | Demonstrated Reone regression; sanitized K2 static evidence corroborates default unlit dedicated rendering, not exact blending. |
| Ordinary additive blade plane | Same additive contribution without saber geometry flag | Shipped K1/K2 data and production-path comparisons; exact retail ordinary-plane material state remains unknown. |
| Ordinary opaque hilt | Existing opaque route, unchanged | Separate material/route; no model-wide or shared-texture classification. |
| K2 `mainmenu01/03/04/05`, `floor_01`, `InnerMenu` | Earlier non-lightmapped additive behavior | Candidate original #343 motivating material. The older brighter floor returns; a general requirement for zero contribution in darkness was not independently established. |
| K1 `mainmenu`, `floor_01`, `InnerMenu` | Earlier additive behavior; use an authored lightmap only if resolved | This installation cannot resolve `mainmenu_a00005` in either B or R1. The bound-lightmap rule is verified with synthetic fixtures, not claimed as a live property of this menu. |
| Non-additive alpha material | Current texture alpha, authored opacity, lighting and self-illumination | Preserves meaningful lit-material controls rather than requiring every post-#343 pixel. |
| Other additive/self-illuminated material | Earlier additive behavior; self-illumination does not add a new term in that branch | Explicit rollback tradeoff, not a new universal emission model. |

The #343 prose and test assertion do not by themselves prove retail material
semantics. Its independent emitter allocation/birth/lifecycle/atlas improvements
remain unchanged. #344's own merged changes do not establish another shader
contract; inherited #343 changes must not be counted twice.

## Actual contribution equations

Let texture RGB be `T`, stored texture alpha `t`, uniform material RGB tint `C`,
and authored/controller opacity `A`. This path has no additional per-vertex tint
attribute. Let `Y(T) = dot(T, (0.299, 0.587, 0.114))` and `e = 0.0001`.

For `FEATURE_PREMULALPHA`, use `d = Y(T)` and `D = T / max(e, d)`.
Otherwise use `d = t`, `D = T`. Alpha is `a = A * d`, except envmapped materials
retain `a = A`; zero alpha discards. Production routing does not set the additive
flag for an envmapped material. An artificial combined-feature draw retains the
pre-#343 calculation rather than defining a new production category.

For additive mode, `L = 1` without a lightmap; with a lightmap use its RGB, or
`mix(1, lightmapRGB, 0.2)` for water. These are the earlier rules, not a new
emission inference. For non-additive mode retain the current ambient, authored
self-illumination, lightmap and scene-light evaluation, with static-light filters
and the existing upper clamp: `L = min(1, ambient + max(0, diffuse))`.

Source color is `c = L * C * D`. Existing environment reflection adds
`environmentRGB * (1 - d)`; water multiplies source color by `uWaterAlpha`.
There is no new exposure, brightness factor, forced opacity or blend change.

For each fragment, existing depth attenuation is
`q = 1 / (1 + abs(eyeZ) / 100)` and `w = a * q`. The fragment outputs
`(c*w, a)` and `w`. The transparent framebuffer uses RGBA16F accumulation and
revealage plus R16F accumulated weight. With additive RGB blend and alpha factors
`ZERO, ONE_MINUS_SRC_ALPHA`, initially zero RGB/weight and unit revealage become:

```
S = sum(c_i * w_i)
W = sum(w_i)
r = product(1 - a_i)
alpha = 1 - r
finalRGB = alpha * S / max(0.0001, W) + r * (opaqueRGB + highlightRGB)
finalAlpha = alpha + opaqueAlpha
```

Blend equations are `FUNC_ADD`; transparent depth writes remain disabled. No
claim is made that retail uses this luminance representation or weighted OIT.
Below the epsilon, both encoding and resolve attenuation remain significant;
near-black tests must not pretend exact cancellation or accept blanket discard.

## Validation design

The opt-in GPU suite uses production shader packages, actual Retro/PBR passes,
and minimal redistributable fixtures. Local acceptance additionally uses copied
shipped assets/saves and full-model/per-draw captures; no game assets belong in
this repository. Historical pre/post shaders are provenance-bearing controls.
Non-additive lit controls distinguish this containment from a wholesale old
shader or global fullbright; ordinary-plane routing distinguishes it from the
incomplete dedicated-only correction. Geometry-flag misuse is a separate
negative control.

GPU comparisons use raw shader/UNORM values with framebuffer sRGB conversion and
dithering disabled. This does not assert that authored texture RGB represents
linear radiance, or promise cross-driver pixel identity. See the GPU test README
for framebuffer formats, tolerances, variants and reproduction commands.

The known PBR command-file startup ordering issue is independent. Waiting two
frames before `loadgame` is fixture initialization, not an engine fix. Cursor/map
scaling, dark polygons, Linux acceptance and exact retail fidelity are also
outside this bounded rollback.
