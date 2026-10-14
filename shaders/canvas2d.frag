#version 330
in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_tex;
uniform vec4 u_mode;      // x : 0 solid colour, 1 image, 2 masked image, 3 glyph (texture alpha = coverage)
uniform vec4 u_mask;      // mask colour (rgb) for mode 2

out vec4 o_color;

void main()
{
	float mode = u_mode.x;
	if (mode == 0.0) {
		o_color = v_color;
		return;
	}
	vec4 t = texture(u_tex, v_uv);
	if (mode == 2.0) {
		vec3 d = abs(t.rgb - u_mask.rgb);
		if (max(d.r, max(d.g, d.b)) < 0.5 / 255.0) discard;
	} else if (mode == 3.0) {
		// glyph : coverage in alpha, blended
		if (t.a <= 0.0) discard;
		o_color = vec4(v_color.rgb, t.a);
		return;
	}
	o_color = vec4(t.rgb * v_color.rgb, 1.0);
}
