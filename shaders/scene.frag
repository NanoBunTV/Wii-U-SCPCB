#version 330
// Direct3D 7 texture stage cascade as set up by Blitz3D (gxScene::setTexState) :
// colour = op(texture, current), alpha = texture alpha * current (TEX_ALPHA) or current; then specular,
// fog and alpha test.

in vec4 v_diffuse;
in vec4 v_specular;
in vec4 v_uv[4];

uniform sampler2D u_tex0;
uniform sampler2D u_tex1;
uniform sampler2D u_tex2;
uniform sampler2D u_tex3;

uniform vec4 u_ps_stage[4];   // x : blend op, y : texture alpha
uniform vec4 u_fogcolor;      // rgb
uniform vec4 u_ps_flags;      // x : alpha test on, y : alpha reference (0..1), z : specular on

out vec4 o_color;

vec4 applyStage(vec4 cur, vec4 t, vec4 st)
{
	float op = st.x;
	// textures without the alpha flag have no alpha channel in D3D : alpha reads as 1
	if (st.y < 0.5) t.a = 1.0;
	vec3 c;
	if (op == 1.0) c = mix(cur.rgb, t.rgb, t.a);               // BLENDTEXTUREALPHA
	else if (op == 2.0) c = cur.rgb * t.rgb;                   // MODULATE
	else if (op == 3.0) c = cur.rgb + t.rgb;                   // ADD
	else if (op == 4.0) {                                      // DOTPRODUCT3
		float d = clamp(4.0 * dot(cur.rgb - 0.5, t.rgb - 0.5), 0.0, 1.0);
		c = vec3(d);
	}
	else if (op == 5.0) c = cur.rgb * t.rgb * 2.0;             // MODULATE2X
	else return cur;
	float a = st.y > 0.5 ? cur.a * t.a : cur.a;
	return clamp(vec4(c, a), 0.0, 1.0);
}

void main()
{
	vec4 c = v_diffuse;
	if (u_ps_stage[0].x != 0.0) c = applyStage(c, texture(u_tex0, v_uv[0].xy), u_ps_stage[0]);
	if (u_ps_stage[1].x != 0.0) c = applyStage(c, texture(u_tex1, v_uv[1].xy), u_ps_stage[1]);
	if (u_ps_stage[2].x != 0.0) c = applyStage(c, texture(u_tex2, v_uv[2].xy), u_ps_stage[2]);
	if (u_ps_stage[3].x != 0.0) c = applyStage(c, texture(u_tex3, v_uv[3].xy), u_ps_stage[3]);

	if (u_ps_flags.z > 0.5) c.rgb = min(c.rgb + v_specular.rgb, vec3(1.0));
	c.rgb = mix(u_fogcolor.rgb, c.rgb, v_specular.w);

	if (u_ps_flags.x > 0.5 && c.a <= u_ps_flags.y) discard;
	o_color = c;
}
