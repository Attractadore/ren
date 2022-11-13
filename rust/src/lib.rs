mod ffi;
pub mod vk;

use ffi::RenDevice;

pub struct Device {
    device: *mut RenDevice,
}

impl Device {
    unsafe fn new(device: *mut RenDevice) -> Self {
        Self { device }
    }
}

impl Drop for Device {
    fn drop(&mut self) {
        unsafe { ffi::Ren_DestroyDevice(self.device) }
    }
}
