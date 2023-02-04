use anyhow::Result;
use ren::{
    CameraDesc, CameraProjection, MaterialColor, MaterialDesc, MeshDesc, MeshInstanceDesc, Scene,
};
use utils::{App, AppBase};

mod utils;

struct DrawTriangleApp {}

impl DrawTriangleApp {
    fn new(scene: &mut Scene) -> Result<Self> {
        let positions = [
            [0.0, 0.5, 0.0],
            [-(3.0f32).sqrt() / 4.0, -0.25, 0.0],
            [3.0f32.sqrt() / 4.0, -0.25, 0.0],
        ];

        let colors = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]];

        let indices = [0, 1, 2];

        let mesh = scene.create_mesh(&MeshDesc {
            positions: &positions,
            colors: Some(&colors),
            indices: &indices,
        })?;

        let material = scene.create_material(&MaterialDesc {
            color: MaterialColor::Const,
            base_color: [1.0, 0.0, 0.0, 1.0],
        })?;

        scene.create_mesh_instance(&MeshInstanceDesc { mesh, material })?;

        Ok(Self {})
    }
}

impl App for DrawTriangleApp {
    fn get_title(&self) -> &str {
        "Draw Triangle"
    }

    fn handle_frame(&mut self, scene: &mut Scene) -> Result<()> {
        scene.set_camera(&CameraDesc {
            projection: CameraProjection::Orthographic { width: 2.0 },
            position: [0.0, 0.0, 1.0],
            forward: [0.0, 0.0, -1.0],
            up: [0.0, 1.0, 0.0],
        });
        Ok(())
    }
}

fn main() -> Result<()> {
    let mut base = AppBase::new()?;
    let app = DrawTriangleApp::new(base.get_scene_mut())?;
    base.run(app);
}
