mod ffi;
pub mod vk;

use ffi::{
    RenCameraDesc, RenDevice, RenMaterial, RenMaterialAlbedo, RenMaterialDesc, RenMesh,
    RenMeshDesc, RenModel, RenModelDesc, RenOrthographicProjection, RenPerspectiveProjection,
    RenProjection, RenResult, RenScene, RenSwapchain,
};
use std::cell::{RefCell, RefMut};
use std::marker::PhantomData;
use std::rc::Rc;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum Error {
    #[error("Vulkan runtime error")]
    Vulkan,
    #[error("System error")]
    System,
    #[error("C++ runtime error")]
    Runtime,
    #[error("Unknown error")]
    Unknown,
}

impl Error {
    fn new(result: RenResult) -> Result<(), Self> {
        match result {
            RenResult::REN_SUCCESS => Ok(()),
            RenResult::REN_VULKAN_ERROR => Err(Error::Vulkan),
            RenResult::REN_SYSTEM_ERROR => Err(Error::System),
            RenResult::REN_RUNTIME_ERROR => Err(Error::Runtime),
            _ => Err(Error::Unknown),
        }
    }
}

trait DestroyHandle {
    unsafe fn destroy(ptr: *mut Self);
}

#[derive(Clone)]
struct Handle<T: DestroyHandle>(*mut T);

impl<T: DestroyHandle> Handle<T> {
    unsafe fn new(handle: *mut T) -> Self {
        Self(handle)
    }
}

impl<T: DestroyHandle> Drop for Handle<T> {
    fn drop(&mut self) {
        unsafe { T::destroy(self.0) }
    }
}

struct FrameGuard(RefCell<()>);
struct FrameLock<'a>(RefMut<'a, ()>);

impl FrameGuard {
    fn new() -> Self {
        FrameGuard(RefCell::new(()))
    }

    fn lock(&self) -> FrameLock {
        FrameLock(self.0.borrow_mut())
    }
}

type HDevice = Handle<RenDevice>;

impl DestroyHandle for RenDevice {
    unsafe fn destroy(device: *mut RenDevice) {
        ffi::ren_DestroyDevice(device)
    }
}

pub struct Device {
    handle: HDevice,
    guard: FrameGuard,
}

impl Device {
    unsafe fn new(device: *mut RenDevice) -> Rc<Self> {
        Rc::new(Self {
            handle: HDevice::new(device),
            guard: FrameGuard::new(),
        })
    }
}

pub struct DeviceFrame<'a> {
    device: &'a Device,
    _lock: FrameLock<'a>,
}

impl<'a> DeviceFrame<'a> {
    pub fn new(device: &'a Device) -> Result<Self, Error> {
        Error::new(unsafe { ffi::ren_DeviceBeginFrame(device.handle.0) })?;
        Ok(Self {
            device,
            _lock: device.guard.lock(),
        })
    }

    pub fn end(self) -> Result<(), Error> {
        Error::new(unsafe { ffi::ren_DeviceEndFrame(self.device.handle.0) })
    }
}

impl<'a> Drop for DeviceFrame<'a> {
    fn drop(&mut self) {
        if !std::thread::panicking() {
            Error::new(unsafe { ffi::ren_DeviceEndFrame(self.device.handle.0) }).unwrap();
        }
    }
}

type HSwapchain = Handle<RenSwapchain>;

impl DestroyHandle for RenSwapchain {
    unsafe fn destroy(swapchain: *mut RenSwapchain) {
        ffi::ren_DestroySwapchain(swapchain)
    }
}

pub struct Swapchain {
    handle: HSwapchain,
    device: Rc<Device>,
}

impl Swapchain {
    unsafe fn new(device: Rc<Device>, swapchain: *mut RenSwapchain) -> Self {
        Self {
            handle: HSwapchain::new(swapchain),
            device,
        }
    }

    pub fn set_size(&mut self, width: u32, height: u32) {
        unsafe { ffi::ren_SetSwapchainSize(self.handle.0, width, height) }
    }

    pub fn get_size(&self) -> (u32, u32) {
        let (mut width, mut height) = (0, 0);
        unsafe { ffi::ren_GetSwapchainSize(self.handle.0, &mut width, &mut height) };
        (width, height)
    }
}

type HScene = Handle<RenScene>;

impl DestroyHandle for RenScene {
    unsafe fn destroy(scene: *mut RenScene) {
        ffi::ren_DestroyScene(scene)
    }
}

pub struct Scene {
    handle: HScene,
    guard: FrameGuard,
    device: Rc<Device>,
}

impl Scene {
    pub fn new(device: Rc<Device>) -> Result<Rc<Self>, Error> {
        Ok(Rc::new(Self {
            handle: unsafe {
                HScene::new({
                    let mut scene = std::ptr::null_mut();
                    Error::new(ffi::ren_CreateScene(device.handle.0, &mut scene))?;
                    scene
                })
            },
            guard: FrameGuard::new(),
            device,
        }))
    }
}

pub struct SceneFrame<'a, 'd> {
    scene: Rc<Scene>,
    _lock: FrameLock<'a>,
    device: PhantomData<&'a DeviceFrame<'d>>,
    swapchain: PhantomData<&'a mut Swapchain>,
}

impl<'a, 'd> SceneFrame<'a, 'd> {
    pub fn new(
        device: &'a DeviceFrame<'d>,
        scene: &'a Rc<Scene>,
        swapchain: &'a mut Swapchain,
        width: u32,
        height: u32,
    ) -> Result<Self, Error> {
        Error::new(unsafe {
            assert_eq!(device.device.handle.0, scene.device.handle.0);
            assert_eq!(device.device.handle.0, swapchain.device.handle.0);
            ffi::ren_SceneBeginFrame(scene.handle.0, swapchain.handle.0)
        })?;
        Error::new(unsafe {
            assert!(width > 0);
            assert!(height > 0);
            ffi::ren_SetSceneOutputSize(scene.handle.0, width, height)
        })?;
        Ok(Self {
            scene: scene.clone(),
            _lock: scene.guard.lock(),
            device: PhantomData,
            swapchain: PhantomData,
        })
    }

    pub fn end(self) -> Result<(), Error> {
        Error::new(unsafe { ffi::ren_SceneEndFrame(self.scene.handle.0) })
    }
}

impl<'a, 'd> Drop for SceneFrame<'a, 'd> {
    fn drop(&mut self) {
        if !std::thread::panicking() {
            Error::new(unsafe { ffi::ren_SceneEndFrame(self.scene.handle.0) }).unwrap();
        }
    }
}

trait DestroySceneHandle {
    unsafe fn destroy(scene: *mut RenScene, handle: Self);
}

#[derive(Clone)]
struct SceneHandle<T: DestroySceneHandle + Copy> {
    handle: T,
    scene: Rc<Scene>,
}

impl<T: DestroySceneHandle + Copy> SceneHandle<T> {
    fn new(handle: T, scene: Rc<Scene>) -> Self {
        Self { handle, scene }
    }
}

impl<T: DestroySceneHandle + Copy> Drop for SceneHandle<T> {
    fn drop(&mut self) {
        unsafe { T::destroy(self.scene.handle.0, self.handle) }
    }
}

#[derive(Clone, Copy)]
struct MeshID(RenMesh);
type HMesh = SceneHandle<MeshID>;
pub struct Mesh {
    handle: HMesh,
}

impl DestroySceneHandle for MeshID {
    unsafe fn destroy(scene: *mut RenScene, mesh: MeshID) {
        ffi::ren_DestroyMesh(scene, mesh.0);
    }
}

pub struct MeshDesc<'a> {
    pub positions: &'a [[f32; 3]],
    pub colors: Option<&'a [[f32; 3]]>,
    pub indices: &'a [u32],
}

impl<'a, 'd> SceneFrame<'a, 'd> {
    pub fn create_mesh(&self, desc: &MeshDesc) -> Result<Rc<Mesh>, Error> {
        Ok(Rc::new(Mesh {
            handle: HMesh::new(
                MeshID({
                    let num_vertices = desc.positions.len();
                    let desc = RenMeshDesc {
                        num_vertices: num_vertices as u32,
                        num_indices: desc.indices.len() as u32,
                        positions: desc.positions.as_ptr() as *const f32,
                        colors: desc.colors.map_or(std::ptr::null(), |colors| {
                            assert!(colors.len() == num_vertices);
                            colors.as_ptr() as *const f32
                        }),
                        indices: desc.indices.as_ptr(),
                    };
                    let mut mesh = Default::default();
                    Error::new(unsafe {
                        ffi::ren_CreateMesh(self.scene.handle.0, &desc, &mut mesh)
                    })?;
                    mesh
                }),
                self.scene.clone(),
            ),
        }))
    }
}

#[derive(Clone, Copy)]
struct MaterialID(RenMaterial);
type HMaterial = SceneHandle<MaterialID>;
pub struct Material {
    handle: HMaterial,
}

impl DestroySceneHandle for MaterialID {
    unsafe fn destroy(scene: *mut RenScene, material: MaterialID) {
        ffi::ren_DestroyMaterial(scene, material.0);
    }
}

pub enum MaterialAlbedo {
    Const([f32; 3]),
    Vertex,
}

pub struct MaterialDesc {
    pub albedo: MaterialAlbedo,
}

impl<'a, 'd> SceneFrame<'a, 'd> {
    pub fn create_material(&self, desc: &MaterialDesc) -> Result<Rc<Material>, Error> {
        Ok(Rc::new(Material {
            handle: HMaterial::new(
                MaterialID({
                    let desc = RenMaterialDesc {
                        albedo: match desc.albedo {
                            MaterialAlbedo::Const(_) => {
                                RenMaterialAlbedo::REN_MATERIAL_ALBEDO_CONST
                            }
                            MaterialAlbedo::Vertex => RenMaterialAlbedo::REN_MATERIAL_ALBEDO_VERTEX,
                        },
                        __bindgen_anon_1: match desc.albedo {
                            MaterialAlbedo::Const(const_albedo) => {
                                ffi::RenMaterialDesc__bindgen_ty_1 { const_albedo }
                            }
                            MaterialAlbedo::Vertex => unsafe { std::mem::zeroed() },
                        },
                    };
                    let mut material = Default::default();
                    Error::new(unsafe {
                        ffi::ren_CreateMaterial(self.scene.handle.0, &desc, &mut material)
                    })?;
                    material
                }),
                self.scene.clone(),
            ),
        }))
    }
}

#[derive(Clone, Copy)]
struct ModelID(RenModel);
type HModel = SceneHandle<ModelID>;
pub struct Model {
    handle: HModel,
    _mesh: Rc<Mesh>,
    _material: Rc<Material>,
}

impl DestroySceneHandle for ModelID {
    unsafe fn destroy(scene: *mut RenScene, model: ModelID) {
        ffi::ren_DestroyModel(scene, model.0);
    }
}

pub struct ModelDesc {
    pub mesh: Rc<Mesh>,
    pub material: Rc<Material>,
}

impl<'a, 'd> SceneFrame<'a, 'd> {
    pub fn create_model(&self, desc: ModelDesc) -> Result<Model, Error> {
        Ok(Model {
            handle: HModel::new(
                ModelID({
                    let desc = RenModelDesc {
                        mesh: desc.mesh.handle.handle.0,
                        material: desc.material.handle.handle.0,
                    };
                    let mut model = Default::default();
                    Error::new(unsafe {
                        ffi::ren_CreateModel(self.scene.handle.0, &desc, &mut model)
                    })?;
                    model
                }),
                self.scene.clone(),
            ),
            _mesh: desc.mesh,
            _material: desc.material,
        })
    }
}

impl Model {
    pub fn set_matrix(&self, matrix: &[f32; 16]) {
        unsafe {
            ffi::ren_SetModelMatrix(
                self.handle.scene.handle.0,
                self.handle.handle.0,
                matrix.as_ptr(),
            )
        }
    }
}

pub enum CameraProjection {
    Perspective { hfov: f32 },
    Orthographic { width: f32 },
}

pub struct CameraDesc {
    pub projection: CameraProjection,
    pub position: [f32; 3],
    pub forward: [f32; 3],
    pub up: [f32; 3],
}

impl<'a, 'd> SceneFrame<'a, 'd> {
    pub fn set_camera(&self, camera: &CameraDesc) {
        let camera = RenCameraDesc {
            projection: match camera.projection {
                CameraProjection::Perspective { .. } => RenProjection::REN_PROJECTION_PERSPECTIVE,
                CameraProjection::Orthographic { .. } => RenProjection::REN_PROJECTION_ORTHOGRAPHIC,
            },
            __bindgen_anon_1: match camera.projection {
                CameraProjection::Perspective { hfov } => ffi::RenCameraDesc__bindgen_ty_1 {
                    perspective: RenPerspectiveProjection { hfov },
                },
                CameraProjection::Orthographic { width } => ffi::RenCameraDesc__bindgen_ty_1 {
                    orthographic: RenOrthographicProjection { width },
                },
            },
            position: camera.position,
            forward: camera.forward,
            up: camera.up,
        };
        unsafe { ffi::ren_SetSceneCamera(self.scene.handle.0, &camera) }
    }
}
