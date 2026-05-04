// SPDX-License-Identifier: GPL-3.0-only
// raster.frag — Vulkan fragment shader for the bench's raster phase.
// Heavy fragment work: many texture taps + math to stress fillrate.

#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 1) uniform sampler2D tex;

void main() {
    vec3 c = vec3(0.0);
    // 16-tap blur sampling; stresses bandwidth + fragment ALU.
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec2 ofs = vec2(x, y) / 1024.0;
            c += texture(tex, v_uv + ofs).rgb;
        }
    }
    c /= 25.0;
    outColor = vec4(c, 1.0);
}
