const int NUM_SERIES = 4;
const int NUM_FRAMES = 100;

layout(std140) uniform Profiler {
    vec4 uSeriesColors[NUM_SERIES];
    vec4 uSeriesValues1[NUM_FRAMES / 4];
    vec4 uSeriesValues2[NUM_FRAMES / 4];
    vec4 uSeriesValues3[NUM_FRAMES / 4];
    vec4 uSeriesValues4[NUM_FRAMES / 4];
};
