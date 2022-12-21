mod ffi;

use crate::{Device, Swapchain};
use std::os::raw::c_char;

pub use ffi::{
    PFN_vkGetInstanceProcAddr, VkInstance, VkPhysicalDevice, VkPresentModeKHR, VkSurfaceKHR,
};

pub fn get_required_api_version() -> u32 {
    unsafe { ffi::ren_vk_GetRequiredAPIVersion() }
}

pub fn get_required_layers() -> &'static [*const c_char] {
    unsafe {
        let layer_cnt = ffi::ren_vk_GetRequiredLayerCount();
        let layers = ffi::ren_vk_GetRequiredLayers();
        std::slice::from_raw_parts(layers, layer_cnt)
    }
}

pub fn get_required_extensions() -> &'static [*const c_char] {
    unsafe {
        let extension_cnt = ffi::ren_vk_GetRequiredExtensionCount();
        let extensions = ffi::ren_vk_GetRequiredExtensions();
        std::slice::from_raw_parts(extensions, extension_cnt)
    }
}

pub unsafe fn create_device(
    proc: PFN_vkGetInstanceProcAddr,
    instance: VkInstance,
    physical_device: VkPhysicalDevice,
) -> Device {
    Device::new(ffi::ren_vk_CreateDevice(proc, instance, physical_device))
}

pub unsafe fn create_swapchain(device: &Device, surface: VkSurfaceKHR) -> Swapchain {
    Swapchain::new(device, ffi::ren_vk_CreateSwapchain(device.device, surface))
}

pub fn get_swapchain_surface(swapchain: &Swapchain) -> VkSurfaceKHR {
    unsafe { ffi::ren_vk_GetSwapchainSurface(swapchain.swapchain) }
}

pub fn get_swapchain_present_mode(swapchain: &Swapchain) -> VkPresentModeKHR {
    unsafe { ffi::ren_vk_GetSwapchainPresentMode(swapchain.swapchain) }
}

pub fn set_swapchain_present_mode(swapchain: &mut Swapchain, present_mode: VkPresentModeKHR) {
    unsafe { ffi::ren_vk_SetSwapchainPresentMode(swapchain.swapchain, present_mode) }
}
