#include "Opaque.h"
#include "Transforms.h"

namespace ren::sh {

[[vk::push_constant]] OpaqueArgs pc;

vec4 main(OpaqueVsOutput in) : SV_Target0 {
  Material material = pc.materials[in.material];

  vec4 color = material.base_color;
  if (OPAQUE_FEATURE_VC) {
    color *= in.color;
  }

  if (OPAQUE_FEATURE_UV && !IsNull(material.base_color_texture)) {
    color *= Get(material.base_color_texture).Sample(in.uv);
  }

  float occlusion = 1.0f;
  float roughness = material.roughness;
  float metallic = material.metallic;
  if (OPAQUE_FEATURE_UV && !IsNull(material.orm_texture)) {
    vec4 orm = Get(material.orm_texture).Sample(in.uv);
    occlusion = 1.0f - material.occlusion_strength * (1.0f - orm.r);
    roughness *= orm.g;
    metallic *= orm.b;
  }

  vec3 albedo = mix(color.rgb, vec3(0.0f), metallic);
  vec3 f0 = fresnel_f0(color.rgb, metallic);

  vec3 normal = in.normal;
  if (OPAQUE_FEATURE_UV && OPAQUE_FEATURE_TS && !IsNull(material.normal_texture)) {
    vec3 tex = Get(material.normal_texture).Sample(in.uv).xyz;
    tex = 2.0f * tex - 1.0f;
    tex.xy *= material.normal_scale;
    vec3 tangent = in.tangent.xyz;
    float s = in.tangent.w;
    vec3 bitangent = s * cross(normal, tangent);
    normal = mat3(tangent, bitangent, normal) * tex;
  }
  normal = normalize(normal);

  vec4 result = vec4(0.0f, 0.0f, 0.0f, 1.0f);
  vec3 view = normalize(pc.eye - in.position);
  for (int i = 0; i < pc.num_directional_lights; ++i) {
    DirectionalLight light = pc.directional_lights[i];
    result.xyz += lighting(normal, light.origin, view, albedo, f0, roughness, light.color * light.illuminance);
  }

  float ka = 1.0f;
  if (!IsNull(pc.ssao)) {
    vec2 uv = gl_FragCoord.xy * pc.inv_viewport;
    vec2 ba = Get(pc.ssao).SampleLevel(uv, 0).rg;
    float z = abs(1.0f / gl_FragCoord.w);
    ka = clamp(ba.x + ba.y * z, 0.0f, 1.0f);
  }
  vec3 bent_normal = normalize(in.normal);

  if (!IsNull(pc.env_map)) {
    result.xyz += occlusion * env_lighting(normal, view, albedo, f0, roughness, Get(pc.env_map), ka, bent_normal);
  } else {
    result.xyz += occlusion * env_lighting(normal, view, albedo, f0, roughness, pc.env_luminance, ka, bent_normal);
  }

  result.xyz *= *pc.exposure;

  return result;
}

}
