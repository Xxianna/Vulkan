#version 300 es
precision highp float;

out vec2 outUV;

void main()
{
	outUV = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
