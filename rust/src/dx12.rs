use crate::{ffi, Device, Swapchain};
use raw_window_handle::{HasRawWindowHandle, RawWindowHandle};
use std::ffi::c_void;

pub use ffi::LUID;

pub unsafe fn create_device(adapter: LUID) -> Device {
    Device::new(ffi::ren_dx12_CreateDevice(adapter))
}

pub unsafe fn create_swapchain<'a>(
    device: &'a Device,
    window: &'a dyn HasRawWindowHandle,
) -> Swapchain<'a> {
    match window.raw_window_handle() {
        RawWindowHandle::Win32(win32_handle) => Swapchain::new(
            device,
            ffi::ren_dx12_CreateSwapchain(device.device, win32_handle.hwnd as ffi::HWND),
        ),
        _ => panic!("Failed to get Win32 window handle"),
    }
}

pub fn get_swapchain_hwnd(swapchain: &Swapchain) -> *mut c_void {
    unsafe { ffi::ren_dx12_GetSwapchainHWND(swapchain.swapchain) as *mut c_void }
}
