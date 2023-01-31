use anyhow::Result;
use ren::SceneFrame;

mod utils;

struct CreateDeviceApp {}

impl utils::App for CreateDeviceApp {
    fn new(_scene: &mut SceneFrame) -> Result<Self> {
        Ok(Self {})
    }

    fn get_name(&self) -> &str {
        "Create Device"
    }
}

fn main() -> Result<()> {
    utils::run::<CreateDeviceApp>()
}
