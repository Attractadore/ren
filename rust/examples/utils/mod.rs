use anyhow::{Context, Result};
use ash::{
    vk::{self, Handle},
    Entry,
};
use raw_window_handle::{HasRawDisplayHandle, HasRawWindowHandle};
use ren::{Device, Scene, SceneKey, SwapchainKey};
use std::ffi::CStr;
use winit::{
    dpi::PhysicalSize,
    event::{DeviceEvent, Event, WindowEvent},
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
                proc: std::mem::transmute(vk.static_fn().get_instance_proc_addr),
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
