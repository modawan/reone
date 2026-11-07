const int NUM_AABB_CORNERS = 8;

layout(std140) uniform AABB {
    vec4 uCorners[NUM_AABB_CORNERS];
};
