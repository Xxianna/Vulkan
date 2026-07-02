#version 300 es
precision highp float;

in vec2 inUV;
out vec4 outFragColor;

uniform sampler2D samplerUI;

void main()
{
	outFragColor = texture(samplerUI, inUV);
}
