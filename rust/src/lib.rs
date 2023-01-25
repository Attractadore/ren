mod ffi;
pub mod vk;

use ffi::{RenDevice, RenScene, RenSwapchain};
use std::cell::{RefCell, RefMut};
use std::marker::PhantomData;

pub struct Device {
    device: RefCell<*mut RenDevice>,
}

impl Device {
    unsafe fn new(device: *mut RenDevice) -> Self {
        Self {
            device: RefCell::new(device),
        }
    }
}

impl Drop for Device {
    fn drop(&mut self) {
        unsafe { ffi::ren_DestroyDevice(*self.device.borrow_mut()) }
    }
}

pub struct DeviceFrame<'a> {
    lock: RefMut<'a, *mut RenDevice>,
}

impl<'a> DeviceFrame<'a> {
    pub fn new(device: &'a Device) -> Self {
        let device = device.device.borrow_mut();
        unsafe {
            ffi::ren_DeviceBeginFrame(*device);
        }
        Self { lock: device }
    }
}

impl<'a> Drop for DeviceFrame<'a> {
    fn drop(&mut self) {
        unsafe {
            ffi::ren_DeviceEndFrame(*self.lock);
        }
    }
}

pub struct Swapchain<'a> {
    device: PhantomData<&'a Device>,
    swapchain: *mut RenSwapchain,
}

impl<'a> Swapchain<'a> {
    unsafe fn new(_: &'a Device, swapchain: *mut RenSwapchain) -> Self {
        Self {
            device: PhantomData,
            swapchain,
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
    device: PhantomData<&'a Device>,
    scene: RefCell<*mut RenScene>,
}

impl<'a> Scene<'a> {
    pub fn new(device: &Device) -> Self {
        Self {
            device: PhantomData,
            scene: RefCell::new(unsafe { ffi::ren_CreateScene(*device.device.borrow()) }),
        }
    }
}

impl<'a> Drop for Scene<'a> {
    fn drop(&mut self) {
        unsafe { ffi::ren_DestroyScene(*self.scene.borrow_mut()) }
    }
}

pub struct SceneFrame<'a, 'd> {
    device: PhantomData<&'a DeviceFrame<'d>>,
    swapchain: PhantomData<&'a mut Swapchain<'d>>,
    lock: RefMut<'a, *mut RenScene>,
}

impl<'a, 'd> SceneFrame<'a, 'd> {
    pub fn new(
        _device: &'a DeviceFrame<'d>,
        scene: &'a Scene<'d>,
        swapchain: &'a mut Swapchain<'d>,
    ) -> Self {
        let scene = scene.scene.borrow_mut();
        unsafe {
            ffi::ren_SceneBeginFrame(*scene, swapchain.swapchain);
        }
        Self {
            device: PhantomData,
            swapchain: PhantomData,
            lock: scene,
        }
    }

    pub fn set_output_size(&mut self, width: u32, height: u32) {
        unsafe { ffi::ren_SetSceneOutputSize(*self.lock, width, height) }
    }
}

impl<'a, 'd> Drop for SceneFrame<'a, 'd> {
    fn drop(&mut self) {
        unsafe {
            ffi::ren_SceneEndFrame(*self.lock);
        }
    }
}
