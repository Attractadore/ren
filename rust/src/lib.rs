mod ffi;
pub mod vk;

use ffi::{RenDevice, RenScene, RenSwapchain};
use std::marker::PhantomData;
use std::rc::Rc;

#[derive(Clone)]
struct DeviceRef(Rc<()>);
pub struct Device(*mut RenDevice, DeviceRef);

impl Device {
    unsafe fn new(device: *mut RenDevice) -> Self {
        Self(device, DeviceRef(Rc::new(())))
    }
}

impl Drop for Device {
    fn drop(&mut self) {
        if Rc::strong_count(&self.1 .0) == 1 {
            unsafe { ffi::ren_DestroyDevice(self.0) }
        }
    }
}

pub struct DeviceFrame<'a> {
    lock: &'a mut Device,
}

impl<'a> DeviceFrame<'a> {
    pub fn new(device: &'a mut Device) -> Self {
        unsafe {
            ffi::ren_DeviceBeginFrame(device.0);
        }
        Self { lock: device }
    }
}

impl<'a> Drop for DeviceFrame<'a> {
    fn drop(&mut self) {
        unsafe {
            ffi::ren_DeviceEndFrame(self.lock.0);
        }
    }
}

pub struct Swapchain {
    _device: DeviceRef,
    swapchain: *mut RenSwapchain,
}

impl Swapchain {
    unsafe fn new(device: &Device, swapchain: *mut RenSwapchain) -> Self {
        Self {
            _device: device.1.clone(),
            swapchain,
        }
    }

    pub fn set_size(&mut self, width: u32, height: u32) {
        unsafe { ffi::ren_SetSwapchainSize(self.swapchain, width, height) }
    }
}

impl Drop for Swapchain {
    fn drop(&mut self) {
        unsafe { ffi::ren_DestroySwapchain(self.swapchain) }
    }
}

pub struct Scene {
    _device: DeviceRef,
    scene: *mut RenScene,
}

impl Scene {
    pub fn new(device: &Device) -> Self {
        Self {
            _device: device.1.clone(),
            scene: unsafe { ffi::ren_CreateScene(device.0) },
        }
    }
}

impl Drop for Scene {
    fn drop(&mut self) {
        unsafe { ffi::ren_DestroyScene(self.scene) }
    }
}

pub struct SceneFrame<'a, 'd> {
    device: PhantomData<&'a DeviceFrame<'d>>,
    swapchain: PhantomData<&'a mut Swapchain>,
    lock: &'a mut Scene,
}

impl<'a, 'd> SceneFrame<'a, 'd> {
    pub fn new(
        _device: &'a DeviceFrame<'d>,
        scene: &'a mut Scene,
        swapchain: &'a mut Swapchain,
    ) -> Self {
        unsafe {
            ffi::ren_SceneBeginFrame(scene.scene, swapchain.swapchain);
        }
        Self {
            device: PhantomData,
            swapchain: PhantomData,
            lock: scene,
        }
    }

    pub fn set_output_size(&mut self, width: u32, height: u32) {
        unsafe { ffi::ren_SetSceneOutputSize(self.lock.scene, width, height) }
    }
}

impl<'a, 'd> Drop for SceneFrame<'a, 'd> {
    fn drop(&mut self) {
        unsafe {
            ffi::ren_SceneEndFrame(self.lock.scene);
        }
    }
}
