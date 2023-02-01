use slotmap::{new_key_type, SlotMap};
use std::collections::HashSet;
use std::ops::{Deref, DerefMut};
use thiserror::Error;

mod ffi;
use ffi::{
    RenCameraDesc, RenDevice, RenMaterial, RenMaterialDesc, RenMesh, RenMeshDesc, RenMeshInstance,
    RenMeshInstanceDesc, RenOrthographicProjection, RenPerspectiveProjection, RenResult, RenScene,
    RenSwapchain, REN_MATERIAL_ALBEDO_CONST, REN_MATERIAL_ALBEDO_VERTEX, REN_NULL_MATERIAL,
    REN_NULL_MESH, REN_NULL_MESH_INSTANCE, REN_PROJECTION_ORTHOGRAPHIC, REN_PROJECTION_PERSPECTIVE,
    REN_RUNTIME_ERROR, REN_SUCCESS, REN_SYSTEM_ERROR, REN_VULKAN_ERROR,
};

mod handle;
pub mod vk;

#[derive(Error, Debug)]
pub enum Error {
    #[error("Vulkan runtime error")]
    Vulkan,
    #[error("System error")]
    System,
    #[error("C++ runtime error")]
    Runtime,
    #[error("Specified object was not found")]
    InvalidKey,
    #[error("Specified object is in use")]
    Busy,
    #[error("Unknown error")]
    Unknown,
}

impl Error {
    fn new(result: RenResult) -> Result<(), Self> {
        match result {
            REN_SUCCESS => Ok(()),
            REN_VULKAN_ERROR => Err(Error::Vulkan),
            REN_SYSTEM_ERROR => Err(Error::System),
            REN_RUNTIME_ERROR => Err(Error::Runtime),
            _ => Err(Error::Unknown),
        }
    }
}

type HDevice = handle::Handle<RenDevice>;

impl handle::DestroyHandle for RenDevice {
    unsafe fn destroy(device: *mut RenDevice) {
        ffi::ren_DestroyDevice(device)
    }
}

pub struct Device {
    swapchains: SlotMap<SwapchainKey, Swapchain>,
    scenes: SlotMap<SceneKey, Scene>,
    handle: HDevice,
}

impl Device {
    /// # Safety
    ///
    /// Requires valid vkGetInstanceProcAddr, VkInstance and VkPhysicalDevice
    pub unsafe fn new(
        proc: vk::GetInstanceProcAddr,
        instance: vk::Instance,
        adapter: vk::PhysicalDevice,
    ) -> Result<Self, Error> {
        assert_ne!(proc, None);
        assert_ne!(instance, std::ptr::null_mut());
        assert_ne!(adapter, std::ptr::null_mut());
        Ok(Self {
            handle: {
                let mut device = std::ptr::null_mut();
                Error::new(ffi::ren_vk_CreateDevice(
                    proc,
                    instance,
                    adapter,
                    &mut device,
                ))?;
                HDevice::new(device)
            },
            swapchains: SlotMap::with_key(),
            scenes: SlotMap::with_key(),
        })
    }

    /// # Safety
    ///
    /// Requires valid VkSurfaceKHR
    pub unsafe fn create_swapchain(
        &mut self,
        surface: vk::SurfaceKHR,
    ) -> Result<SwapchainKey, Error> {
        Ok(self
            .swapchains
            .insert(Swapchain::new(self.handle.get_mut(), surface)?))
    }

    pub fn destroy_swapchain(&mut self, key: SwapchainKey) {
        self.swapchains.remove(key);
    }

    pub fn get_swapchain(&self, key: SwapchainKey) -> Option<&Swapchain> {
        self.swapchains.get(key)
    }

    pub fn get_swapchain_mut(&mut self, key: SwapchainKey) -> Option<&mut Swapchain> {
        self.swapchains.get_mut(key)
    }

    pub fn create_scene(&mut self) -> Result<SceneKey, Error> {
        Ok(self.scenes.insert(Scene::new(&mut self.handle)?))
    }

    pub fn destroy_scene(&mut self, key: SceneKey) {
        self.scenes.remove(key);
    }

    pub fn get_scene(&self, key: SceneKey) -> Option<&Scene> {
        self.scenes.get(key)
    }

    pub fn get_scene_mut(&mut self, key: SceneKey) -> Option<&mut Scene> {
        self.scenes.get_mut(key)
    }
}

pub struct DeviceFrame<'a> {
    device: &'a mut Device,
    active_swapchains: HashSet<SwapchainKey>,
    active_scenes: HashSet<SceneKey>,
}

impl<'a> Deref for DeviceFrame<'a> {
    type Target = Device;

    fn deref(&self) -> &Self::Target {
        self.device
    }
}

impl<'a> DerefMut for DeviceFrame<'a> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.device
    }
}

impl<'a> DeviceFrame<'a> {
    pub fn new(device: &'a mut Device) -> Result<Self, Error> {
        unsafe { Error::new(ffi::ren_DeviceBeginFrame(device.handle.get_mut()))? };
        Ok(Self {
            device,
            active_swapchains: HashSet::new(),
            active_scenes: HashSet::new(),
        })
    }

    pub fn end(mut self) -> Result<(), Error> {
        self.end_impl()
    }

    fn end_impl(&mut self) -> Result<(), Error> {
        unsafe { Error::new(ffi::ren_DeviceEndFrame(self.handle.get_mut())) }
    }
}

impl<'a> Drop for DeviceFrame<'a> {
    fn drop(&mut self) {
        if !std::thread::panicking() {
            self.end_impl().unwrap();
        }
    }
}

type HSwapchain = handle::Handle<RenSwapchain>;

impl handle::DestroyHandle for RenSwapchain {
    unsafe fn destroy(swapchain: *mut RenSwapchain) {
        ffi::ren_DestroySwapchain(swapchain)
    }
}

pub struct Swapchain {
    handle: HSwapchain,
}

new_key_type!(
    pub struct SwapchainKey;
);

impl Swapchain {
    unsafe fn new(device: *mut RenDevice, surface: vk::SurfaceKHR) -> Result<Self, Error> {
        Ok(Self {
            handle: {
                assert_ne!(device, std::ptr::null_mut());
                assert_ne!(surface, std::ptr::null_mut());
                let mut swapchain = std::ptr::null_mut();
                Error::new(ffi::ren_vk_CreateSwapchain(device, surface, &mut swapchain))?;
                HSwapchain::new(swapchain)
            },
        })
    }

    pub fn get_size(&self) -> (u32, u32) {
        let (mut width, mut height) = (0, 0);
        unsafe { ffi::ren_GetSwapchainSize(self.handle.get(), &mut width, &mut height) };
        (width, height)
    }

    pub fn set_size(&mut self, width: u32, height: u32) {
        unsafe { ffi::ren_SetSwapchainSize(self.handle.get_mut(), width, height) }
    }

    pub fn get_surface(&self) -> vk::SurfaceKHR {
        unsafe { ffi::ren_vk_GetSwapchainSurface(self.handle.get()) }
    }

    pub fn get_present_mode(&self) -> vk::PresentModeKHR {
        unsafe { ffi::ren_vk_GetSwapchainPresentMode(self.handle.get()) }
    }

    pub fn set_present_mode(&mut self, present_mode: vk::PresentModeKHR) -> Result<(), Error> {
        unsafe {
            Error::new(ffi::ren_vk_SetSwapchainPresentMode(
                self.handle.get_mut(),
                present_mode,
            ))
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

#[derive(Clone, Copy)]
struct MeshID(RenMesh);
type HMesh = handle::SceneHandle<MeshID>;

impl handle::DestroySceneHandle for MeshID {
    unsafe fn destroy(scene: *mut RenScene, mesh: MeshID) {
        ffi::ren_DestroyMesh(scene, mesh.0);
    }
}

pub struct Mesh {
    handle: HMesh,
}

new_key_type!(
    pub struct MeshKey;
);

pub struct MeshDesc<'a> {
    pub positions: &'a [[f32; 3]],
    pub colors: Option<&'a [[f32; 3]]>,
    pub indices: &'a [u32],
}

impl Mesh {
    fn new(scene: &mut HScene, desc: &MeshDesc) -> Result<Self, Error> {
        let scene = scene.get_mut();
        let num_vertices = desc.positions.len();
        let desc = RenMeshDesc {
            num_vertices: num_vertices as u32,
            num_indices: desc.indices.len() as u32,
            positions: desc.positions.as_ptr(),
            colors: desc.colors.map_or(std::ptr::null(), |colors| {
                assert!(colors.len() == num_vertices);
                colors.as_ptr()
            }),
            indices: desc.indices.as_ptr(),
        };
        Ok(Self {
            handle: unsafe {
                HMesh::new(scene, {
                    let mut mesh = REN_NULL_MESH;
                    Error::new(ffi::ren_CreateMesh(scene, &desc, &mut mesh))?;
                    MeshID(mesh)
                })
            },
        })
    }
}

#[derive(Clone, Copy)]
struct MaterialID(RenMaterial);
type HMaterial = handle::SceneHandle<MaterialID>;

impl handle::DestroySceneHandle for MaterialID {
    unsafe fn destroy(scene: *mut RenScene, material: MaterialID) {
        ffi::ren_DestroyMaterial(scene, material.0);
    }
}

pub struct Material {
    handle: HMaterial,
}

new_key_type!(
    pub struct MaterialKey;
);

pub enum MaterialAlbedo {
    Const([f32; 3]),
    Vertex,
}

pub struct MaterialDesc {
    pub albedo: MaterialAlbedo,
}

impl Material {
    fn new(scene: &mut HScene, desc: &MaterialDesc) -> Result<Self, Error> {
        let scene = scene.get_mut();
        let desc = RenMaterialDesc {
            albedo: match desc.albedo {
                MaterialAlbedo::Const(_) => REN_MATERIAL_ALBEDO_CONST,
                MaterialAlbedo::Vertex => REN_MATERIAL_ALBEDO_VERTEX,
            },
            __bindgen_anon_1: match desc.albedo {
                MaterialAlbedo::Const(const_albedo) => {
                    ffi::RenMaterialDesc__bindgen_ty_1 { const_albedo }
                }
                MaterialAlbedo::Vertex => unsafe { std::mem::zeroed() },
            },
        };
        Ok(Self {
            handle: unsafe {
                HMaterial::new(scene, {
                    let mut material = REN_NULL_MATERIAL;
                    Error::new(ffi::ren_CreateMaterial(scene, &desc, &mut material))?;
                    MaterialID(material)
                })
            },
        })
    }
}

#[derive(Clone, Copy)]
struct MeshInstanceID(RenMeshInstance);
type HMeshInstance = handle::SceneHandle<MeshInstanceID>;

impl handle::DestroySceneHandle for MeshInstanceID {
    unsafe fn destroy(scene: *mut RenScene, model: MeshInstanceID) {
        ffi::ren_DestroyMeshInstance(scene, model.0);
    }
}

pub struct MeshInstance {
    handle: HMeshInstance,
}

new_key_type!(
    pub struct MeshInstanceKey;
);

pub struct MeshInstanceDesc {
    pub mesh: MeshKey,
    pub material: MaterialKey,
}

impl MeshInstance {
    fn new(scene: &mut HScene, mesh: MeshID, material: MaterialID) -> Result<Self, Error> {
        let scene = scene.get_mut();
        let desc = RenMeshInstanceDesc {
            mesh: mesh.0,
            material: material.0,
        };
        Ok(Self {
            handle: unsafe {
                HMeshInstance::new(
                    scene,
                    MeshInstanceID({
                        let mut mesh_instance = REN_NULL_MESH_INSTANCE;
                        Error::new(ffi::ren_CreateMeshInstance(
                            scene,
                            &desc,
                            &mut mesh_instance,
                        ))?;
                        mesh_instance
                    }),
                )
            },
        })
    }

    pub fn set_matrix(&mut self, matrix: &[f32; 16]) {
        unsafe {
            ffi::ren_SetMeshInstanceMatrix(self.handle.get_scene_mut(), self.handle.get().0, matrix)
        }
    }
}

type HScene = handle::Handle<RenScene>;

impl handle::DestroyHandle for RenScene {
    unsafe fn destroy(ptr: *mut Self) {
        ffi::ren_DestroyScene(ptr);
    }
}

pub struct Scene {
    meshes: SlotMap<MeshKey, Mesh>,
    materials: SlotMap<MaterialKey, Material>,
    mesh_instances: SlotMap<MeshInstanceKey, MeshInstance>,
    handle: HScene,
}

new_key_type!(
    pub struct SceneKey;
);

impl Scene {
    fn new(device: &mut HDevice) -> Result<Self, Error> {
        let device = device.get_mut();
        Ok(Self {
            handle: unsafe {
                let mut scene = std::ptr::null_mut();
                Error::new(ffi::ren_CreateScene(device, &mut scene))?;
                HScene::new(scene)
            },
            meshes: SlotMap::with_key(),
            materials: SlotMap::with_key(),
            mesh_instances: SlotMap::with_key(),
        })
    }

    pub fn get_mesh(&self, key: MeshKey) -> Option<&Mesh> {
        self.meshes.get(key)
    }

    pub fn get_mesh_mut(&mut self, key: MeshKey) -> Option<&mut Mesh> {
        self.meshes.get_mut(key)
    }

    pub fn get_material(&self, key: MaterialKey) -> Option<&Material> {
        self.materials.get(key)
    }

    pub fn get_material_mut(&mut self, key: MaterialKey) -> Option<&mut Material> {
        self.materials.get_mut(key)
    }

    pub fn get_mesh_instance(&self, key: MeshInstanceKey) -> Option<&MeshInstance> {
        self.mesh_instances.get(key)
    }

    pub fn get_mesh_instance_mut(&mut self, key: MeshInstanceKey) -> Option<&mut MeshInstance> {
        self.mesh_instances.get_mut(key)
    }
}

pub struct SceneFrame<'a> {
    scene: &'a mut Scene,
}

impl<'a> SceneFrame<'a> {
    pub fn new(
        device_frame: &'a mut DeviceFrame,
        scene: SceneKey,
        swapchain: SwapchainKey,
        width: u32,
        height: u32,
    ) -> Result<Self, Error> {
        if !device_frame.active_swapchains.insert(swapchain) {
            return Err(Error::Busy);
        }
        if !device_frame.active_scenes.insert(scene) {
            return Err(Error::Busy);
        }
        let swapchain = device_frame
            .get_swapchain_mut(swapchain)
            .ok_or(Error::InvalidKey)?
            .handle
            .get_mut();
        let scene = device_frame.get_scene_mut(scene).ok_or(Error::InvalidKey)?;
        unsafe { Error::new(ffi::ren_SceneBeginFrame(scene.handle.get_mut(), swapchain))? };
        unsafe {
            assert!(width > 0);
            assert!(height > 0);
            Error::new(ffi::ren_SetSceneOutputSize(
                scene.handle.get_mut(),
                width,
                height,
            ))?;
        };
        Ok(Self { scene })
    }

    pub fn end(mut self) -> Result<(), Error> {
        self.end_impl()
    }

    fn end_impl(&mut self) -> Result<(), Error> {
        unsafe { Error::new(ffi::ren_SceneEndFrame(self.scene.handle.get_mut())) }
    }
}

impl<'a> Drop for SceneFrame<'a> {
    fn drop(&mut self) {
        if !std::thread::panicking() {
            self.end_impl().unwrap()
        }
    }
}

impl<'a> Deref for SceneFrame<'a> {
    type Target = Scene;

    fn deref(&self) -> &Self::Target {
        self.scene
    }
}

impl<'a> DerefMut for SceneFrame<'a> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.scene
    }
}

impl<'a> SceneFrame<'a> {
    pub fn set_camera(&mut self, camera: &CameraDesc) {
        let camera = RenCameraDesc {
            projection: match camera.projection {
                CameraProjection::Perspective { .. } => REN_PROJECTION_PERSPECTIVE,
                CameraProjection::Orthographic { .. } => REN_PROJECTION_ORTHOGRAPHIC,
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
        unsafe { ffi::ren_SetSceneCamera(self.handle.get_mut(), &camera) }
    }

    pub fn create_mesh(&mut self, desc: &MeshDesc) -> Result<MeshKey, Error> {
        let mesh = Mesh::new(&mut self.handle, desc)?;
        Ok(self.meshes.insert(mesh))
    }

    pub fn create_material(&mut self, desc: &MaterialDesc) -> Result<MaterialKey, Error> {
        let material = Material::new(&mut self.handle, desc)?;
        Ok(self.materials.insert(material))
    }

    pub fn create_mesh_instance(
        &mut self,
        desc: &MeshInstanceDesc,
    ) -> Result<MeshInstanceKey, Error> {
        let mesh = self
            .get_mesh(desc.mesh)
            .ok_or(Error::InvalidKey)?
            .handle
            .get();
        let material = self
            .get_material(desc.material)
            .ok_or(Error::InvalidKey)?
            .handle
            .get();
        let mesh_instance = MeshInstance::new(&mut self.handle, mesh, material)?;
        Ok(self.mesh_instances.insert(mesh_instance))
    }

    pub fn destroy_mesh_instance(&mut self, key: MeshInstanceKey) {
        self.mesh_instances.remove(key);
    }
}
