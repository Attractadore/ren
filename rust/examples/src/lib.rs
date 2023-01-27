use ash::{
    extensions::khr,
    vk::{self, Handle},
};
use raw_window_handle::{
    HasRawDisplayHandle, HasRawWindowHandle, RawDisplayHandle, RawWindowHandle,
};
use std::ffi::CStr;
use winit::{
    dpi::PhysicalSize,
    event::{Event, WindowEvent},
    event_loop::EventLoop,
    window::WindowBuilder,
};

struct Instance(ash::Instance);

impl Instance {
    fn new(entry: &ash::Entry, display: RawDisplayHandle) -> Self {
        let layers = ren::vk::get_required_raw_layers();

        let extensions: Vec<_> = ren::vk::get_required_raw_extensions()
            .iter()
            .copied()
            .chain(
                ash_window::enumerate_required_extensions(display)
                    .unwrap()
                    .iter()
                    .copied(),
            )
            .collect();

        let application_info = vk::ApplicationInfo {
            api_version: ren::vk::get_required_api_version(),
            ..Default::default()
        };

        let create_info = vk::InstanceCreateInfo {
            p_application_info: &application_info,
            enabled_layer_count: layers.len() as u32,
            pp_enabled_layer_names: layers.as_ptr(),
            enabled_extension_count: extensions.len() as u32,
            pp_enabled_extension_names: extensions.as_ptr(),
            ..Default::default()
        };

        Self(unsafe { entry.create_instance(&create_info, None) }.unwrap())
    }

    fn as_raw(&self) -> u64 {
        self.0.handle().as_raw()
    }

    fn get_loader(&self, entry: &ash::Entry) -> ren::vk::PFN_vkGetInstanceProcAddr {
        unsafe {
            std::mem::transmute({
                let get_instance_proc_addr = b"vkGetInstanceProcAddr\0";
                let get_instance_proc_addr =
                    CStr::from_bytes_with_nul_unchecked(get_instance_proc_addr);
                entry
                    .get_instance_proc_addr(self.0.handle(), get_instance_proc_addr.as_ptr())
                    .unwrap()
            })
        }
    }
}

impl Drop for Instance {
    fn drop(&mut self) {
        unsafe { self.0.destroy_instance(None) }
    }
}

struct Surface {
    entry: khr::Surface,
    surface: vk::SurfaceKHR,
}

impl Surface {
    fn new(
        entry: &ash::Entry,
        instance: &ash::Instance,
        display: RawDisplayHandle,
        window: RawWindowHandle,
    ) -> Self {
        println!("Load VK_KHR_surface");
        let khr_surface = khr::Surface::new(entry, instance);
        Self {
            entry: khr_surface,
            surface: unsafe {
                ash_window::create_surface(entry, instance, display, window, None).unwrap()
            },
        }
    }

    fn as_raw(&self) -> u64 {
        self.surface.as_raw()
    }
}

impl Drop for Surface {
    fn drop(&mut self) {
        unsafe {
            self.entry.destroy_surface(self.surface, None);
        }
    }
}

pub trait App {
    fn new(scene: &ren::SceneFrame) -> Self;

    fn get_name(&self) -> &str;

    fn iterate(&mut self, _scene: &ren::SceneFrame) {}
}

pub fn run<A: App + 'static>() {
    println!("Create EventLoop");
    let event_loop = EventLoop::new();

    let window = WindowBuilder::new()
        .with_inner_size(PhysicalSize::<u32>::from((1280, 720)))
        .build(&event_loop)
        .unwrap();

    println!("Load Vulkan");
    let vk = unsafe { ash::Entry::load() }.unwrap();

    println!("Create VkInstance");
    let instance = Instance::new(&vk, event_loop.raw_display_handle());

    println!("Select VkPhysicalDevice #0");
    let adapter = *unsafe { instance.0.enumerate_physical_devices() }
        .unwrap()
        .first()
        .unwrap();
    println!("Running on {}", {
        unsafe {
            let props = instance.0.get_physical_device_properties(adapter);
            CStr::from_ptr(props.device_name.as_ptr())
        }
        .to_str()
        .unwrap()
    });

    println!("Create ren::Device");
    let device = unsafe {
        ren::vk::create_device(
            instance.get_loader(&vk),
            instance.as_raw() as ren::vk::VkInstance,
            adapter.as_raw() as ren::vk::VkPhysicalDevice,
        )
    };

    println!("Create VkSurfaceKHR");
    let surface = Surface::new(
        &vk,
        &instance.0,
        window.raw_display_handle(),
        window.raw_window_handle(),
    );
    println!("Create ren::Swapchain");
    let mut swapchain = unsafe {
        ren::vk::create_swapchain(device.clone(), surface.as_raw() as ren::vk::VkSurfaceKHR)
    };

    println!("Create ren::Scene");
    let scene = ren::Scene::new(device.clone());

    let mut app = {
        let device = ren::DeviceFrame::new(&device);
        let scene = ren::SceneFrame::new(&device, &scene, &mut swapchain, 1, 1);
        A::new(&scene)
    };
    window.set_title(app.get_name());

    event_loop.run(move |event, _, control_flow| {
        control_flow.set_poll();
        match event {
            Event::WindowEvent {
                event: WindowEvent::CloseRequested,
                ..
            } => {
                control_flow.set_exit();
            }
            Event::WindowEvent {
                event: WindowEvent::Resized(PhysicalSize { width, height }),
                ..
            } => {
                swapchain.set_size(width, height);
            }
            Event::MainEventsCleared => {
                let device = ren::DeviceFrame::new(&device);

                let (width, height) = swapchain.get_size();
                let scene = ren::SceneFrame::new(&device, &scene, &mut swapchain, width, height);

                app.iterate(&scene);
            }
            Event::LoopDestroyed => {
                println!("Done");
            }
            _ => (),
        }
    });
}
