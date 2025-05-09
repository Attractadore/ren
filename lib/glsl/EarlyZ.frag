#include "EarlyZ.h"
#include "Vertex.h"

layout(location = 0) in vec3 v_view_pos;
layout(location = 0) out uint f_normal;

void main() {
    vec3 t = dFdx(v_view_pos);
    vec3 b = -dFdy(v_view_pos);
    vec3 n = normalize(cross(t, b));
    f_normal = pack_normal_to_uint(encode_normal(n));
}
