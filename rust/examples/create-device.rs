use anyhow::Result;
use ren::Scene;

mod utils;

struct Config {}
struct CreateDeviceApp {}

impl utils::App for CreateDeviceApp {
    type Config = Config;

    fn new(_config: Config, _scene: &mut Scene) -> Result<Self> {
        Ok(Self {})
    }

    fn get_name(&self) -> &str {
        "Create Device"
    }
}

fn main() -> Result<()> {
    utils::run::<CreateDeviceApp>(Config {})
}
