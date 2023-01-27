struct CreateDeviceApp {}

impl examples::App for CreateDeviceApp {
    fn get_name(&self) -> &str {
        "Create Device"
    }
}

fn main() {
    examples::run(CreateDeviceApp {});
}
