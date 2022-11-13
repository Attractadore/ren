mod ffi;

use crate::Device;
use std::os::raw::c_char;

pub use ffi::{PFN_vkGetInstanceProcAddr, VkInstance, VkPhysicalDevice};

pub fn get_required_api_version() -> u32 {
    unsafe { ffi::Ren_Vk_GetRequiredAPIVersion() }
}

pub fn get_required_layers() -> &'static [*const c_char] {
    unsafe {
        let layer_cnt = ffi::Ren_Vk_GetNumRequiredLayers();
        let layers = ffi::Ren_Vk_GetRequiredLayers();
        std::slice::from_raw_parts(layers, layer_cnt)
    }
}

pub fn get_required_extensions() -> &'static [*const c_char] {
    unsafe {
        let extension_cnt = ffi::Ren_Vk_GetNumRequiredExtensions();
        let extensions = ffi::Ren_Vk_GetRequiredExtensions();
        std::slice::from_raw_parts(extensions, extension_cnt)
    }
}

pub unsafe fn create_device(
    proc: PFN_vkGetInstanceProcAddr,
    instance: VkInstance,
    physical_device: VkPhysicalDevice,
) -> Device {
    Device::new(ffi::Ren_Vk_CreateDevice(proc, instance, physical_device))
}
