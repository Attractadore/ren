#[cfg(windows)]
pub mod dx12;
mod ffi;
pub mod vk;

use ffi::{RenDevice, RenScene, RenSwapchain};
use std::marker::PhantomData;

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
        unsafe { ffi::ren_DestroyDevice(self.device) }
    }
}

pub struct Swapchain<'a> {
    swapchain: *mut RenSwapchain,
    device: PhantomData<&'a Device>,
}

impl<'a> Swapchain<'a> {
    unsafe fn new(_: &'a Device, swapchain: *mut RenSwapchain) -> Self {
        Self {
            swapchain,
            device: PhantomData,
        }
    }

    pub fn get_size(&self) -> (u32, u32) {
        unsafe {
            (
                ffi::ren_GetSwapchainWidth(self.swapchain),
                ffi::ren_GetSwapchainHeight(self.swapchain),
            )
        }
    }

    pub fn set_size(&mut self, width: u32, height: u32) {
        unsafe { ffi::ren_SetSwapchainSize(self.swapchain, width, height) }
    }
}

impl<'a> Drop for Swapchain<'a> {
    fn drop(&mut self) {
        unsafe { ffi::ren_DestroySwapchain(self.swapchain) }
    }
}

pub struct Scene<'a> {
    scene: *mut RenScene,
    device: PhantomData<&'a Device>,
    swapchain: Option<Swapchain<'a>>,
}

impl<'a> Scene<'a> {
    pub fn new(device: &Device) -> Self {
        Self {
            scene: unsafe { ffi::ren_CreateScene(device.device) },
            device: PhantomData,
            swapchain: None,
        }
    }

    pub fn set_swapchain(&mut self, swapchain: Swapchain<'a>) -> Option<Swapchain<'a>> {
        unsafe {
            ffi::ren_SetSceneSwapchain(self.scene, swapchain.swapchain);
        }
        std::mem::replace(&mut self.swapchain, Some(swapchain))
    }

    pub fn get_swapchain(&self) -> Option<&Swapchain<'a>> {
        self.swapchain.as_ref()
    }

    pub fn get_swapchain_mut(&mut self) -> Option<&mut Swapchain<'a>> {
        self.swapchain.as_mut()
    }

    pub fn reset_swapchain(&mut self) -> Option<Swapchain<'a>> {
        unsafe { ffi::ren_SetSceneSwapchain(self.scene, std::ptr::null_mut()) }
        self.swapchain.take()
    }

    pub fn get_output_size(&self) -> (u32, u32) {
        unsafe {
            (
                ffi::ren_GetSceneOutputWidth(self.scene),
                ffi::ren_GetSceneOutputHeight(self.scene),
            )
        }
    }

    pub fn set_output_size(&mut self, width: u32, height: u32) {
        unsafe { ffi::ren_SetSceneOutputSize(self.scene, width, height) }
    }

    pub fn draw(&mut self) {
        unsafe { ffi::ren_DrawScene(self.scene) }
    }
}

impl<'a> Drop for Scene<'a> {
    fn drop(&mut self) {
        unsafe { ffi::ren_DestroyScene(self.scene) }
    }
}
