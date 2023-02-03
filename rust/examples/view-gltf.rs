use anyhow::{anyhow, bail, Context, Result};
use clap::Parser;
use glam::{Mat4, Vec3, Vec4};
use ren::{
    CameraDesc, CameraProjection, MaterialAlbedo, MaterialDesc, MaterialKey, MeshDesc,
    MeshInstanceDesc, MeshInstanceKey, MeshKey, Scene,
};
use std::{
    fmt::{self, Display, Formatter},
    path::PathBuf,
    slice,
};
use utils::{App, AppBase};

mod utils;

struct SceneWalkContext<'a> {
    scene: &'a mut Scene,
    buffers: Vec<gltf::buffer::Data>,
}

impl<'a> SceneWalkContext<'a> {
    fn new(scene: &'a mut Scene, buffers: Vec<gltf::buffer::Data>) -> Self {
        Self { scene, buffers }
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

struct MeshFeatures {
    colors: bool,
}

struct Mesh {
    key: MeshKey,
    features: MeshFeatures,
}

fn create_mesh(primitive: &gltf::Primitive, ctx: &mut SceneWalkContext) -> Result<Mesh> {
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

    let colors = primitive
        .get(&gltf::Semantic::Colors(0))
        .map(get_data)
        .transpose()?;
    let colors = colors
        .map(|colors| match colors {
            Data::Float3(colors) => Ok(colors),
            Data::Float4(colors) => {
                eprintln!("Warn: converting colors from float4 to float3");
                Ok(colors.into_iter().map(|vec4| vec4.truncate()).collect())
            }
            format => bail!("Unsupported mesh color format: {format}"),
        })
        .transpose()?;

    let indices = get_data(
        primitive
            .indices()
            .context("Mesh doesn't contain indices")?,
    )?;
    let indices = match indices {
        Data::UShort(indices) => {
            eprintln!("Warn: converting indices from uint to ushort");
            indices.into_iter().map(|idx| idx as u32).collect()
        }
        Data::UInt(indices) => indices,
        format => bail!("Invalid mesh index format: {format}"),
    };

    let desc = MeshDesc {
        positions: unsafe {
            slice::from_raw_parts(positions.as_ptr() as *const [f32; 3], positions.len())
        },
        colors: colors.as_ref().map(|colors| unsafe {
            slice::from_raw_parts(colors.as_ptr() as *const [f32; 3], colors.len())
        }),
        indices: &indices,
    };

    let key = ctx.scene.create_mesh(&desc)?;
    let features = MeshFeatures {
        colors: colors.is_some(),
    };

    Ok(Mesh { key, features })
}

fn create_material(
    material: &gltf::Material,
    mesh_features: &MeshFeatures,
    ctx: &mut SceneWalkContext,
) -> Result<MaterialKey> {
    let desc = MaterialDesc {
        albedo: if mesh_features.colors {
            MaterialAlbedo::Vertex
        } else {
            let color = material.pbr_metallic_roughness().base_color_factor();
            MaterialAlbedo::Const([color[0], color[1], color[2]])
        },
    };

    let key = ctx.scene.create_material(&desc)?;

    Ok(key)
}

fn create_mesh_instance(
    primitive: &gltf::Primitive,
    ctx: &mut SceneWalkContext,
) -> Result<MeshInstanceKey> {
    let Mesh {
        key: mesh,
        features,
    } = create_mesh(primitive, ctx)?;
    let material = create_material(&primitive.material(), &features, ctx)?;
    let key = ctx
        .scene
        .create_mesh_instance(&MeshInstanceDesc { mesh, material })?;
    Ok(key)
}

fn visit_node(node: &gltf::Node, matrix: Mat4, ctx: &mut SceneWalkContext) -> Result<()> {
    let matrix = matrix * Mat4::from_cols_array_2d(&node.transform().matrix());
    if let Some(mesh) = node.mesh() {
        let matrix = matrix.to_cols_array();
        for primitive in mesh.primitives() {
            let mesh_instance = create_mesh_instance(&primitive, ctx)?;
            let mesh_instance = ctx.scene.get_mesh_instance_mut(mesh_instance).unwrap();
            mesh_instance.set_matrix(&matrix);
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
}

impl ViewGLTFApp {
    fn new(config: Config, scene: &mut Scene) -> Result<Self> {
        let gltf_scene = config.doc.scenes().nth(config.scene).unwrap();
        let mut ctx = SceneWalkContext::new(scene, config.buffers);
        let matrix = Mat4::IDENTITY;
        for node in gltf_scene.nodes() {
            visit_node(&node, matrix, &mut ctx)?;
        }
        scene.set_camera(&CameraDesc {
            projection: CameraProjection::Perspective {
                hfov: 90f32.to_radians(),
            },
            position: [-3.0, 0.0, 3.0],
            forward: [1.0, 0.0, -1.0],
            up: [0.0, 0.0, 1.0],
        });
        Ok(Self { desc: config.desc })
    }
}

impl App for ViewGLTFApp {
    fn get_title(&self) -> &str {
        &self.desc
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
    base.run(app);
}
