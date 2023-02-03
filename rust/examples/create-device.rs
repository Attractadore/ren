use anyhow::Result;
use utils::{App, AppBase};

mod utils;

struct CreateDeviceApp {}

impl App for CreateDeviceApp {
    fn get_title(&self) -> &str {
        "Create Device"
    }
}

fn main() -> Result<()> {
    let base = AppBase::new()?;
    base.run(CreateDeviceApp {});
}
