use anyhow::{anyhow, bail, Context, Result};
use clap::Parser;
use glam::{Mat4, Vec3, Vec3A, Vec4};
use ren::{
    CameraDesc, CameraProjection, DirLightDesc, MaterialDesc, MaterialID, MeshDesc, MeshID,
    MeshInstDesc, MeshInstID, Scene,
};
use std::{
    collections::HashMap,
    fmt::{self, Display, Formatter},
    path::PathBuf,
    slice,
    time::Instant,
};
use utils::{App, AppBase, Camera, CameraController};
use winit::event::{DeviceEvent, WindowEvent};

mod utils;

struct SceneWalkContext<'a> {
    scene: &'a mut Scene,
    buffers: Vec<gltf::buffer::Data>,
    materials: HashMap<Option<usize>, MaterialID>,
}

impl<'a> SceneWalkContext<'a> {
    fn new(scene: &'a mut Scene, buffers: Vec<gltf::buffer::Data>) -> Self {
        Self {
            scene,
            buffers,
            materials: HashMap::new(),
        }
    }
}

#[derive(Debug)]
enum Data {
    UShort(Vec<u16>),
    UInt(Vec<u32>),
    Float3(Vec<Vec3>),
    Float4(Vec<Vec4>),
}

impl Display for Data {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                Data::UShort(_) => "ushort",
                Data::UInt(_) => "uint",
                Data::Float3(_) => "float3",
                Data::Float4(_) => "float4",
            }
        )
    }
}

impl From<Vec<u16>> for Data {
    fn from(value: Vec<u16>) -> Self {
        Data::UShort(value)
    }
}

impl From<Vec<u32>> for Data {
    fn from(value: Vec<u32>) -> Self {
        Data::UInt(value)
    }
}

impl From<Vec<Vec3>> for Data {
    fn from(value: Vec<Vec3>) -> Self {
        Data::Float3(value)
    }
}

impl From<Vec<Vec4>> for Data {
    fn from(value: Vec<Vec4>) -> Self {
        Data::Float4(value)
    }
}

fn get_accessor_formatted_data<T: Copy>(
    accessor: &gltf::Accessor,
    ctx: &SceneWalkContext,
) -> Result<Data>
where
    Data: From<Vec<T>>,
{
    let view = if let Some(view) = accessor.view() {
        view
    } else {
        bail!("Sparse accessor");
    };

    let offset = accessor.offset() + view.offset();
    let buffer = &ctx.buffers[view.buffer().index()];
    let data = if let Some(stride) = view.stride() {
        let mut vec = Vec::with_capacity(accessor.count());
        let ptr = unsafe { buffer.as_ptr().add(offset) };
        for idx in 0..accessor.count() {
            assert!(offset + idx * stride + std::mem::size_of::<T>() < buffer.len());
            vec.push(unsafe { (ptr.add(idx * stride) as *const T).read() });
        }
        vec
    } else {
        unsafe {
            let ptr = (buffer.as_ptr().add(offset)) as *const T;
            slice::from_raw_parts(ptr, accessor.count())
        }
        .to_vec()
    };

    Ok(Data::from(data))
}

fn get_accessor_data(accessor: &gltf::Accessor, ctx: &SceneWalkContext) -> Result<Data> {
    match (accessor.data_type(), accessor.dimensions()) {
        (gltf::accessor::DataType::U16, gltf::accessor::Dimensions::Scalar) => {
            get_accessor_formatted_data::<u16>(accessor, ctx)
        }
        (gltf::accessor::DataType::U32, gltf::accessor::Dimensions::Scalar) => {
            get_accessor_formatted_data::<u32>(accessor, ctx)
        }
        (gltf::accessor::DataType::F32, gltf::accessor::Dimensions::Vec3) => {
            get_accessor_formatted_data::<Vec3>(accessor, ctx)
        }
        (gltf::accessor::DataType::F32, gltf::accessor::Dimensions::Vec4) => {
            get_accessor_formatted_data::<Vec4>(accessor, ctx)
        }
        (data_type, dimensions) => Err(anyhow!(
            "Unsupported data type {:?}/{:?}",
            data_type,
            dimensions
        )),
    }
}

fn create_mesh(primitive: &gltf::Primitive, ctx: &mut SceneWalkContext) -> Result<MeshID> {
    let mode = primitive.mode();
    if !matches!(mode, gltf::mesh::Mode::Triangles) {
        bail!("Unsupported mesh mode {:?}", mode);
    }

    if primitive.morph_targets().next().is_some() {
        eprintln!("Warn: ignoring mesh morph targets");
    }

    let get_data = |accessor| get_accessor_data(&accessor, ctx);

    let positions = get_data(
        primitive
            .get(&gltf::Semantic::Positions)
            .context("Mesh doesn't contain vertices")?,
    )?;
    let positions = match positions {
        Data::Float3(positions) => positions,
        format => bail!("Unsupported mesh position format: {format}"),
    };

    let normals = get_data(
        primitive
            .get(&gltf::Semantic::Normals)
            .context("Mesh doesn't contain vertices")?,
    )?;
    let normals = match normals {
        Data::Float3(normals) => normals,
        format => bail!("Unsupported mesh position format: {format}"),
    };

    let colors = primitive
        .get(&gltf::Semantic::Colors(0))
        .map(get_data)
        .transpose()?;
    let colors = colors
        .map(|colors| match colors {
            Data::Float3(colors) => {
                eprintln!("Warn: converting colors from float3 to float4");
                Ok(colors.into_iter().map(|vec3| vec3.extend(1.0)).collect())
            }
            Data::Float4(colors) => Ok(colors),
            format => bail!("Unsupported mesh color format: {format}"),
        })
        .transpose()?;

    let indices = primitive.indices().map_or_else(
        || {
            eprintln!("Warn: generating mesh indices");
            Ok(Data::UInt((0..(positions.len() as u32)).collect()))
        },
        get_data,
    )?;

    let indices = match indices {
        Data::UShort(indices) => {
            eprintln!("Warn: converting indices from ushort to uint");
            indices.into_iter().map(|idx| idx as u32).collect()
        }
        Data::UInt(indices) => indices,
        format => bail!("Invalid mesh index format: {format}"),
    };

    let desc = MeshDesc {
        positions: unsafe {
            slice::from_raw_parts(positions.as_ptr() as *const [f32; 3], positions.len())
        },
        normals: unsafe {
            Some(slice::from_raw_parts(
                normals.as_ptr() as *const [f32; 3],
                normals.len(),
            ))
        },
        colors: colors.as_ref().map(|colors| unsafe {
            slice::from_raw_parts(colors.as_ptr() as *const [f32; 4], colors.len())
        }),
        indices: Some(&indices),
        ..Default::default()
    };

    let mesh = ctx.scene.create_mesh(&desc)?;

    Ok(mesh)
}

fn create_material(material: &gltf::Material, ctx: &mut SceneWalkContext) -> Result<MaterialID> {
    let material_name = material.index().map_or_else(
        || "default material".to_string(),
        |index| format!("material #{index}"),
    );
    if let Some(alpha_cutoff) = material.alpha_cutoff() {
        eprintln!("Warn: ignoring alpha cutoff {alpha_cutoff} for {material_name}");
    }

    let alpha_mode = material.alpha_mode();
    if !matches!(alpha_mode, gltf::material::AlphaMode::Opaque) {
        eprintln!("Warn: ignoring alpha mode {alpha_mode:?} for {material_name}");
    }

    let double_sided = material.double_sided();
    if double_sided {
        eprintln!("Warn: ignoring double sidedness for {material_name}");
    }

    let pbr = material.pbr_metallic_roughness();

    if pbr.base_color_texture().is_some() {
        eprintln!("Warn: ignoring base color texture for {material_name}");
    }

    if pbr.metallic_roughness_texture().is_some() {
        eprintln!("Warn: ignoring metallic / roughness texture for {material_name}",);
    }

    if material.normal_texture().is_some() {
        eprintln!("Warn: ignoring normal texture for {material_name}",);
    }

    if material.occlusion_texture().is_some() {
        eprintln!("Warn: ignoring occlusion texture for {material_name}");
    }

    if material.emissive_texture().is_some() {
        eprintln!("Warn: ignoring emissive texture for {material_name}",);
    }

    let emissive_factor = material.emissive_factor();
    if emissive_factor != [0.0, 0.0, 0.0] {
        eprintln!("Warn: ignoring emissive factor {emissive_factor:?} for {material_name}");
    }

    let desc = MaterialDesc {
        base_color_factor: pbr.base_color_factor(),
        metallic_factor: pbr.metallic_factor(),
        roughness_factor: pbr.roughness_factor(),
        ..Default::default()
    };

    let material = ctx.scene.create_material(&desc)?;

    Ok(material)
}

fn create_mesh_instance(
    primitive: &gltf::Primitive,
    ctx: &mut SceneWalkContext,
) -> Result<MeshInstID> {
    let mesh = create_mesh(primitive, ctx)?;
    let material = primitive.material();
    let index = material.index();
    let material = if let Some(material) = ctx.materials.get(&index) {
        *material
    } else {
        let material = create_material(&material, ctx)?;
        ctx.materials.insert(index, material);
        material
    };
    let mesh_inst = ctx.scene.create_mesh_inst(&MeshInstDesc {
        mesh,
        material,
        casts_shadows: true,
    })?;
    Ok(mesh_inst)
}

fn visit_node(node: &gltf::Node, matrix: Mat4, ctx: &mut SceneWalkContext) -> Result<()> {
    let matrix = matrix * Mat4::from_cols_array_2d(&node.transform().matrix());
    if let Some(mesh) = node.mesh() {
        if mesh.weights().is_some() {
            eprintln!("Warn: ignoring mesh morph target weights");
        }
        let matrix = matrix.to_cols_array_2d();
        for primitive in mesh.primitives() {
            let mesh_instance = create_mesh_instance(&primitive, ctx);
            match mesh_instance {
                Ok(mesh_inst) => {
                    ctx.scene.set_mesh_inst_matrix(mesh_inst, &matrix);
                }
                Err(err) => eprintln!(
                    "Error: failed to create mesh for primitive #{}: {err}",
                    primitive.index()
                ),
            }
        }
    }
    for node in node.children() {
        visit_node(&node, matrix, ctx)?;
    }
    Ok(())
}

struct Config {
    desc: String,
    doc: gltf::Document,
    buffers: Vec<gltf::buffer::Data>,
    _images: Vec<gltf::image::Data>,
    scene: usize,
}

struct ViewGLTFApp {
    desc: String,
    controller: CameraController,
    time: Instant,
}

impl ViewGLTFApp {
    fn new(config: Config, scene: &mut Scene) -> Result<Self> {
        let gltf_scene = config.doc.scenes().nth(config.scene).unwrap();
        let mut ctx = SceneWalkContext::new(scene, config.buffers);
        let matrix = Mat4::IDENTITY;
        for node in gltf_scene.nodes() {
            visit_node(&node, matrix, &mut ctx)?;
        }
        scene.create_dir_light(&DirLightDesc {
            color: [1.0, 1.0, 1.0],
            illuminance: 1.0,
            origin: [0.0, 0.0, 1.0],
        })?;
        let mut camera = Camera::new();
        camera.position = Vec3A::new(-3.0, 0.0, 3.0);
        camera.forward = Vec3A::new(1.0, 0.0, -1.0).normalize();
        let controller = CameraController::new(camera);
        Ok(Self {
            desc: config.desc,
            controller,
            time: Instant::now(),
        })
    }
}

impl App for ViewGLTFApp {
    fn get_title(&self) -> &str {
        &self.desc
    }

    fn handle_window_event(&mut self, event: &WindowEvent) -> Result<()> {
        if let WindowEvent::KeyboardInput { input, .. } = event {
            self.controller.handle_key_input(input);
        }
        Ok(())
    }

    fn handle_device_event(&mut self, event: &DeviceEvent) -> Result<()> {
        if let DeviceEvent::MouseMotion { delta: (dx, dy) } = event {
            self.controller
                .handle_mouse_motion((*dx as f32, *dy as f32))
        }
        Ok(())
    }

    fn handle_frame(&mut self, scene: &mut Scene, width: u32, height: u32) -> Result<()> {
        let now = Instant::now();
        let dt = now - self.time;
        self.time = now;
        self.controller.consume_movement_input(dt.as_secs_f32());
        self.controller.consume_rotation_input();
        scene.set_camera(&CameraDesc {
            projection: CameraProjection::Perspective {
                hfov: 90f32.to_radians(),
            },
            width,
            height,
            position: self.controller.camera.position.to_array(),
            forward: self.controller.camera.get_forward_vector().to_array(),
            up: self.controller.camera.get_up_vector().to_array(),
            ..Default::default()
        })?;
        Ok(())
    }
}

#[derive(Parser)]
struct CliOpts {
    /// .gltf/.glb file to open
    file: PathBuf,

    /// Index of the scene to view
    #[arg(short, long, value_name = "index")]
    scene: Option<usize>,
}

fn main() -> Result<()> {
    let opts = CliOpts::parse();
    let (doc, buffers, images) = gltf::import(&opts.file)?;

    let scene = if let Some(index) = opts.scene {
        doc.scenes()
            .find(|s| s.index() == index)
            .context(format!("Failed to find scene #{index} in document"))?
    } else if let Some(scene) = doc.default_scene() {
        scene
    } else {
        let index = 0;
        doc.scenes()
            .find(|s| s.index() == index)
            .context("No scenes in document")?
    };

    let desc = scene.name().map_or_else(
        || format!("{} Scene #{}", opts.file.display(), scene.index()),
        |name| name.to_string(),
    );

    let mut base = AppBase::new()?;
    let app = ViewGLTFApp::new(
        Config {
            desc,
            scene: scene.index(),
            doc,
            buffers,
            _images: images,
        },
        base.get_scene_mut(),
    )?;

    let window = base.get_window();
    let result = window.set_cursor_grab(winit::window::CursorGrabMode::Locked);
    if let Err(winit::error::ExternalError::NotSupported(_)) = result {
        window.set_cursor_grab(winit::window::CursorGrabMode::Confined)?;
    } else {
        result?;
    }
    window.set_cursor_visible(false);

    base.run(app);
}
