#version 450

#ifdef VERTEX_SHADER

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec2 in_tex0;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_tex0;
layout(location = 2) out float v_fog;

layout(push_constant) uniform PushConsts 
{
	mat4 u_world;
	mat4 u_proj;
	mat4 u_view;
};

layout(set = 0, binding = 0, std140) uniform State
{
	vec2 u_alphaRef;
	vec4 u_fogData;
	vec4 u_fogColor;
};

#define u_fogStart (u_fogData.x)
#define u_fogEnd (u_fogData.y)
#define u_fogRange (u_fogData.z)
#define u_fogDisable (u_fogData.w)

float DoFog(float w)
{
	return clamp((w - u_fogEnd)*u_fogRange, u_fogDisable, 1.0);
}

void main(void)
{
	vec4 Vertex = u_world * vec4(in_pos, 1.0);
	gl_Position = u_proj * u_view * Vertex;
	v_color = in_color;
	v_tex0 = in_tex0;
	v_fog = DoFog(gl_Position.w);
}

#else

layout(set = 0, binding = 0, std140) uniform State
{
	vec2 u_alphaRef;
	vec4 u_fogData;
	vec4 u_fogColor;
};

layout(set = 0, binding = 1) uniform sampler2D tex0;

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_tex0;
layout(location = 2) in float v_fog;

void DoAlphaTest(float a)
{
#ifndef NO_ALPHATEST
	if(a < u_alphaRef.x || a >= u_alphaRef.y)
		discard;
#endif
}

void main(void)
{
	vec4 color = v_color*texture(tex0, vec2(v_tex0.x, v_tex0.y));
	color.rgb = mix(u_fogColor.rgb, color.rgb, v_fog);
	DoAlphaTest(color.a);
	fragColor = color;
}

#endif