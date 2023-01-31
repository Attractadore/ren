use anyhow::Result;
use ren::{CameraDesc, CameraProj, MaterialDesc, MeshDesc, MeshInstanceDesc, SceneFrame};

mod utils;

struct DrawTriangleApp {}

impl utils::App for DrawTriangleApp {
    fn new(scene: &mut SceneFrame) -> Result<Self> {
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
            albedo: ren::MaterialAlbedo::Const([1.0, 0.0, 0.0]),
        })?;

        let _model = scene.create_mesh_instance(&MeshInstanceDesc { mesh, material })?;

        Ok(Self {})
    }

    fn get_name(&self) -> &str {
        "Draw Triangle"
    }

    fn iterate(&mut self, scene: &mut SceneFrame) -> Result<()> {
        scene.set_camera(&CameraDesc {
            proj: CameraProj::Ortho { width: 2.0 },
            pos: [0.0, 0.0, 1.0],
            fwd: [0.0, 0.0, -1.0],
            up: [0.0, 1.0, 0.0],
        });
        Ok(())
    }
}

fn main() -> Result<()> {
    utils::run::<DrawTriangleApp>()
}
