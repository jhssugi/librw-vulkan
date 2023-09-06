#version 450

#ifdef VERTEX_SHADER

#define MAX_LIGHTS 8

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec4 in_weights;
layout(location = 4) in vec4 in_indices;
layout(location = 5) in vec2 in_tex0;
layout(location = 6) in vec2 in_tex1;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec4 v_envColor;
layout(location = 2) out vec2 v_tex0;
layout(location = 3) out vec2 v_tex1;
layout(location = 4) out float v_fog;

layout(set = 0, binding = 0, std140) uniform State
{
	vec2 u_alphaRef;
	vec4 u_fogData;
	vec4 u_fogColor;
};

layout(set = 0, binding = 1, std140) uniform Scene
{
	mat4 u_proj;
	mat4 u_view;
};

layout(set = 0, binding = 2, std140) uniform Object
{
	mat4 u_world;
	vec4 u_ambLight;
	vec4 u_lightParams[MAX_LIGHTS];	// type, radius, minusCosAngle, hardSpot
	vec4 u_lightPosition[MAX_LIGHTS];
	vec4 u_lightDirection[MAX_LIGHTS];
	vec4 u_lightColor[MAX_LIGHTS];
};

layout(push_constant) uniform PushConsts
{
	mat4 u_texMatrix;
	vec4 u_colorClamp;
	vec4 u_envColor;
};

vec3 DoDynamicLight(vec3 V, vec3 N)
{
	vec3 color = vec3(0.0, 0.0, 0.0);
	for(int i = 0; i < MAX_LIGHTS; i++){
		if(u_lightParams[i].x == 0.0)
			break;
#ifdef DIRECTIONALS
		if(u_lightParams[i].x == 1.0){
			// direct
			float l = max(0.0, dot(N, -u_lightDirection[i].xyz));
			color += l*u_lightColor[i].rgb;
		}else
#endif
#ifdef POINTLIGHTS
		if(u_lightParams[i].x == 2.0){
			// point
			vec3 dir = V - u_lightPosition[i].xyz;
			float dist = length(dir);
			float atten = max(0.0, (1.0 - dist/u_lightParams[i].y));
			float l = max(0.0, dot(N, -normalize(dir)));
			color += l*u_lightColor[i].rgb*atten;
		}else
#endif
#ifdef SPOTLIGHTS
		if(u_lightParams[i].x == 3.0){
			// spot
			vec3 dir = V - u_lightPosition[i].xyz;
			float dist = length(dir);
			float atten = max(0.0, (1.0 - dist/u_lightParams[i].y));
			dir /= dist;
			float l = max(0.0, dot(N, -dir));
			float pcos = dot(dir, u_lightDirection[i].xyz);	// cos to point
			float ccos = -u_lightParams[i].z;
			float falloff = (pcos-ccos)/(1.0-ccos);
			if(falloff < 0.0)	// outside of cone
				l = 0.0;
			l *= max(falloff, u_lightParams[i].w);
			return l*u_lightColor[i].rgb*atten;
		}else
#endif
			;
	}
	return color;
}

void main(void)
{
	vec4 Vertex = u_world * vec4(in_pos, 1.0);
	gl_Position = u_proj * u_view * Vertex;
	vec3 Normal = mat3(u_world) * in_normal;

	v_tex0 = in_tex0;
	v_tex1 = (u_texMatrix * vec4(Normal, 1.0)).xy;

	v_color = in_color;
	v_color.rgb += u_ambLight.rgb*surfAmbient;
	v_color.rgb += DoDynamicLight(Vertex.xyz, Normal)*surfDiffuse;
	v_color = clamp(v_color, 0.0, 1.0);
	v_envColor = max(v_color, u_colorClamp) * u_envColor;
	v_color *= u_matColor;

	v_fog = DoFog(gl_Position.w);
}

#else

layout(set = 3, binding = 0) uniform sampler2D tex0;
layout(set = 3, binding = 1) uniform sampler2D tex1;

layout(set = 3, binding = 2, std140) uniform Block
{
	vec4 u_fxparams;
};


#define shininess (u_fxparams.x)
#define disableFBA (u_fxparams.y)

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec4 v_envColor;
layout(location = 2) in vec2 v_tex0;
layout(location = 3) in vec2 v_tex1;
layout(location = 4) in float v_fog;

layout(location = 0) out vec4 fragColor;

void DoAlphaTest(float a)
{
#ifndef NO_ALPHATEST
	if(a < u_alphaRef.x || a >= u_alphaRef.y)
		discard;
#endif
}

void main(void)
{
	vec4 pass1 = v_color;
	pass1 *= texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));

	vec4 pass2 = v_envColor*shininess*texture(tex1, vec2(v_tex1.x, 1.0-v_tex1.y));

	pass1.rgb = mix(u_fogColor.rgb, pass1.rgb, v_fog);
	pass2.rgb = mix(vec3(0.0, 0.0, 0.0), pass2.rgb, v_fog);

	float fba = max(pass1.a, disableFBA);
	vec4 color;
	color.rgb = pass1.rgb*pass1.a + pass2.rgb*fba;
	color.a = pass1.a;

	DoAlphaTest(color.a);

	fragColor = color;
}

#endif