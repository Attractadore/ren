#include "ren/ren.h"
#include "Device.hpp"
#include "Scene.hpp"
#include "Support/Errors.hpp"
#include "Swapchain.hpp"
#include "ren/ren-vk.h"

#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>

namespace ren {

Renderer *g_renderer = nullptr;

namespace {
std::unique_ptr<Renderer> g_renderer_holder;
SmallVector<Scene *, 1> g_scenes;
} // namespace

} // namespace ren

namespace {

template <std::invocable F>
  requires std::same_as<std::invoke_result_t<F>, void>
RenResult lippincott(F f) noexcept {
  try {
    f();
    return REN_SUCCESS;
  } catch (const std::system_error &) {
    return REN_SYSTEM_ERROR;
  } catch (const std::runtime_error &e) {
    if (std::string_view(e.what()).starts_with("Vulkan")) {
      return REN_VULKAN_ERROR;
    }
    return REN_RUNTIME_ERROR;
  } catch (...) {
    return REN_UNKNOWN_ERROR;
  }
}

} // namespace

extern "C" {

static_assert(sizeof(RenVector2) == sizeof(glm::vec2));
static_assert(sizeof(RenVector3) == sizeof(glm::vec3));
static_assert(sizeof(RenVector4) == sizeof(glm::vec4));
static_assert(sizeof(RenMatrix4x4) == sizeof(glm::mat4));

RenResult ren_Init(size_t num_instance_extensions,
                   const char *const *instance_extensions, unsigned adapter) {
  if (num_instance_extensions > 0) {
    assert(instance_extensions);
  }
  return lippincott([&] {
    ren::g_renderer_holder = std::make_unique<ren::Renderer>(
        ren::Span(instance_extensions, num_instance_extensions), adapter);
    ren::g_renderer = ren::g_renderer_holder.get();
  });
}

void ren_Quit() {
  ren_assert(ren::g_scenes.empty(), "All scenes must be destroyed");
  ren::g_renderer_holder.reset();
  ren::g_renderer = nullptr;
}

RenResult ren_Draw() {
  return lippincott([] {
    for (ren::Scene *scene : ren::g_scenes) {
      scene->draw();
    }
    ren::g_renderer->next_frame();
    for (ren::Scene *scene : ren::g_scenes) {
      scene->next_frame();
    }
  });
}

RenResult ren_vk_CreateSwapchain(RenPFNCreateSurface create_surface,
                                 void *usrptr, RenSwapchain **p_swapchain) {
  assert(device);
  assert(create_surface);
  assert(p_swapchain);

  RenResult result = REN_SUCCESS;
  VkSurfaceKHR surface = nullptr;

  result = lippincott([&] {
    ren::throw_if_failed(
        create_surface(ren::g_renderer->get_instance(), usrptr, &surface),
        "Vulkan: Failed to create surface");
  });
  if (result) {
    goto clean;
  }

  result = lippincott([&] { *p_swapchain = new RenSwapchain(surface); });
  if (result) {
    goto clean;
  }

  return REN_SUCCESS;

clean:
  if (surface) {
    vkDestroySurfaceKHR(ren::g_renderer->get_instance(), surface, nullptr);
  }

  return result;
}

void ren_DestroySwapchain(RenSwapchain *swapchain) { delete swapchain; }

void ren_SetSwapchainSize(RenSwapchain *swapchain, unsigned width,
                          unsigned height) {
  assert(swapchain);
  assert(width);
  assert(height);
  swapchain->set_size(width, height);
}

void ren_GetSwapchainSize(const RenSwapchain *swapchain, unsigned *p_width,
                          unsigned *p_height) {
  assert(swapchain);
  assert(p_width);
  assert(p_height);
  auto [width, height] = swapchain->get_size();
  *p_width = width;
  *p_height = height;
}

VkSurfaceKHR ren_vk_GetSwapchainSurface(const RenSwapchain *swapchain) {
  assert(swapchain);
  return swapchain->get_surface();
}

VkPresentModeKHR ren_vk_GetSwapchainPresentMode(const RenSwapchain *swapchain) {
  assert(swapchain);
  return swapchain->get_present_mode();
}

RenResult ren_vk_SetSwapchainPresentMode(RenSwapchain *swapchain,
                                         VkPresentModeKHR present_mode) {
  assert(swapchain);
  return lippincott([&] { swapchain->set_present_mode(present_mode); });
}

RenResult ren_CreateScene(RenSwapchain *swapchain, RenScene **p_scene) {
  assert(device);
  assert(swapchain);
  assert(p_scene);
  return lippincott([&] {
    auto scene = std::make_unique<RenScene>(*swapchain);
    ren::g_scenes.push_back(scene.get());
    *p_scene = scene.release();
  });
}

void ren_DestroyScene(RenScene *scene) {
  ren::g_scenes.unstable_erase(scene);
  delete scene;
}

RenResult ren_DrawScene(RenScene *scene) {
  assert(scene);
  return lippincott([&] { scene->draw(); });
}

RenResult ren_SetSceneCamera(RenScene *scene, const RenCameraDesc *desc) {
  assert(scene);
  assert(desc);
  return lippincott([&] { scene->set_camera(*desc); });
}

RenResult ren_SetSceneToneMapping(RenScene *scene,
                                  RenToneMappingOperator oper) {
  assert(scene);
  return lippincott([&] { scene->set_tone_mapping(oper); });
}

RenResult ren_CreateMesh(RenScene *scene, const RenMeshDesc *desc,
                         RenMesh *p_mesh) {
  assert(scene);
  assert(desc);
  assert(p_mesh);
  return lippincott([&] { *p_mesh = scene->create_mesh(*desc); });
}

RenResult ren_CreateImage(RenScene *scene, const RenImageDesc *desc,
                          RenImage *p_image) {
  assert(scene);
  assert(desc);
  assert(p_image);
  return lippincott([&] { *p_image = scene->create_image(*desc); });
}

RenResult ren_CreateMaterials(RenScene *scene, const RenMaterialDesc *descs,
                              size_t count, RenMaterial *materials) {
  assert(scene);
  assert(descs);
  assert(materials);
  return lippincott(
      [&] { scene->create_materials(std::span(descs, count), materials); });
}

RenResult ren_CreateMeshInsts(RenScene *scene, const RenMeshInstDesc *descs,
                              size_t count, RenMeshInst *mesh_insts) {
  assert(scene);
  assert(descs);
  assert(mesh_insts);
  return lippincott(
      [&] { scene->create_mesh_insts(std::span(descs, count), mesh_insts); });
}

void ren_DestroyMeshInsts(RenScene *scene, const RenMeshInst *mesh_insts,
                          size_t count) {
  assert(scene);
  assert(mesh_insts);
  scene->destroy_mesh_insts(std::span(mesh_insts, count));
}

void ren_SetMeshInstMatrices(RenScene *scene, const RenMeshInst *mesh_insts,
                             const RenMatrix4x4 *matrices, size_t count) {
  assert(scene);
  assert(mesh_insts);
  assert(matrices);
  scene->set_mesh_inst_matrices(std::span(mesh_insts, count),
                                std::span(matrices, count));
}

RenResult ren_CreateDirLights(RenScene *scene, const RenDirLightDesc *descs,
                              size_t count, RenDirLight *lights) {
  assert(scene);
  assert(descs);
  assert(lights);
  return lippincott(
      [&] { scene->create_dir_lights(std::span(descs, count), lights); });
}

void ren_DestroyDirLights(RenScene *scene, const RenDirLight *lights,
                          size_t count) {
  assert(scene);
  assert(lights);
  scene->destroy_dir_lights(std::span(lights, count));
}

RenResult ren_ConfigDirLights(RenScene *scene, const RenDirLight *lights,
                              const RenDirLightDesc *descs, size_t count) {
  assert(scene);
  assert(lights);
  assert(descs);
  return lippincott([&] {
    scene->config_dir_lights(std::span(lights, count), std::span(descs, count));
  });
}

RenResult ren_CreatePointLights(RenScene *scene, const RenPointLightDesc *descs,
                                size_t count, RenPointLight *lights) {
  assert(scene);
  assert(descs);
  assert(lights);
  return lippincott([&] { ren::todo("Point light are not implemented!"); });
}

void ren_DestroyPointLights(RenScene *scene, const RenPointLight *lights,
                            size_t count) {
  assert(scene);
  assert(lights);
  ren::todo("Point light are not implemented!");
}

RenResult ren_ConfigPointLights(RenScene *scene, const RenPointLight *lights,
                                const RenPointLightDesc *descs, size_t count) {
  assert(scene);
  assert(lights);
  assert(descs);
  return lippincott([&] { ren::todo("Point light are not implemented!"); });
}

RenResult ren_CreateSpotLights(RenScene *scene, const RenSpotLightDesc *descs,
                               size_t count, RenSpotLight *lights) {
  assert(scene);
  assert(descs);
  assert(lights);
  return lippincott([&] { ren::todo("Spot light are not implemented!"); });
}

void ren_DestroySpotLights(RenScene *scene, const RenSpotLight *lights,
                           size_t count) {
  assert(scene);
  assert(lights);
  ren::todo("Spot light are not implemented!");
}

RenResult ren_ConfigSpotLights(RenScene *scene, const RenSpotLight *lights,
                               const RenSpotLightDesc *descs, size_t count) {
  assert(scene);
  assert(lights);
  assert(descs);
  return lippincott([&] { ren::todo("Spot light are not implemented!"); });
}
}
