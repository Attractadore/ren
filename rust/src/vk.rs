use crate::{ffi, Device, Swapchain};
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::rc::Rc;

pub use ffi::{
    PFN_vkGetInstanceProcAddr, VkInstance, VkPhysicalDevice, VkPresentModeKHR, VkSurfaceKHR,
};

pub fn get_required_api_version() -> u32 {
    unsafe { ffi::ren_vk_GetRequiredAPIVersion() }
}

pub fn get_required_raw_layers() -> &'static [*const c_char] {
    unsafe {
        let layer_cnt = ffi::ren_vk_GetRequiredLayerCount();
        let layers = ffi::ren_vk_GetRequiredLayers();
        std::slice::from_raw_parts(layers, layer_cnt)
    }
}

pub fn get_required_layers() -> Vec<CString> {
    get_required_raw_layers()
        .iter()
        .copied()
        .map(|ext| {
            let ext = unsafe { CStr::from_ptr(ext) };
            CString::from(ext)
        })
        .collect()
}

pub fn get_required_raw_extensions() -> &'static [*const c_char] {
    unsafe {
        let extension_cnt = ffi::ren_vk_GetRequiredExtensionCount();
        let extensions = ffi::ren_vk_GetRequiredExtensions();
        std::slice::from_raw_parts(extensions, extension_cnt)
    }
}

pub fn get_required_extensions() -> Vec<CString> {
    get_required_raw_extensions()
        .iter()
        .copied()
        .map(|ext| {
            let ext = unsafe { CStr::from_ptr(ext) };
            CString::from(ext)
        })
        .collect()
}
/// # Safety
///
/// Requires valid vkGetInstanceProcAddr, VkInstance and VkPhysicalDevice
pub unsafe fn create_device(
    proc: PFN_vkGetInstanceProcAddr,
    instance: VkInstance,
    adapter: VkPhysicalDevice,
) -> Rc<Device> {
    Device::new(ffi::ren_vk_CreateDevice(proc, instance, adapter))
}

/// # Safety
///
/// Requires valid VkSurfaceKHR
pub unsafe fn create_swapchain(device: Rc<Device>, surface: VkSurfaceKHR) -> Swapchain {
    let device_handle = device.handle.0;
    Swapchain::new(device, ffi::ren_vk_CreateSwapchain(device_handle, surface))
}

pub fn get_swapchain_surface(swapchain: &Swapchain) -> VkSurfaceKHR {
    unsafe { ffi::ren_vk_GetSwapchainSurface(swapchain.handle.0) }
}

pub fn get_swapchain_present_mode(swapchain: &Swapchain) -> VkPresentModeKHR {
    unsafe { ffi::ren_vk_GetSwapchainPresentMode(swapchain.handle.0) }
}

pub fn set_swapchain_present_mode(swapchain: &mut Swapchain, present_mode: VkPresentModeKHR) {
    unsafe { ffi::ren_vk_SetSwapchainPresentMode(swapchain.handle.0, present_mode) }
}
