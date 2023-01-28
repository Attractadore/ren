use anyhow::Result;

mod utils;

struct CreateDeviceApp {}

impl utils::App for CreateDeviceApp {
    fn new(_scene: &ren::SceneFrame) -> Result<Self> {
        Ok(Self {})
    }

    fn get_name(&self) -> &str {
        "Create Device"
    }
}

fn main() -> Result<()> {
    utils::run::<CreateDeviceApp>()
}
