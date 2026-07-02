#version 300 es
precision highp float;

in vec3 inNormal;
in vec3 inColor;
in vec2 inUV;
in vec3 inViewVec;
in vec3 inLightVec;

uniform sampler2D samplerColorMap;

layout (location = 0) out vec4 outFragColor;

void main()
{
	vec4 color = texture(samplerColorMap, inUV) * vec4(inColor, 1.0);

	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	vec3 V = normalize(inViewVec);
	vec3 R = reflect(L, N);
	vec3 diffuse = max(dot(N, L), 0.15) * inColor;
	vec3 specular = pow(max(dot(R, V), 0.0), 16.0) * vec3(0.75);
	outFragColor = vec4(diffuse * color.rgb + specular, 1.0);
}
