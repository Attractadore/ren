#include "ren/ren.hpp"
#include "Lippincott.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "Support/Errors.hpp"
#include "Swapchain.hpp"
#include "ren/ren-vk.hpp"

#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>

namespace ren {

Renderer *g_renderer = nullptr;

namespace {
std::unique_ptr<Renderer> g_renderer_holder;
SmallVector<SceneImpl *, 1> g_scenes;
} // namespace

auto init(const RendererDesc &init_info) -> expected<void> {
  return lippincott([&] {
    g_renderer_holder = std::make_unique<Renderer>(
        init_info.instance_extensions, init_info.adapter);
    g_renderer = g_renderer_holder.get();
  });
}

void quit() {
  ren_assert_msg(g_scenes.empty(), "All scenes must have been destroyed");
  g_renderer_holder.reset();
  g_renderer = nullptr;
}

auto draw() -> expected<void> {
  return lippincott([] {
    for (SceneImpl *scene : g_scenes) {
      scene->draw();
    }
    g_renderer->next_frame();
    for (SceneImpl *scene : g_scenes) {
      scene->next_frame();
    }
  });
}

namespace {

auto get_swapchain(SwapchainId swapchain) -> SwapchainImpl * {
  return std::bit_cast<SwapchainImpl *>(swapchain);
}

auto get_swapchain_id(const SwapchainImpl *swapchain) -> SwapchainId {
  return std::bit_cast<SwapchainId>(swapchain);
}

} // namespace

namespace vk {

auto create_swapchain(vk::PFNCreateSurface create_surface, void *usrptr)
    -> expected<Swapchain> {
  ren_assert(create_surface);
  VkSurfaceKHR surface = nullptr;
  return lippincott([&] {
           throw_if_failed(
               create_surface(g_renderer->get_instance(), usrptr, &surface),
               "Vulkan: Failed to create surface");
           return Swapchain(get_swapchain_id(new SwapchainImpl(surface)));
         })
      .transform_error([&](Error error) {
        vkDestroySurfaceKHR(g_renderer->get_instance(), surface, nullptr);
        return error;
      });
}

} // namespace vk

void destroy_swapchain(SwapchainId swapchain) {
  delete get_swapchain(swapchain);
}

auto get_size(SwapchainId swapchain) -> std::tuple<unsigned, unsigned> {
  ren_assert(swapchain);
  return get_swapchain(swapchain)->get_size();
}

void set_size(SwapchainId swapchain, unsigned width, unsigned height) {
  ren_assert(swapchain);
  get_swapchain(swapchain)->set_size(width, height);
}

namespace {

auto get_scene(SceneId scene) -> SceneImpl * {
  return std::bit_cast<SceneImpl *>(scene);
}

auto get_scene_id(const SceneImpl *scene) -> SceneId {
  return std::bit_cast<SceneId>(scene);
}

} // namespace

auto create_scene(SwapchainId swapchain) -> expected<Scene> {
  ren_assert(swapchain);
  return lippincott([&] {
    auto scene = std::make_unique<SceneImpl>(*get_swapchain(swapchain));
    g_scenes.push_back(scene.get());
    return Scene(get_scene_id(scene.release()));
  });
}

void destroy_scene(SceneId scene) {
  ren_assert(scene);
  SceneImpl *ptr = get_scene(scene);
  g_scenes.unstable_erase(ptr);
  delete ptr;
}

void set_camera(SceneId scene, const CameraDesc &desc) {
  get_scene(scene)->set_camera(desc);
}

void set_tone_mapping(SceneId scene, const ToneMappingDesc &desc) {
  get_scene(scene)->set_tone_mapping(desc);
}

auto create_mesh(SceneId scene, const MeshDesc &desc) -> expected<MeshId> {
  return lippincott([&] { return get_scene(scene)->create_mesh(desc); });
}

auto create_image(SceneId scene, const ImageDesc &desc) -> expected<ImageId> {
  return lippincott([&] { return get_scene(scene)->create_image(desc); });
}

auto create_materials(SceneId scene, std::span<const MaterialDesc> descs,
                      MaterialId *out) -> expected<void> {
  return lippincott(
      [&] { return get_scene(scene)->create_materials(descs, out); });
}

auto create_mesh_instances(SceneId scene,
                           std::span<const MeshInstanceDesc> descs,
                           std::span<const glm::mat4x3> transforms,
                           MeshInstanceId *out) -> expected<void> {
  return lippincott([&] {
    return get_scene(scene)->create_mesh_instances(descs, transforms, out);
  });
}

void destroy_mesh_instances(SceneId scene,
                            std::span<const MeshInstanceId> mesh_instances) {
  get_scene(scene)->destroy_mesh_instances(mesh_instances);
}

void set_mesh_instance_transforms(
    SceneId scene, std::span<const MeshInstanceId> mesh_instances,
    std::span<const glm::mat4x3> transforms) {
  get_scene(scene)->set_mesh_instance_transforms(mesh_instances, transforms);
}

[[nodiscard]] auto create_directional_light(SceneId scene,
                                            const DirectionalLightDesc &desc)
    -> expected<DirectionalLightId> {
  return lippincott(
      [&] { return get_scene(scene)->create_directional_light(desc); });
}

void destroy_directional_light(SceneId scene, DirectionalLightId light) {
  get_scene(scene)->destroy_directional_light(light);
}

void update_directional_light(SceneId scene, DirectionalLightId light,
                              const DirectionalLightDesc &desc) {
  get_scene(scene)->update_directional_light(light, desc);
}

} // namespace ren
