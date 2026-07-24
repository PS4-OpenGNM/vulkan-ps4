#version 450

layout(location = 0) out vec4 frag_color;

layout(set = 0, binding = 0) uniform ClearColor {
    vec4 color;
};

void main() {
    frag_color = color;
}
