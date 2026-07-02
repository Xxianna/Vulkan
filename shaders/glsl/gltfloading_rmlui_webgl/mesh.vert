#version 300 es
precision highp float;

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inColor;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

out vec3 outNormal;
out vec3 outColor;
out vec2 outUV;
out vec3 outViewVec;
out vec3 outLightVec;

void main()
{
	outNormal = inNormal;
	outColor = inColor;
	outUV = inUV;
	gl_Position = projection * view * model * vec4(inPos.xyz, 1.0);

	vec4 pos = view * vec4(inPos, 1.0);
	outNormal = mat3(view) * inNormal;
	outLightVec = vec3(view * vec4(5.0, 5.0, -5.0, 1.0)) - pos.xyz;
	outViewVec = -pos.xyz;
}
