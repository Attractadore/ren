use anyhow::{Context, Result};
use ash::{
    vk::{self, Handle},
    Entry,
};
use glam::{Quat, Vec3, Vec3A};
use raw_window_handle::{HasRawDisplayHandle, HasRawWindowHandle};
use ren::{Device, Scene, SceneKey, SwapchainKey};
use std::{ffi::CStr, mem};
use winit::{
    dpi::PhysicalSize,
    event::{DeviceEvent, ElementState, Event, KeyboardInput, VirtualKeyCode, WindowEvent},
    event_loop::EventLoop,
    window::{Window, WindowBuilder},
};

struct Instance(ash::Instance);

impl Instance {
    fn new(entry: &Entry) -> Result<Self> {
        let create_info = vk::InstanceCreateInfo {
            ..Default::default()
        };
        Ok(Self(unsafe { entry.create_instance(&create_info, None) }?))
    }
}

impl Drop for Instance {
    fn drop(&mut self) {
        unsafe { self.0.destroy_instance(None) }
    }
}

pub trait App {
    fn get_title(&self) -> &str;

    fn handle_window_event(&mut self, _event: &WindowEvent) -> Result<()> {
        Ok(())
    }

    fn handle_device_event(&mut self, _event: &DeviceEvent) -> Result<()> {
        Ok(())
    }

    fn handle_frame(&mut self, _scene: &mut Scene) -> Result<()> {
        Ok(())
    }
}

pub struct AppBase {
    device: Device,
    // Need to keep Vulkan DLL loaded as long as Device is alive
    _vk: Entry,
    window: Window,
    event_loop: EventLoop<()>,
    swapchain: SwapchainKey,
    scene: SceneKey,
}

impl AppBase {
    pub fn new() -> Result<Self> {
        let event_loop = EventLoop::new();

        let window = WindowBuilder::new()
            .with_inner_size(PhysicalSize::<u32>::from((1280, 720)))
            .build(&event_loop)?;

        let vk = unsafe { Entry::load()? };

        let instance = Instance::new(&vk)?;

        let adapter = *unsafe { instance.0.enumerate_physical_devices()? }
            .first()
            .context("Failed to find a Vulkan device")?;
        let props = unsafe { instance.0.get_physical_device_properties(adapter) };
        println!(
            "Running on {}",
            unsafe { CStr::from_ptr(props.device_name.as_ptr()) }.to_str()?
        );

        let mut device = unsafe {
            Device::new(&ren::DeviceDesc {
                proc: mem::transmute(vk.static_fn().get_instance_proc_addr),
                instance_extensions: ash_window::enumerate_required_extensions(
                    event_loop.raw_display_handle(),
                )?,
                pipeline_cache_uuid: props.pipeline_cache_uuid,
            })?
        };

        let swapchain = unsafe {
            device.create_swapchain(|instance| {
                let instance =
                    ash::Instance::load(vk.static_fn(), vk::Instance::from_raw(instance as u64));
                ash_window::create_surface(
                    &vk,
                    &instance,
                    window.raw_display_handle(),
                    window.raw_window_handle(),
                    None,
                )
                .map(|surface| surface.as_raw() as ren::vk::SurfaceKHR)
                .map_err(|result| ren::vk::Result(result.as_raw()))
            })?
        };

        let scene = device.create_scene()?;
        let (width, height) = device.get_swapchain(swapchain).unwrap().get_size();
        device
            .get_scene_mut(scene)
            .unwrap()
            .set_viewport(width, height)?;

        Ok(Self {
            event_loop,
            window,
            _vk: vk,
            device,
            swapchain,
            scene,
        })
    }

    pub fn get_window(&self) -> &Window {
        &self.window
    }

    pub fn get_scene_mut(&mut self) -> &mut Scene {
        self.device.get_scene_mut(self.scene).unwrap()
    }

    pub fn run(mut self, mut app: impl App + 'static) -> ! {
        self.window.set_title(app.get_title());
        self.event_loop.run(move |event, _, control_flow: _| {
            || -> Result<()> {
                control_flow.set_poll();
                match event {
                    Event::WindowEvent { event, .. } => match event {
                        WindowEvent::CloseRequested => {
                            control_flow.set_exit();
                        }
                        WindowEvent::Resized(PhysicalSize { width, height }) => {
                            self.device
                                .get_swapchain_mut(self.swapchain)
                                .unwrap()
                                .set_size(width, height);
                            self.device
                                .get_scene_mut(self.scene)
                                .unwrap()
                                .set_viewport(width, height)?;
                        }
                        _ => app.handle_window_event(&event)?,
                    },
                    Event::DeviceEvent { event, .. } => app.handle_device_event(&event)?,
                    Event::MainEventsCleared => {
                        app.handle_frame(self.device.get_scene_mut(self.scene).unwrap())?;
                        self.device.draw_scene(self.scene, self.swapchain)?;
                    }
                    _ => (),
                };
                Ok(())
            }()
            .unwrap();
        })
    }
}

pub struct Camera {
    pub position: Vec3A,
    pub forward: Vec3A,
    pub up: Vec3A,
    pub quat: Quat,
}

impl Camera {
    pub fn new() -> Self {
        Self {
            position: Vec3A::new(0.0, 0.0, 0.0),
            forward: Vec3A::new(1.0, 0.0, 0.0),
            up: Vec3A::new(0.0, 0.0, 1.0),
            quat: Quat::IDENTITY,
        }
    }

    pub fn get_forward_vector(&self) -> Vec3A {
        self.quat * self.forward
    }

    pub fn get_left_vector(&self) -> Vec3A {
        (self.quat * (self.up.cross(self.forward))).normalize()
    }

    pub fn get_up_vector(&self) -> Vec3A {
        self.up
    }
}

pub struct CameraController {
    pub camera: Camera,
    pub speed: f32,
    pub sensitivity: (f32, f32),
    rotation_input: Quat,
    pub forward_key: VirtualKeyCode,
    pub back_key: VirtualKeyCode,
    pub left_key: VirtualKeyCode,
    pub right_key: VirtualKeyCode,
    pub up_key: VirtualKeyCode,
    pub down_key: VirtualKeyCode,
    forward_mult: f32,
    back_mult: f32,
    left_mult: f32,
    right_mult: f32,
    up_mult: f32,
    down_mult: f32,
}

impl CameraController {
    pub fn new(camera: Camera) -> Self {
        Self {
            camera,
            speed: 5.0,
            sensitivity: (-0.04f32.to_radians(), 0.02f32.to_radians()),
            rotation_input: Quat::IDENTITY,
            forward_key: VirtualKeyCode::W,
            back_key: VirtualKeyCode::S,
            left_key: VirtualKeyCode::A,
            right_key: VirtualKeyCode::D,
            up_key: VirtualKeyCode::V,
            down_key: VirtualKeyCode::C,
            forward_mult: 0.0,
            back_mult: 0.0,
            left_mult: 0.0,
            right_mult: 0.0,
            up_mult: 0.0,
            down_mult: 0.0,
        }
    }

    pub fn consume_movement_input(&mut self, time: f32) {
        let up = self.camera.get_up_vector();
        let forward = self
            .camera
            .get_forward_vector()
            .reject_from_normalized(up)
            .normalize();
        let left = self
            .camera
            .get_left_vector()
            .reject_from_normalized(up)
            .normalize();
        let movement_input = forward * (self.forward_mult - self.back_mult)
            + left * (self.left_mult - self.right_mult)
            + up * (self.up_mult - self.down_mult);
        let direction = movement_input.normalize_or_zero();
        self.camera.position += direction * time * self.speed;
    }

    pub fn add_rotation_input(&mut self, axis: Vec3, angle: f32) {
        let quat = Quat::from_axis_angle(axis, angle);
        self.rotation_input = quat * self.rotation_input;
    }

    pub fn consume_rotation_input(&mut self) {
        self.camera.quat =
            (mem::replace(&mut self.rotation_input, Quat::IDENTITY) * self.camera.quat).normalize();
    }

    pub fn add_pitch_input(&mut self, angle: f32) {
        let axis = Vec3::from(self.camera.get_left_vector());
        self.add_rotation_input(axis, angle);
    }

    pub fn add_yaw_input(&mut self, angle: f32) {
        let axis = Vec3::from(self.camera.up);
        self.add_rotation_input(axis, angle);
    }

    pub fn handle_mouse_motion(&mut self, motion: (f32, f32)) {
        self.add_yaw_input(motion.0 * self.sensitivity.0);
        self.add_pitch_input(motion.1 * self.sensitivity.1);
    }

    pub fn handle_key_input(&mut self, input: &KeyboardInput) {
        if let Some(key) = input.virtual_keycode {
            let get_mult = |state| match state {
                ElementState::Pressed => 1.0,
                ElementState::Released => 0.0,
            };
            if key == self.forward_key {
                self.forward_mult = get_mult(input.state);
            } else if key == self.back_key {
                self.back_mult = get_mult(input.state);
            } else if key == self.left_key {
                self.left_mult = get_mult(input.state);
            } else if key == self.right_key {
                self.right_mult = get_mult(input.state);
            } else if key == self.up_key {
                self.up_mult = get_mult(input.state);
            } else if key == self.down_key {
                self.down_mult = get_mult(input.state);
            }
        }
    }
}
