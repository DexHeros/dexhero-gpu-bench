// SPDX-License-Identifier: GPL-3.0-only
// raster.vert — Vulkan vertex shader for the bench's raster phase.
// Renders a fullscreen quad — gl_VertexIndex pattern, no vertex buffer.

#version 450
layout(location = 0) out vec2 v_uv;

void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_uv = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
