struct CreateDeviceApp {}

impl examples::App for CreateDeviceApp {
    fn new(_scene: &ren::SceneFrame) -> Self {
        Self {}
    }

    fn get_name(&self) -> &str {
        "Create Device"
    }
}

fn main() {
    examples::run::<CreateDeviceApp>();
}
