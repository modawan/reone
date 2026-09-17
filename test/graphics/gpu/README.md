# Transparent material framebuffer regression

This opt-in runner creates a hidden SDL OpenGL 4.0 core context and reads actual
GPU output. It uses redistributable synthetic models, not game assets or saves.
Context failure is a failed run, never a passing skip. GPU availability on CI is
not assumed; Windows results do not establish Linux acceptance.

Configure with the repository's normal dependency/toolchain setup and
`-DBUILD_TESTS=ON -DBUILD_GPU_TESTS=ON`, then:

```powershell
cmake --build build-material-repair --config Release --target shader-output-tests
ctest --test-dir build-material-repair -C Release -R '^ShaderOutputTests$' --output-on-failure
```

The target generates separate production packages under
`build-material-repair/gpu-shaders`. The production `shaderpack` executable
packages each source directory, and production `Resources` and `Shaders` load,
preprocess and compile them. Every process starts fresh providers/programs.
Rebuilding after a GLSL edit regenerates packages; no hot reload is assumed.

| Package | Purpose |
|---|---|
| current | Shader under test, including selective additive containment |
| pre343 | Frozen public pre-343 fragment reference |
| post343 | Frozen public post-343 fragment reference |
| dedicated-only | Preserved incomplete candidate D, a required negative |
| lighting-only | Post-343 with only additive lighting bypassed |
| global-fullbright | Post-343 with all material lighting forced to one |
| wrong-geometry | Current fragment plus forbidden additive reuse of saber vertex processing |

The public references are `glsl/f_oit_model.glsl` from
`5fa20f1f47335b65f017b95e17e1e836ecdbb772` (pre-343) and
`0c35810b0e2edcdc6ae6a15e133524272ddce218` (post-343). The D fixture is the
preserved uncommitted dedicated-only proposal from that base. All normal
variants retain current includes, vertex shaders and resolve; only the explicitly
wrong geometry variant changes `v_model.glsl`.

The test contract distinguishes source lighting from contribution encoding.
The selected containment restores the pre-343 additive contribution, including
ordinary planes, and keeps ordinary non-additive alpha materials scene-lit with
authored alpha and self illumination. Pre-343 is a compatibility reference for
additive draws, not unquestioned retail truth. Post-343 is used only for the
retained lit-material behavior, supported by explicit light/alpha sensitivity
checks. The original menu's additive appearance may therefore return to its
earlier result; these tests do not disguise that containment tradeoff as a
preserved improvement.

There are two complementary fixture levels:

- Direct material samples exercise RGB/alpha/tint/opacity, backgrounds, lighting,
  self illumination, zero/near-zero values and lightmap/water combinations.
- `ModelFixture` creates actual `Model`, `SceneGraph`, `ModelSceneNode` and
  `MeshSceneNode` objects. Authored texture properties, alpha controllers and
  geometry specialization determine real Retro/PBR material routes. Passive
  observers record routes, final feature masks, shader selection and transforms.
  The fixture never manually sets the saber flag to obtain ordinary shading.

Generic cross-planes cover ordinary and dedicated geometry independently and
together, with blue/red/green samples and front/oblique/side orientations. Their
shared texture is not mutated per consumer. Opaque siblings retain their opaque
shader route and raw pass output; PBR's opaque check concerns G-buffer output,
not the complete deferred lighting pipeline. Vertex/index arrays are checked
unchanged. The ordinary test plane uses indices 84..87 and a nonzero stale saber
displacement: an incorrect vertex-flag reuse changes its actual GPU footprint,
and an isolated negative variant proves the test detects it. Authored opacity
0/.35/1, unused zero normals, duplicate layers and reversed draw order are covered.

Accumulation uses production `Context::OIT_Transparent`, RGBA16F accumulated
RGB/revealage and R16F weight, with production clear and depth-write policy.
Production `oitBlend` resolves to RGBA8. Framebuffer sRGB conversion and dithering
are disabled: results are raw shader/UNORM values, not a claim that shipped
texture RGB is linear radiance. All readback values must be finite.
Absolute tolerances are 0.002 for half-float accumulation (about two ULPs near
one) and 2/255 for final UNORM8 quantization. Near-black RGB/weight instead use
max(1.192e-7, 0.003 * reference), require positive representable coverage, and
cannot pass by discarding every near-black sample. Footprint masks compare
covered pixels directly. No cross-driver bit identity is required.

For explicit negative runs, invoke the executable with
`--candidate=post343`, `--candidate=dedicated-only`,
`--candidate=lighting-only`, `--candidate=global-fullbright`,
`--candidate=wrong-geometry` or `--candidate=pre343`.
The retained contracts must reject each for a relevant reason: ordinary blade
recovery, alpha-sensitive additive contribution, genuinely lit non-additive
materials, or geometry preservation. A whole rollback is not rejected merely
because an unsupported ordinary additive pixel changed.

The former seven-test D run is historical and was insufficient: shipped models
can include ordinary additive planes in the complete blade. Passing synthetic
tests, even with the improved model route, does not by itself certify the full
shipped model, ignition/retraction, retail fidelity, all menu consumers or
sustained play. Matched local shipped-asset captures and acceptance are separate
gates. The documented initial PBR startup delay workaround is not an engine
startup fix; this runner does not exercise that startup sequence.
