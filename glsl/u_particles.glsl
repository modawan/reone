const int MAX_PARTICLES = 64;

struct Particle {
    vec4 positionFrame;
    vec4 right;
    vec4 up;
    vec4 color;
    vec2 size;
};

layout(std140) uniform Particles {
    ivec2 uGridSize;
    int uParticleReconstructionMode;
    int uParticleAlphaMode;
    int uParticleTrailMode;
    int uMotionBlur;
    float uParticleReconstructionStrength;
    float uParticleAlphaExponent;
    float uParticleTrailCoreIntensity;
    float uParticleCoverageContrast;
    int uParticleDiagnosticMode;
    Particle uParticles[MAX_PARTICLES];
};
