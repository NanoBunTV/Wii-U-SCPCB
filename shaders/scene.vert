#version 330
// Direct3D 7 fixed-function vertex pipeline as used by Blitz3D (gxScene) :
// world-view transform, D3D projection, per-vertex lighting (8 lights), linear fog, texture coordinate
// generation / transform for 4 texture stages.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_argb;     // big-endian 0xAARRGGBB -> (A,R,G,B)
layout(location = 3) in vec2 a_uv0;
layout(location = 4) in vec2 a_uv1;
layout(location = 5) in vec4 a_bones;    // skinning (attribute buffer 1) : bone indices
layout(location = 6) in vec4 a_weights;  // and weights; a 1-element dummy buffer when not skinned

uniform vec4 u_skin;          // x : 1 = skinned on the GPU (a_pos / a_normal in mesh space)
uniform vec4 u_bones[192];    // 64 bones x 3 rows : world = dot(vec4(pos,1), row)

uniform vec4 u_wv[3];         // world*view as 3 columns : view = dot(vec4(pos,1), u_wv[i])
uniform vec4 u_proj;          // W, H, Q, -Q*near
uniform vec4 u_proj_mode;     // x : 1 = perspective (w = z), 0 = orthographic (w = 1)

uniform vec4 u_material;      // diffuse / ambient colour (rgb) + alpha
uniform vec4 u_ambient;       // global ambient (rgb)
uniform vec4 u_specular;      // x : specular level, y : power, z : enabled
uniform vec4 u_vflags;        // x : colour from vertex (COLORVERTEX), y : emissive from vertex, z : lighting on
uniform vec4 u_light_pos[8];  // view space position, w : type (0 off, 1 directional, 2 point, 3 spot)
uniform vec4 u_light_dir[8];  // view space direction (normalised), w : linear attenuation (1/range)
uniform vec4 u_light_col[8];  // diffuse colour
uniform vec4 u_light_spot[8]; // x : cos(inner/2), y : cos(outer/2)

uniform vec4 u_fog;           // x : start, y : end, z : enabled

uniform vec4 u_stage[4];      // x : blend op (0 = off), y : texture alpha, z : coordinate set, w : texgen
uniform vec4 u_texmat[8];     // per stage two rows : u' = dot(vec3(u,v,1), row0.xyz), v' = dot(..., row1.xyz)

out vec4 v_diffuse;
out vec4 v_specular;          // rgb + fog factor in w (1 = no fog)
out vec4 v_uv[4];

vec4 stageCoords(int s, vec3 view_pos, vec3 view_normal)
{
	vec4 st = u_stage[s];
	if (st.x == 0.0) return vec4(0.0);
	if (st.w == 1.0) {
		// sphere map : camera space normal, D3D matrix (0.5,-0.5) + 0.5
		return vec4(view_normal.x * 0.5 + 0.5, -view_normal.y * 0.5 + 0.5, 0.0, 0.0);
	}
	if (st.w >= 2.0) {
		vec3 t;
		if (st.w == 2.0) t = view_normal;
		else if (st.w == 3.0) t = view_pos;
		else t = reflect(normalize(view_pos), view_normal);
		return vec4(t, 1.0);
	}
	vec2 uv = st.z > 0.5 ? a_uv1 : a_uv0;
	vec3 u3 = vec3(uv, 1.0);
	return vec4(dot(u3, u_texmat[s * 2].xyz), dot(u3, u_texmat[s * 2 + 1].xyz), 0.0, 0.0);
}

void main()
{
	vec4 p = vec4(a_pos, 1.0);
	vec3 n = a_normal;
	if (u_skin.x > 0.5) {
		// Blitz's CPU skinning, here : weighted bone transforms (the normal uses the same matrices, renormalised below)
		vec3 sp = vec3(0.0), sn = vec3(0.0);
		for (int i = 0; i < 4; ++i) {
			float w = a_weights[i];
			if (w == 0.0) continue;
			int b = int(a_bones[i] + 0.5) * 3;
			sp += w * vec3(dot(p, u_bones[b]), dot(p, u_bones[b + 1]), dot(p, u_bones[b + 2]));
			sn += w * vec3(dot(a_normal, u_bones[b].xyz), dot(a_normal, u_bones[b + 1].xyz), dot(a_normal, u_bones[b + 2].xyz));
		}
		p = vec4(sp, 1.0);
		n = sn;
	}
	vec3 vpos = vec3(dot(p, u_wv[0]), dot(p, u_wv[1]), dot(p, u_wv[2]));
	vec3 vnorm = vec3(dot(n, u_wv[0].xyz), dot(n, u_wv[1].xyz), dot(n, u_wv[2].xyz));
	float nlen = length(vnorm);
	vnorm = nlen > 0.0 ? vnorm / nlen : vec3(0.0, 0.0, -1.0);

	float w = u_proj_mode.x > 0.5 ? vpos.z : 1.0;
	gl_Position = vec4(vpos.x * u_proj.x, vpos.y * u_proj.y, vpos.z * u_proj.z + u_proj.w, w);

	vec4 vcol = a_argb.yzwx;
	vec4 mat = u_vflags.x > 0.5 ? vcol : u_material;
	vec3 emissive = u_vflags.y > 0.5 ? vcol.rgb : vec3(0.0);

	vec3 diffuse_sum = vec3(0.0);
	vec3 spec_sum = vec3(0.0);
	if (u_vflags.z > 0.5) {
		vec3 view_dir = -normalize(vpos);
		for (int i = 0; i < 8; ++i) {
			float type = u_light_pos[i].w;
			if (type == 0.0) continue;
			vec3 L;
			float atten = 1.0;
			if (type == 1.0) {
				L = -u_light_dir[i].xyz;
			} else {
				vec3 d = u_light_pos[i].xyz - vpos;
				float dist = length(d);
				L = dist > 0.0 ? d / dist : vec3(0.0, 0.0, -1.0);
				float a = u_light_dir[i].w * dist;
				atten = a > 0.0 ? 1.0 / a : 1.0e6;
				if (type == 3.0) {
					float rho = dot(-L, u_light_dir[i].xyz);
					float ci = u_light_spot[i].x, co = u_light_spot[i].y;
					float spot = rho > ci ? 1.0 : (rho <= co ? 0.0 : (rho - co) / max(ci - co, 1.0e-6));
					atten *= spot;
				}
			}
			float ndl = max(dot(vnorm, L), 0.0);
			diffuse_sum += u_light_col[i].rgb * (ndl * atten);
			if (u_specular.z > 0.5 && ndl > 0.0) {
				vec3 h = normalize(L + view_dir);
				spec_sum += u_light_col[i].rgb * (pow(max(dot(vnorm, h), 0.0), u_specular.y) * atten);
			}
		}
		v_diffuse.rgb = clamp(emissive + u_ambient.rgb * mat.rgb + diffuse_sum * mat.rgb, 0.0, 1.0);
	} else {
		v_diffuse.rgb = mat.rgb;
	}
	v_diffuse.a = mat.a;
	v_specular.rgb = clamp(spec_sum * u_specular.x, 0.0, 1.0);

	// linear vertex fog on view depth
	float fog = 1.0;
	if (u_fog.z > 0.5) fog = clamp((u_fog.y - vpos.z) / max(u_fog.y - u_fog.x, 1.0e-4), 0.0, 1.0);
	v_specular.w = fog;

	v_uv[0] = stageCoords(0, vpos, vnorm);
	v_uv[1] = stageCoords(1, vpos, vnorm);
	v_uv[2] = stageCoords(2, vpos, vnorm);
	v_uv[3] = stageCoords(3, vpos, vnorm);
}
