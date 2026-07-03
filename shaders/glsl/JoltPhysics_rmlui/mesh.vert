#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec3 outViewVec;
layout (location = 3) out vec3 outLightVec;

layout (set = 0, binding = 0) uniform UBO {
	mat4 projection;
	mat4 view;
	vec4 lightPos;
	vec4 viewPos;
} ubo;

layout (push_constant) uniform PushConsts {
	mat4 model;
	vec3 color;
} push;

void main()
{
	outColor = push.color;
	vec4 pos = push.model * vec4(inPos, 1.0);
	gl_Position = ubo.projection * ubo.view * pos;

	vec4 worldPos = ubo.view * pos;
	outNormal = mat3(ubo.view) * mat3(push.model) * inNormal;
	outLightVec = ubo.lightPos.xyz - worldPos.xyz;
	outViewVec = ubo.viewPos.xyz - worldPos.xyz;
}
