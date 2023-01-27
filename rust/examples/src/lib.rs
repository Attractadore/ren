use ash::{
    extensions::khr,
    vk::{self, Handle},
};
use raw_window_handle::{
    HasRawDisplayHandle, HasRawWindowHandle, RawDisplayHandle, RawWindowHandle,
};
use std::ffi::{CStr, CString};
use winit::{
    dpi::PhysicalSize,
    event::{Event, WindowEvent},
    event_loop::EventLoop,
    window::WindowBuilder,
};

struct Instance {
    entry: ash::Entry,
    instance: ash::Instance,
}

impl Instance {
    fn new(entry: ash::Entry, display: RawDisplayHandle, app_name: &str) -> Self {
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

        let app_name = CString::new(app_name).unwrap();
        let application_info = vk::ApplicationInfo {
            p_application_name: app_name.as_ptr(),
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

        Self {
            instance: unsafe { entry.create_instance(&create_info, None) }.unwrap(),
            entry,
        }
    }

    fn as_raw(&self) -> u64 {
        self.instance.handle().as_raw()
    }

    fn select_adapter(&self) -> vk::PhysicalDevice {
        *unsafe { self.instance.enumerate_physical_devices() }
            .unwrap()
            .first()
            .unwrap()
    }

    fn get_device_name(&self, device: vk::PhysicalDevice) -> String {
        let props = unsafe { self.instance.get_physical_device_properties(device) };
        let name = unsafe { CStr::from_ptr(props.device_name.as_ptr()) };
        name.to_str().unwrap().to_string()
    }

    fn get_loader(&self) -> vk::PFN_vkGetInstanceProcAddr {
        unsafe {
            std::mem::transmute({
                let get_instance_proc_addr = b"vkGetInstanceProcAddr\0";
                let get_instance_proc_addr =
                    CStr::from_bytes_with_nul_unchecked(get_instance_proc_addr);
                self.entry
                    .get_instance_proc_addr(self.instance.handle(), get_instance_proc_addr.as_ptr())
                    .unwrap()
            })
        }
    }
}

impl Drop for Instance {
    fn drop(&mut self) {
        unsafe { self.instance.destroy_instance(None) }
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
    fn get_name(&self) -> &str;

    fn iterate(&mut self, _scene: &mut ren::SceneFrame) {}
}

pub fn run<A: App + 'static>(mut app: A) {
    let app_name = app.get_name();

    println!("Create EventLoop");
    let event_loop = EventLoop::new();

    let window = WindowBuilder::new()
        .with_inner_size(PhysicalSize::<u32>::from((1280, 720)))
        .with_title(app_name)
        .build(&event_loop)
        .unwrap();

    println!("Load Vulkan");
    let vk = unsafe { ash::Entry::load() }.unwrap();
    println!("Create VkInstance");
    let instance = Instance::new(vk, event_loop.raw_display_handle(), app_name);

    println!("Select VkPhysicalDevice #0");
    let adapter = instance.select_adapter();
    println!("Running on {}", instance.get_device_name(adapter));
    println!("Create ren::Device");
    let get_instance_proc_addr: ren::vk::PFN_vkGetInstanceProcAddr =
        unsafe { std::mem::transmute(instance.get_loader()) };
    let mut device = unsafe {
        ren::vk::create_device(
            get_instance_proc_addr,
            instance.as_raw() as ren::vk::VkInstance,
            adapter.as_raw() as ren::vk::VkPhysicalDevice,
        )
    };

    println!("Create VkSurfaceKHR");
    let surface = Surface::new(
        &instance.entry,
        &instance.instance,
        window.raw_display_handle(),
        window.raw_window_handle(),
    );
    println!("Create ren::Swapchain");
    let mut swapchain =
        unsafe { ren::vk::create_swapchain(&device, surface.as_raw() as ren::vk::VkSurfaceKHR) };

    println!("Create ren::Scene");
    let mut scene = ren::Scene::new(&device);

    event_loop.run(move |event, _, control_flow| {
        control_flow.set_poll();
        match event {
            Event::WindowEvent {
                event: WindowEvent::CloseRequested,
                ..
            } => {
                control_flow.set_exit();
            }
            Event::MainEventsCleared => {
                let PhysicalSize { width, height } = window.inner_size();
                swapchain.set_size(width, height);

                let device = ren::DeviceFrame::new(&mut device);

                let mut scene = ren::SceneFrame::new(&device, &mut scene, &mut swapchain);
                scene.set_output_size(width, height);

                app.iterate(&mut scene);
            }
            Event::LoopDestroyed => {
                println!("Done");
            }
            _ => (),
        }
    });
}
