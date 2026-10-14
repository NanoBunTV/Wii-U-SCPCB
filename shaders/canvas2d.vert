#version 330
// Blitz 2D drawing on the back buffer (and presenting it) : pixel-space quads.
layout(location = 0) in vec2 a_pos;      // pixels
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_rgba;     // big-endian 0xRRGGBBAA -> (R,G,B,A)

uniform vec4 u_screen;                   // x : 2/width, y : -2/height (pixel -> NDC), z : depth

out vec2 v_uv;
out vec4 v_color;

void main()
{
	gl_Position = vec4(a_pos.x * u_screen.x - 1.0, a_pos.y * u_screen.y + 1.0, u_screen.z, 1.0);
	v_uv = a_uv;
	v_color = a_rgba;
}
