use ash::{
    extensions::khr,
    vk::{self, Handle},
};
use std::ffi::{CStr, CString};

use renderer2 as ren;

struct Instance<'e> {
    entry: &'e ash::Entry,
    instance: ash::Instance,
}

impl<'e> Instance<'e> {
    fn new(entry: &'e ash::Entry, window: &sdl2::video::Window, app_name: &str) -> Self {
        let layers = ren::vk::get_required_layers();

        let window_extensions: Vec<_> = window
            .vulkan_instance_extensions()
            .unwrap()
            .iter()
            .map(|s| CString::new(*s).unwrap())
            .collect();
        let extensions: Vec<_> = ren::vk::get_required_extensions()
            .iter()
            .copied()
            .chain(window_extensions.iter().map(|cstr| cstr.as_ptr()))
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
            entry,
            instance: unsafe { entry.create_instance(&create_info, None) }.unwrap(),
        }
    }

    fn get(&self) -> &ash::Instance {
        &self.instance
    }

    fn as_raw(&self) -> u64 {
        self.instance.handle().as_raw()
    }

    fn select_device(&self) -> vk::PhysicalDevice {
        *unsafe { self.instance.enumerate_physical_devices() }
            .unwrap()
            .first()
            .unwrap()
    }

    fn get_device_name(&self, device: vk::PhysicalDevice) -> String {
        let props = unsafe { self.instance.get_physical_device_properties(device) };
        let name_cstr = unsafe { CStr::from_ptr(props.device_name.as_ptr()) };
        name_cstr.to_str().unwrap().to_string()
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

impl<'e> Drop for Instance<'e> {
    fn drop(&mut self) {
        unsafe { self.instance.destroy_instance(None) }
    }
}

struct Surface<'e> {
    entry: &'e khr::Surface,
    surface: vk::SurfaceKHR,
}

impl<'e> Surface<'e> {
    fn new(
        entry: &'e khr::Surface,
        instance: &ash::Instance,
        window: &sdl2::video::Window,
    ) -> Self {
        Self {
            entry,
            surface: vk::SurfaceKHR::from_raw(
                window
                    .vulkan_create_surface(instance.handle().as_raw() as sdl2::video::VkInstance)
                    .unwrap(),
            ),
        }
    }

    fn as_raw(&self) -> u64 {
        self.surface.as_raw()
    }
}

impl<'e> Drop for Surface<'e> {
    fn drop(&mut self) {
        unsafe {
            self.entry.destroy_surface(self.surface, None);
        }
    }
}

fn main() {
    let app_name = "Create Device (Vulkan)";

    println!("Create sdl2::Window");
    let sdl_context = sdl2::init().unwrap();
    let video_subsystem = sdl_context.video().unwrap();
    let window = video_subsystem
        .window(app_name, 1280, 720)
        .resizable()
        .vulkan()
        .build()
        .unwrap();

    println!("Load Vulkan");
    let entry = unsafe { ash::Entry::load() }.unwrap();
    println!("Create VkInstance");
    let instance = Instance::new(&entry, &window, app_name);

    println!("Load VK_KHR_surface");
    let khr_surface = khr::Surface::new(&entry, instance.get());
    println!("Create VkSurfaceKHR");
    let surface = Surface::new(&khr_surface, instance.get(), &window);

    println!("Select VkPhysicalDevice");
    let physical_device = instance.select_device();
    println!("Running on {}", instance.get_device_name(physical_device));

    println!("Create ren::Device");
    let get_instance_proc_addr: ren::vk::PFN_vkGetInstanceProcAddr =
        unsafe { std::mem::transmute(instance.get_loader()) };
    let device = unsafe {
        ren::vk::create_device(
            get_instance_proc_addr,
            instance.as_raw() as ren::vk::VkInstance,
            physical_device.as_raw() as ren::vk::VkPhysicalDevice,
        )
    };

    println!("Create ren::Swapchain");
    let swapchain =
        unsafe { ren::vk::create_swapchain(&device, surface.as_raw() as ren::vk::VkSurfaceKHR) };

    println!("Create ren::Scene");
    let mut scene = ren::Scene::new(&device);
    scene.set_swapchain(swapchain);

    let mut quit: bool = false;
    while !quit {
        for e in sdl_context.event_pump().unwrap().poll_iter() {
            match e {
                sdl2::event::Event::Quit { .. } => quit = true,
                _ => {}
            }
        }

        let (width, height) = window.drawable_size();
        scene.set_output_size(width, height);
        scene.get_swapchain_mut().unwrap().set_size(width, height);

        scene.draw();
    }

    println!("Done");
}
