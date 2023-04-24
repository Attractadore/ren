use anyhow::Result;
use ren::{
    CameraDesc, CameraProjection, DirLightDesc, MaterialDesc, MeshDesc, MeshInstDesc, Scene,
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

        let normals = [[0.0, 0.0, 1.0], [0.0, 0.0, 1.0], [0.0, 0.0, 1.0]];

        let indices = [0, 1, 2];

        let mesh = scene.create_mesh(&MeshDesc {
            positions: &positions,
            normals: Some(&normals),
            indices: Some(&indices),
            ..Default::default()
        })?;

        let material = scene.create_material(&MaterialDesc {
            base_color_factor: [1.0, 0.0, 0.0, 1.0],
            metallic_factor: 0.0,
            roughness_factor: 1.0,
            ..Default::default()
        })?;

        scene.create_mesh_inst(&MeshInstDesc {
            mesh,
            material,
            casts_shadows: false,
        })?;

        scene.create_dir_light(&DirLightDesc {
            color: [1.0, 1.0, 1.0],
            illuminance: 1.0,
            origin: [0.0, 0.0, 1.0],
        })?;

        Ok(Self {})
    }
}

impl App for DrawTriangleApp {
    fn get_title(&self) -> &str {
        "Draw Triangle"
    }

    fn handle_frame(&mut self, scene: &mut Scene, width: u32, height: u32) -> Result<()> {
        scene.set_camera(&CameraDesc {
            projection: CameraProjection::Orthographic { width: 2.0 },
            width,
            height,
            position: [0.0, 0.0, 1.0],
            forward: [0.0, 0.0, -1.0],
            up: [0.0, 1.0, 0.0],
            ..Default::default()
        })?;
        Ok(())
    }
}

fn main() -> Result<()> {
    let mut base = AppBase::new()?;
    let app = DrawTriangleApp::new(base.get_scene_mut())?;
    base.run(app);
}
