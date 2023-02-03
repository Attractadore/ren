use anyhow::{Context, Result};
use ash::vk::{self, Handle};
use raw_window_handle::{HasRawDisplayHandle, HasRawWindowHandle};
use ren::{Device, Scene};
use std::ffi::CStr;
use winit::{
    dpi::PhysicalSize,
    event::{DeviceEvent, Event, WindowEvent},
    event_loop::EventLoop,
    window::WindowBuilder,
};

struct Instance(ash::Instance);

impl Instance {
    fn new(entry: &ash::Entry) -> Result<Self> {
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
    type Config;

    fn new(config: Self::Config, scene: &mut Scene) -> Result<Self>
    where
        Self: Sized;

    fn get_name(&self) -> &str;

    fn handle_window_event(&mut self, _event: &WindowEvent) -> Result<()> {
        Ok(())
    }

    fn handle_device_event(&mut self, _event: &DeviceEvent) -> Result<()> {
        Ok(())
    }

    fn iterate(&mut self, _scene: &mut Scene) -> Result<()> {
        Ok(())
    }
}

pub fn run<A: App + 'static>(config: A::Config) -> Result<()> {
    println!("Create EventLoop");
    let event_loop = EventLoop::new();

    let window = WindowBuilder::new()
        .with_inner_size(PhysicalSize::<u32>::from((1280, 720)))
        .build(&event_loop)?;

    println!("Load Vulkan");
    let vk = unsafe { ash::Entry::load() }?;

    println!("Create VkInstance");
    let instance = Instance::new(&vk)?;

    println!("Select VkPhysicalDevice #0");
    let adapter = *unsafe { instance.0.enumerate_physical_devices() }?
        .first()
        .context("Failed to find a Vulkan device")?;
    let props = unsafe { instance.0.get_physical_device_properties(adapter) };
    println!(
        "Running on {}",
        unsafe { CStr::from_ptr(props.device_name.as_ptr()) }.to_str()?
    );

    println!("Create ren::Device");
    let mut device = unsafe {
        Device::new(&ren::DeviceDesc {
            proc: std::mem::transmute(vk.static_fn().get_instance_proc_addr),
            instance_extensions: ash_window::enumerate_required_extensions(
                event_loop.raw_display_handle(),
            )?,
            pipeline_cache_uuid: props.pipeline_cache_uuid,
        })?
    };

    println!("Create ren::Swapchain");
    let swapchain = unsafe {
        device.create_swapchain(|instance| {
            let instance =
                ash::Instance::load(vk.static_fn(), vk::Instance::from_raw(instance as u64));
            println!("Create VkSurfaceKHR");
            ash_window::create_surface(
                &vk,
                &instance,
                window.raw_display_handle(),
                window.raw_window_handle(),
                None,
            )
            .map(|surface| surface.as_raw() as ren::vk::SurfaceKHR)
            .map_err(|_| ren::vk::ERROR_UNKNOWN)
        })?
    };

    println!("Create ren::Scene");
    let scene = device.create_scene()?;
    let (width, height) = device.get_swapchain(swapchain).unwrap().get_size();
    device
        .get_scene_mut(scene)
        .unwrap()
        .set_viewport(width, height)?;

    let mut app = A::new(config, device.get_scene_mut(scene).unwrap())?;
    window.set_title(app.get_name());

    event_loop.run(move |event, _, control_flow: _| {
        || -> Result<()> {
            control_flow.set_poll();
            match event {
                Event::WindowEvent { event, .. } => match event {
                    WindowEvent::CloseRequested => {
                        control_flow.set_exit();
                    }
                    WindowEvent::Resized(PhysicalSize { width, height }) => {
                        device
                            .get_swapchain_mut(swapchain)
                            .unwrap()
                            .set_size(width, height);
                        device
                            .get_scene_mut(scene)
                            .unwrap()
                            .set_viewport(width, height)?;
                    }
                    _ => app.handle_window_event(&event)?,
                },
                Event::DeviceEvent { event, .. } => app.handle_device_event(&event)?,
                Event::MainEventsCleared => {
                    app.iterate(device.get_scene_mut(scene).unwrap())?;
                    device.draw_scene(scene, swapchain)?;
                }
                Event::LoopDestroyed => {
                    println!("Done");
                }
                _ => (),
            };
            Ok(())
        }()
        .unwrap();
    });
}
