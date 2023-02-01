use crate::ffi;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;

pub use ffi::PFN_vkGetInstanceProcAddr as GetInstanceProcAddr;
pub use ffi::VkInstance as Instance;
pub use ffi::VkPhysicalDevice as PhysicalDevice;
pub use ffi::VkPresentModeKHR as PresentModeKHR;
pub use ffi::VkSurfaceKHR as SurfaceKHR;

pub fn get_required_api_version() -> u32 {
    unsafe { ffi::ren_vk_GetRequiredAPIVersion() }
}

pub fn get_required_raw_layers() -> &'static [*const c_char] {
    unsafe {
        let layers = ffi::ren_vk_GetRequiredLayers();
        let layer_cnt = ffi::ren_vk_GetRequiredLayerCount();
        std::slice::from_raw_parts(layers, layer_cnt)
    }
}

pub fn get_required_layers() -> Vec<CString> {
    get_required_raw_layers()
        .iter()
        .copied()
        .map(|ext| CString::from(unsafe { CStr::from_ptr(ext) }))
        .collect()
}

pub fn get_required_raw_extensions() -> &'static [*const c_char] {
    unsafe {
        let extensions = ffi::ren_vk_GetRequiredExtensions();
        let extension_cnt = ffi::ren_vk_GetRequiredExtensionCount();
        std::slice::from_raw_parts(extensions, extension_cnt)
    }
}

pub fn get_required_extensions() -> Vec<CString> {
    get_required_raw_extensions()
        .iter()
        .copied()
        .map(|ext| CString::from(unsafe { CStr::from_ptr(ext) }))
        .collect()
}
