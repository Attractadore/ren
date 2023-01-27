use std::rc::Rc;

struct DrawTriangleApp {
    model: Option<ren::Model>,
}

impl DrawTriangleApp {
    fn new() -> Self {
        Self { model: None }
    }
}

impl examples::App for DrawTriangleApp {
    fn get_name(&self) -> &str {
        "Draw Triangle"
    }

    fn iterate(&mut self, scene: &mut ren::SceneFrame) {
        if self.model.is_none() {
            let positions = [
                [0.0, 0.5, 0.0],
                [-(3.0f32).sqrt() / 4.0, -0.25, 0.0],
                [3.0f32.sqrt() / 4.0, -0.25, 0.0],
            ];

            let colors = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]];

            let indices = [0, 1, 2];

            let mesh = Rc::new(scene.create_mesh(&ren::MeshDesc {
                positions: &positions,
                colors: Some(&colors),
                indices: &indices,
            }));

            let material = Rc::new(scene.create_material(&ren::MaterialDesc {
                albedo: ren::MaterialAlbedo::Const([1.0, 0.0, 0.0]),
            }));

            self.model = Some(scene.create_model(ren::ModelDesc { mesh, material }));
        }

        scene.set_camera(&ren::CameraDesc {
            projection: ren::CameraProjection::Orthographic { width: 2.0 },
            position: [0.0, 0.0, 1.0],
            forward: [0.0, 0.0, -1.0],
            up: [0.0, 1.0, 0.0],
        });
    }
}

fn main() {
    examples::run(DrawTriangleApp::new());
}
