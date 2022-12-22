use windows::core::PCWSTR;
use windows::Win32::Graphics::Dxgi;

use renderer2 as ren;

fn create_dxgi_factory() -> Dxgi::IDXGIFactory4 {
    unsafe { Dxgi::CreateDXGIFactory2(0).expect("DXGI: Failed to create factory") }
}

fn select_adapter(factory: &Dxgi::IDXGIFactory4) -> Dxgi::IDXGIAdapter1 {
    unsafe {
        factory
            .EnumAdapters1(0)
            .expect("DXGI: Failed to find adapter")
    }
}

fn get_adapter_name(adapter: &Dxgi::IDXGIAdapter1) -> String {
    unsafe {
        let desc = adapter
            .GetDesc1()
            .expect("DXGI: Failed to get adapter description");
        PCWSTR::from_raw(desc.Description.as_ptr())
            .to_string()
            .unwrap()
    }
}

fn get_adapter_luid(adapter: &Dxgi::IDXGIAdapter1) -> ren::dx12::LUID {
    unsafe {
        let desc = adapter
            .GetDesc1()
            .expect("DXGI: Failed to get adapter description");
        std::mem::transmute(desc.AdapterLuid)
    }
}

fn main() {
    let app_name = "Create Device (DirectX 12)";

    println!("Create sdl2::Window");
    let sdl_context = sdl2::init().unwrap();
    let video_subsystem = sdl_context.video().unwrap();
    let window = video_subsystem
        .window(app_name, 1280, 720)
        .resizable()
        .build()
        .unwrap();

    println!("Create IDXGIFactory4");
    let dxgi_factory = create_dxgi_factory();

    println!("Select IDXGIAdapter1");
    let adapter = select_adapter(&dxgi_factory);
    println!("Running on {}", get_adapter_name(&adapter));

    println!("Create ren::Device");
    let device = unsafe { ren::dx12::create_device(get_adapter_luid(&adapter)) };

    println!("Create ren::Swapchain");
    let swapchain = unsafe { ren::dx12::create_swapchain(&device, &window) };

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
