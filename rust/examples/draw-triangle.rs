use anyhow::Result;

mod utils;

struct DrawTriangleApp {
    _model: ren::Model,
}

impl utils::App for DrawTriangleApp {
    fn new(scene: &ren::SceneFrame) -> Result<Self> {
        let positions = [
            [0.0, 0.5, 0.0],
            [-(3.0f32).sqrt() / 4.0, -0.25, 0.0],
            [3.0f32.sqrt() / 4.0, -0.25, 0.0],
        ];

        let colors = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]];

        let indices = [0, 1, 2];

        let mesh = scene.create_mesh(&ren::MeshDesc {
            positions: &positions,
            colors: Some(&colors),
            indices: &indices,
        })?;

        let material = scene.create_material(&ren::MaterialDesc {
            albedo: ren::MaterialAlbedo::Const([1.0, 0.0, 0.0]),
        })?;

        Ok(Self {
            _model: scene.create_model(ren::ModelDesc { mesh, material })?,
        })
    }

    fn get_name(&self) -> &str {
        "Draw Triangle"
    }

    fn iterate(&mut self, scene: &ren::SceneFrame) -> Result<()> {
        scene.set_camera(&ren::CameraDesc {
            projection: ren::CameraProjection::Orthographic { width: 2.0 },
            position: [0.0, 0.0, 1.0],
            forward: [0.0, 0.0, -1.0],
            up: [0.0, 1.0, 0.0],
        });
        Ok(())
    }
}

fn main() -> Result<()> {
    utils::run::<DrawTriangleApp>()
}
