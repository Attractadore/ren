#include "Swapchain.hpp"
#include "Profiler.hpp"
#include "Renderer.hpp"
#include "core/Views.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <algorithm>
#include <fmt/format.h>

namespace ren {

namespace {

auto get_fullscreen_state(SDL_Window *window) -> bool {
  int w, h;
  SDL_GetWindowSize((SDL_Window *)window, &w, &h);
  int display = SDL_GetWindowDisplayIndex((SDL_Window *)window);
  SDL_DisplayMode mode;
  SDL_GetDesktopDisplayMode(display, &mode);
  return mode.w == w and mode.h == h;
}

} // namespace

Swapchain::~Swapchain() {
  m_renderer->wait_idle();
  for (Handle<Texture> t : m_textures) {
    m_renderer->destroy(t);
  }
  rhi::destroy_swap_chain(m_swap_chain);
  rhi::destroy_surface(m_surface);
}

auto Swapchain::init(Renderer &renderer, SDL_Window *window,
                     rhi::QueueFamily queue_family) -> Result<void, Error> {
  m_renderer = &renderer;
  m_window = window;
  m_queue_family = queue_family;

  ren_try(m_surface, rhi::create_surface(m_window));

  SDL_GetWindowSizeInPixels(m_window, &m_size.x, &m_size.y);
  m_fullscreen = get_fullscreen_state(m_window);

  rhi::Adapter adapter = m_renderer->get_adapter();
  rhi::Device device = m_renderer->get_rhi_device();

  ren_try(rhi::PresentMode present_mode, select_present_mode());
  ren_try(u32 num_images, select_image_count(present_mode));

  {
    u32 num_formats = 0;
    ren_try_to(
        rhi::get_surface_formats(adapter, m_surface, &num_formats, nullptr));
    SmallVector<TinyImageFormat> formats(num_formats);
    ren_try_to(rhi::get_surface_formats(adapter, m_surface, &num_formats,
                                        formats.data()));
    auto it = std::ranges::find_if(formats, [](TinyImageFormat format) {
      return format == TinyImageFormat_B8G8R8A8_SRGB;
    });
    m_format = it != formats.end() ? *it : formats.front();
  }

  {
    ren_try(rhi::ImageUsageFlags supported_usage,
            rhi::get_surface_supported_image_usage(adapter, m_surface));
    constexpr Flags<rhi::ImageUsage> REQUIRED_USAGE =
        rhi::ImageUsage::TransferDst;
    ren_assert((supported_usage & REQUIRED_USAGE) == REQUIRED_USAGE);
    m_usage = REQUIRED_USAGE;
  }

  fmt::println("Create swap chain: {}x{}, fullscreen: {}, vsync: {}, {} images",
               m_size.x, m_size.y, m_fullscreen, m_vsync == VSync::On,
               num_images);

  ren_try(m_swap_chain, rhi::create_swap_chain({
                            .device = device,
                            .surface = m_surface,
                            .queue = rhi::get_queue(device, queue_family),
                            .width = (u32)m_size.x,
                            .height = (u32)m_size.y,
                            .format = m_format,
                            .usage = m_usage,
                            .num_images = num_images,
                            .present_mode = present_mode,
                        }));

  m_size = rhi::get_swap_chain_size(m_swap_chain);

  ren_try_to(update_textures());

  fmt::println("Created swap chain: {}x{}, present mode: {}, {} images",
               m_size.x, m_size.y, (int)present_mode, m_textures.size());

  return {};
}

void Swapchain::set_vsync(VSync vsync) {
  if (m_vsync != vsync) {
    m_vsync = vsync;
    m_dirty = true;
  }
}

void Swapchain::set_frames_in_flight(u32 num_frames_in_flight) {
  if (m_num_frames_in_flight != num_frames_in_flight) {
    m_num_frames_in_flight = num_frames_in_flight;
    m_dirty = true;
  }
}

auto Swapchain::select_present_mode() -> Result<rhi::PresentMode, Error> {
  auto present_mode = rhi::PresentMode::Fifo;
  if (m_vsync == VSync::Off) {
    rhi::PresentMode present_modes[rhi::PRESENT_MODE_COUNT];
    u32 num_present_modes = std::size(present_modes);
    ren_try_to(rhi::get_surface_present_modes(m_renderer->get_adapter(),
                                              m_surface, &num_present_modes,
                                              present_modes));
    bool have_immediate = false;
    bool have_mailbox = false;
    for (usize i : range(num_present_modes)) {
      have_immediate =
          have_immediate or present_modes[i] == rhi::PresentMode::Immediate;
      have_mailbox =
          have_mailbox or present_modes[i] == rhi::PresentMode::Mailbox;
    }
    if (have_immediate) {
      present_mode = rhi::PresentMode::Immediate;
    } else if (have_mailbox) {
      present_mode = rhi::PresentMode::Mailbox;
    }
  }
  return present_mode;
}

auto Swapchain::select_image_count(rhi::PresentMode pm) -> Result<u32, Error> {
  SDL_SysWMinfo wm_info;
  SDL_VERSION(&wm_info.version);
  if (!SDL_GetWindowWMInfo(m_window, &wm_info)) {
    return Failure(Error::SDL2);
  }
  switch (wm_info.subsystem) {
  default:
    return 3;
  case SDL_SYSWM_X11:
  case SDL_SYSWM_WAYLAND: {
    // On Linux, we need the following images:
    // 1. One for presenting.
    // 2. For mailbox, one queued for present.
    // 3. One for drawing into.
    // 4. One less than the number of frames in flight to record commands for
    // due to synchronous acquire.
    u32 num_images = m_num_frames_in_flight + 1;
    // Tearing is only allowed in fullscreen on Linux.
    if (pm == rhi::PresentMode::Mailbox or
        (pm == rhi::PresentMode::Immediate and not m_fullscreen)) {
      return num_images + 1;
    }
    return num_images;
  }
  case SDL_SYSWM_WINDOWS: {
    // On Windows, we need the following images:
    // 1. One for presenting.
    // 2. For mailbox, 1 or 2 queued for present. DWM can only returns images
    // that for queued for present, but were not presented, back to the swap
    // chain on the next vblank, which caps the maximum frame rate in mailbox to
    // refresh rate * (number of swap chain images - 1). flight count.
    // 3. One for drawing into.
    // 4. One less than the number of frames in flight to record commands for
    // due to synchronous acquire.
    u32 num_images = m_num_frames_in_flight + 1;
    // On Windows, tearing is allowed in windowed mode if MPOs are supported.
    if (pm == rhi::PresentMode::Mailbox) {
      return num_images + 1;
    }
    return num_images;
  }
  }
}

auto Swapchain::update_textures() -> Result<void, Error> {
  rhi::Image images[rhi::MAX_SWAP_CHAIN_IMAGE_COUNT];
  u32 num_images = std::size(images);
  ren_try_to(rhi::get_swap_chain_images(m_swap_chain, &num_images, images));
  m_textures.resize(num_images);
  for (usize i : range(num_images)) {
    m_textures[i] = m_renderer->create_external_texture({
        .name = fmt::format("Swap Chain Texture {}", i),
        .handle = images[i],
        .format = m_format,
        .usage = m_usage,
        .width = (u32)m_size.x,
        .height = (u32)m_size.y,
    });
  }
  return {};
}

auto Swapchain::update() -> Result<void, Error> {
  m_renderer->wait_idle();

  auto present_mode = select_present_mode();
  if (!present_mode) {
    return Failure(present_mode.error());
  }

  auto num_images = select_image_count(*present_mode);
  if (!num_images) {
    return Failure(num_images.error());
  }

  fmt::println("Update swap chain: {}x{}, fullscreen: {}, vsync: {}, {} images",
               m_size.x, m_size.y, m_fullscreen, m_vsync == VSync::On,
               *num_images);

  ren_try_to(rhi::set_present_mode(m_swap_chain, *present_mode));
  ren_try_to(rhi::resize_swap_chain(m_swap_chain, m_size, *num_images));
  m_size = rhi::get_swap_chain_size(m_swap_chain);

  for (Handle<Texture> t : m_textures) {
    m_renderer->destroy(t);
  }
  ren_try_to(update_textures());

  m_dirty = false;

  fmt::println("Updated swap chain: {}x{}, present mode: {}, {} images",
               m_size.x, m_size.y, (int)*present_mode, m_textures.size());

  return {};
}

auto Swapchain::acquire_texture(Handle<Semaphore> signal_semaphore)
    -> Result<Handle<Texture>, Error> {
  ren_prof_zone("Swapchain::acquire_texture");

  glm::ivec2 size;
  SDL_GetWindowSizeInPixels(m_window, &size.x, &size.y);
  if (m_size != size) {
    m_size = size;
    m_dirty = true;
  }

  bool fullscreen = get_fullscreen_state(m_window);
  if (m_fullscreen != fullscreen) {
    m_fullscreen = fullscreen;
    m_dirty = true;
  }

  if (m_dirty) {
    ren_try_to(update());
  }

  while (true) {
    rhi::Result<u32> image = rhi::acquire_image(
        m_swap_chain,
        rhi::Semaphore{m_renderer->get_semaphore(signal_semaphore).handle});
    if (image) {
      return m_textures[*image];
    }
    if (image.error() == rhi::Error::OutOfDate) {
      ren_try_to(update());
      continue;
    }
    return Failure(image.error());
  }
}

auto Swapchain::present(Handle<Semaphore> wait_semaphore)
    -> Result<void, Error> {
  ren_prof_zone("Swapchain::present");
  auto result = rhi::present(
      m_swap_chain,
      rhi::Semaphore{m_renderer->get_semaphore(wait_semaphore).handle});
  if (!result) {
    if (result.error() == rhi::Error::OutOfDate) {
      return update();
    }
    return Failure(result.error());
  }
  return {};
}

} // namespace ren
