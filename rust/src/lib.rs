use ash::vk::Handle;
use raw_window_handle::{
    HasRawDisplayHandle, HasRawWindowHandle, RawDisplayHandle, RawWindowHandle,
};
use slotmap::{new_key_type, SlotMap};
use std::{
    cmp,
    ffi::{c_char, c_void, CStr},
    mem, ptr, str,
};
use thiserror::Error;

mod ffi;
pub use ffi::VkPresentModeKHR;
use ffi::{
    RenAlphaMode, RenCameraDesc, RenCameraDesc__bindgen_ty_1, RenDevice, RenDeviceDesc,
    RenDirLight, RenDirLightDesc, RenExposureMode, RenFilter, RenFormat, RenImage, RenImageDesc,
    RenMaterial, RenMaterialDesc, RenMesh, RenMeshDesc, RenMeshInst, RenMeshInstDesc,
    RenOrthographicProjection, RenPFNCreateSurface, RenPerspectiveProjection, RenProjection,
    RenResult, RenSampler, RenScene, RenSwapchain, RenTexture, RenTextureChannel,
    RenTextureChannelSwizzle, RenToneMappingOperator, RenWrappingMode, REN_ALPHA_MODE_BLEND,
    REN_ALPHA_MODE_MASK, REN_ALPHA_MODE_OPAQUE, REN_EXPOSURE_MODE_AUTOMATIC,
    REN_EXPOSURE_MODE_CAMERA, REN_FILTER_LINEAR, REN_FILTER_NEAREST, REN_FORMAT_R16_UNORM,
    REN_FORMAT_R8_SRGB, REN_FORMAT_R8_UNORM, REN_FORMAT_RG16_UNORM, REN_FORMAT_RG8_SRGB,
    REN_FORMAT_RG8_UNORM, REN_FORMAT_RGB16_UNORM, REN_FORMAT_RGB32_SFLOAT, REN_FORMAT_RGB8_SRGB,
    REN_FORMAT_RGB8_UNORM, REN_FORMAT_RGBA16_UNORM, REN_FORMAT_RGBA32_SFLOAT,
    REN_FORMAT_RGBA8_SRGB, REN_FORMAT_RGBA8_UNORM, REN_NULL_DIR_LIGHT, REN_NULL_IMAGE,
    REN_NULL_MATERIAL, REN_NULL_MESH, REN_NULL_MESH_INST, REN_PROJECTION_ORTHOGRAPHIC,
    REN_PROJECTION_PERSPECTIVE, REN_RUNTIME_ERROR, REN_SUCCESS, REN_SYSTEM_ERROR,
    REN_TEXTURE_CHANNEL_A, REN_TEXTURE_CHANNEL_B, REN_TEXTURE_CHANNEL_G,
    REN_TEXTURE_CHANNEL_IDENTITY, REN_TEXTURE_CHANNEL_ONE, REN_TEXTURE_CHANNEL_R,
    REN_TEXTURE_CHANNEL_ZERO, REN_TONE_MAPPING_OPERATOR_ACES, REN_TONE_MAPPING_OPERATOR_REINHARD,
    REN_VULKAN_ERROR, REN_WRAPPING_MODE_CLAMP_TO_EDGE, REN_WRAPPING_MODE_MIRRORED_REPEAT,
    REN_WRAPPING_MODE_REPEAT,
};

const UUID_SIZE: usize = 16;

mod handle;

#[derive(Error, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Error {
    #[error("Vulkan runtime error")]
    Vulkan,
    #[error("System error")]
    System,
    #[error("C++ runtime error")]
    Runtime,
    #[error("Specified object was not found")]
    InvalidKey,
    #[error("Unknown error")]
    Unknown,
}

impl From<ash::vk::Result> for Error {
    fn from(_: ash::vk::Result) -> Self {
        Self::Vulkan
    }
}

impl From<ash::LoadingError> for Error {
    fn from(_: ash::LoadingError) -> Self {
        Self::System
    }
}

impl From<str::Utf8Error> for Error {
    fn from(_: str::Utf8Error) -> Self {
        Self::System
    }
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

new_key_type!(
    pub struct AdapterKey;
);

pub struct Adapter {
    name: String,
    pipeline_cache_uuid: [u8; UUID_SIZE],
}

impl Adapter {
    pub fn get_name(&self) -> &str {
        &self.name
    }
}

pub struct DeviceBuilder {
    vk: ash::Entry,
    display: RawDisplayHandle,
    extensions: &'static [*const c_char],
    adapters: SlotMap<AdapterKey, Adapter>,
    adapter: Option<AdapterKey>,
}

struct Instance(ash::Instance);

impl Instance {
    fn new(entry: &ash::Entry) -> Result<Self, Error> {
        let create_info = ash::vk::InstanceCreateInfo {
            ..Default::default()
        };
        Ok(Self(unsafe { entry.create_instance(&create_info, None) }?))
    }
}

impl Drop for Instance {
    fn drop(&mut self) {
        unsafe { self.0.destroy_instance(None) }
    }
}

impl DeviceBuilder {
    pub fn adapters(&self) -> impl Iterator<Item = AdapterKey> + '_ {
        self.adapters.keys()
    }

    pub fn get_adapter(&self, key: AdapterKey) -> Option<&Adapter> {
        self.adapters.get(key)
    }

    pub fn select_adapter(&mut self, adapter: Option<AdapterKey>) -> &mut Self {
        self.adapter = adapter;
        self
    }

    pub fn build(self) -> Result<Device, Error> {
        let adapter = self
            .adapter
            .map_or_else(
                || self.adapters.values().next(),
                |adapter| self.adapters.get(adapter),
            )
            .unwrap();
        Ok(Device {
            handle: unsafe {
                let mut device = ptr::null_mut();
                Error::new(ffi::ren_vk_CreateDevice(
                    &RenDeviceDesc {
                        proc_: mem::transmute(self.vk.static_fn().get_instance_proc_addr),
                        num_instance_extensions: self.extensions.len() as u32,
                        instance_extensions: self.extensions.as_ptr(),
                        pipeline_cache_uuid: adapter.pipeline_cache_uuid,
                    },
                    &mut device,
                ))?;
                HDevice::new(device)
            },
            swapchains: SlotMap::with_key(),
            scenes: SlotMap::with_key(),
            vk: self.vk,
            display: self.display,
        })
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
    // Unload Vulkan after dropping device
    vk: ash::Entry,
    display: RawDisplayHandle,
}

impl Device {
    pub fn builder<Display>(display: &Display) -> Result<DeviceBuilder, Error>
    where
        Display: HasRawDisplayHandle,
    {
        let vk = unsafe { ash::Entry::load()? };
        let display = display.raw_display_handle();
        let extensions = ash_window::enumerate_required_extensions(display)?;

        let instance = Instance::new(&vk)?;
        let mut adapters = SlotMap::with_key();
        for device in unsafe { instance.0.enumerate_physical_devices()? } {
            let props = unsafe { instance.0.get_physical_device_properties(device) };
            let name = unsafe { CStr::from_ptr(props.device_name.as_ptr()) }
                .to_str()?
                .to_string();
            adapters.insert(Adapter {
                name,
                pipeline_cache_uuid: props.pipeline_cache_uuid,
            });
        }
        if adapters.is_empty() {
            return Err(Error::System);
        }

        Ok(DeviceBuilder {
            vk,
            display,
            extensions,
            adapters,
            adapter: None,
        })
    }

    pub fn create_swapchain<Window>(&mut self, window: &Window) -> Result<SwapchainKey, Error>
    where
        Window: HasRawDisplayHandle + HasRawWindowHandle,
    {
        struct Data<'a> {
            vk: &'a ash::Entry,
            display: RawDisplayHandle,
            window: RawWindowHandle,
        }

        unsafe extern "C" fn create_surface(
            instance: ffi::VkInstance,
            usrptr: *mut c_void,
            p_surface: *mut ffi::VkSurfaceKHR,
        ) -> ffi::VkResult {
            let Data {
                vk,
                display,
                window,
            } = &*(usrptr as *const Data);
            let instance =
                ash::Instance::load(vk.static_fn(), ash::vk::Instance::from_raw(instance as u64));
            let surface = ash_window::create_surface(vk, &instance, *display, *window, None);
            match surface {
                Ok(surface) => {
                    *p_surface = surface.as_raw() as ffi::VkSurfaceKHR;
                    ffi::VK_SUCCESS
                }
                Err(result) => ffi::VkResult(result.as_raw()),
            }
        }

        assert_eq!(self.display, window.raw_display_handle());
        let mut data = Data {
            vk: &self.vk,
            display: self.display,
            window: window.raw_window_handle(),
        };

        Ok(self.swapchains.insert(unsafe {
            Swapchain::new(
                self.handle.get_mut(),
                Some(create_surface),
                &mut data as *mut Data as *mut c_void,
            )?
        }))
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

    pub fn draw(&mut self, scene: SceneKey, swapchain: SwapchainKey) -> Result<(), Error> {
        let scene = self.get_scene_mut(scene).ok_or(Error::InvalidKey)?;
        unsafe {
            Error::new(ffi::ren_DrawScene(
                scene.handle.get_mut(),
                self.get_swapchain_mut(swapchain)
                    .ok_or(Error::InvalidKey)?
                    .handle
                    .get_mut(),
            ))
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
    unsafe fn new(
        device: *mut RenDevice,
        create_surface: RenPFNCreateSurface,
        usrptr: *mut c_void,
    ) -> Result<Self, Error> {
        Ok(Self {
            handle: {
                assert_ne!(device, ptr::null_mut());
                let mut swapchain = ptr::null_mut();
                Error::new(ffi::ren_vk_CreateSwapchain(
                    device,
                    create_surface,
                    usrptr,
                    &mut swapchain,
                ))?;
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

    pub fn get_present_mode(&self) -> VkPresentModeKHR {
        unsafe { ffi::ren_vk_GetSwapchainPresentMode(self.handle.get()) }
    }

    pub fn set_present_mode(&mut self, present_mode: VkPresentModeKHR) -> Result<(), Error> {
        unsafe {
            Error::new(ffi::ren_vk_SetSwapchainPresentMode(
                self.handle.get_mut(),
                present_mode,
            ))
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum CameraProjection {
    Perspective { hfov: f32 },
    Orthographic { width: f32 },
}

impl Default for CameraProjection {
    fn default() -> Self {
        Self::Perspective {
            hfov: 90.0f32.to_radians(),
        }
    }
}

impl From<CameraProjection> for RenProjection {
    fn from(proj: CameraProjection) -> Self {
        match proj {
            CameraProjection::Perspective { .. } => REN_PROJECTION_PERSPECTIVE,
            CameraProjection::Orthographic { .. } => REN_PROJECTION_ORTHOGRAPHIC,
        }
    }
}

impl From<CameraProjection> for RenCameraDesc__bindgen_ty_1 {
    fn from(proj: CameraProjection) -> Self {
        match proj {
            CameraProjection::Perspective { hfov } => RenCameraDesc__bindgen_ty_1 {
                perspective: RenPerspectiveProjection { hfov },
            },
            CameraProjection::Orthographic { width } => RenCameraDesc__bindgen_ty_1 {
                orthographic: RenOrthographicProjection { width },
            },
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ExposureMode {
    Camera { iso: f32 },
    Automatic,
}

impl From<ExposureMode> for RenExposureMode {
    fn from(exposure: ExposureMode) -> Self {
        match exposure {
            ExposureMode::Camera { .. } => REN_EXPOSURE_MODE_CAMERA,
            ExposureMode::Automatic => REN_EXPOSURE_MODE_AUTOMATIC,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct CameraDesc {
    pub projection: CameraProjection,
    pub width: u32,
    pub height: u32,
    pub aperture: f32,
    pub shutter_time: f32,
    pub exposure_compensation: f32,
    pub exposure_mode: ExposureMode,
    pub position: [f32; 3],
    pub forward: [f32; 3],
    pub up: [f32; 3],
}

impl Default for CameraDesc {
    fn default() -> Self {
        let iso = 400.0;
        Self {
            projection: Default::default(),
            width: 1280,
            height: 720,
            aperture: 16.0,
            shutter_time: 1.0 / iso,
            exposure_compensation: 0.0,
            exposure_mode: ExposureMode::Automatic,
            position: [0.0; 3],
            forward: [1.0, 0.0, 0.0],
            up: [0.0, 0.0, 1.0],
        }
    }
}

impl From<CameraDesc> for RenCameraDesc {
    fn from(desc: CameraDesc) -> Self {
        RenCameraDesc {
            projection: desc.projection.into(),
            __bindgen_anon_1: desc.projection.into(),
            width: desc.width,
            height: desc.height,
            aperture: desc.aperture,
            shutter_time: desc.shutter_time,
            exposure_compensation: desc.exposure_compensation,
            iso: match desc.exposure_mode {
                ExposureMode::Camera { iso } => iso,
                _ => 0.0,
            },
            exposure_mode: desc.exposure_mode.into(),
            position: desc.position,
            forward: desc.forward,
            up: desc.up,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ToneMappingOperator {
    Reinhard,
    ACES,
}

impl From<ToneMappingOperator> for RenToneMappingOperator {
    fn from(operator: ToneMappingOperator) -> Self {
        match operator {
            ToneMappingOperator::Reinhard => REN_TONE_MAPPING_OPERATOR_REINHARD,
            ToneMappingOperator::ACES => REN_TONE_MAPPING_OPERATOR_ACES,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct MeshID(RenMesh);

#[derive(Debug, Default, Clone, Copy, PartialEq)]
pub struct MeshDesc<'a> {
    pub positions: &'a [[f32; 3]],
    pub colors: Option<&'a [[f32; 4]]>,
    pub normals: Option<&'a [[f32; 3]]>,
    pub tangents: Option<&'a [[f32; 4]]>,
    pub uvs: Option<&'a [[f32; 2]]>,
    pub indices: Option<&'a [u32]>,
}

impl<'a> From<MeshDesc<'a>> for RenMeshDesc {
    fn from(desc: MeshDesc<'a>) -> Self {
        let num_vertices = desc
            .positions
            .len()
            .min(desc.colors.map_or(usize::MAX, |colors| colors.len()))
            .min(desc.normals.map_or(usize::MAX, |normals| normals.len()))
            .min(desc.tangents.map_or(usize::MAX, |tangents| tangents.len()))
            .min(desc.uvs.map_or(usize::MAX, |uvs| uvs.len()));
        RenMeshDesc {
            num_vertices: num_vertices as u32,
            positions: desc.positions.as_ptr(),
            colors: desc.colors.map_or(ptr::null(), |colors| colors.as_ptr()),
            normals: desc.normals.map_or(ptr::null(), |normals| normals.as_ptr()),
            tangents: desc
                .tangents
                .map_or(ptr::null(), |tangents| tangents.as_ptr()),
            uvs: desc.uvs.map_or(ptr::null(), |uvs| uvs.as_ptr()),
            num_indices: desc.indices.map_or(0, |indices| indices.len() as u32),
            indices: desc.indices.map_or(ptr::null(), |indices| indices.as_ptr()),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ComponentFormat {
    R8,
    RG8,
    RGB8,
    RGBA8,
    R16,
    RG16,
    RGB16,
    RGBA16,
    RGB32,
    RGBA32,
}

impl ComponentFormat {
    fn size(&self) -> usize {
        match self {
            ComponentFormat::R8 => 1 * 1,
            ComponentFormat::RG8 => 1 * 2,
            ComponentFormat::RGB8 => 1 * 3,
            ComponentFormat::RGBA8 => 1 * 4,
            ComponentFormat::R16 => 2,
            ComponentFormat::RG16 => 2 * 2,
            ComponentFormat::RGB16 => 2 * 3,
            ComponentFormat::RGBA16 => 2 * 4,
            ComponentFormat::RGB32 => 4 * 3,
            ComponentFormat::RGBA32 => 4 * 4,
        }
    }
}

impl From<Format> for ComponentFormat {
    fn from(format: Format) -> Self {
        match format {
            Format::R8_UNORM | Format::R8_SRGB => ComponentFormat::R8,
            Format::RG8_UNORM | Format::RG8_SRGB => ComponentFormat::RG8,
            Format::RGB8_UNORM | Format::RGB8_SRGB => ComponentFormat::RGB8,
            Format::RGBA8_UNORM | Format::RGBA8_SRGB => ComponentFormat::RGBA8,
            Format::R16_UNORM => ComponentFormat::R16,
            Format::RG16_UNORM => ComponentFormat::RG16,
            Format::RGB16_UNORM => ComponentFormat::RGB16,
            Format::RGBA16_UNORM => ComponentFormat::RGBA16,
            Format::RGB32_SFLOAT => ComponentFormat::RGB32,
            Format::RGBA32_SFLOAT => ComponentFormat::RGBA32,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum NumericFormat {
    UNORM,
    SRGB,
    SFLOAT,
}

impl From<Format> for NumericFormat {
    fn from(format: Format) -> Self {
        match format {
            Format::R8_UNORM
            | Format::RG8_UNORM
            | Format::RGB8_UNORM
            | Format::RGBA8_UNORM
            | Format::R16_UNORM
            | Format::RG16_UNORM
            | Format::RGB16_UNORM
            | Format::RGBA16_UNORM => NumericFormat::UNORM,
            Format::R8_SRGB | Format::RG8_SRGB | Format::RGB8_SRGB | Format::RGBA8_SRGB => {
                NumericFormat::SRGB
            }
            Format::RGB32_SFLOAT | Format::RGBA32_SFLOAT => NumericFormat::SFLOAT,
        }
    }
}

#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Format {
    R8_UNORM,
    R8_SRGB,
    RG8_UNORM,
    RG8_SRGB,
    RGB8_UNORM,
    RGB8_SRGB,
    RGBA8_UNORM,
    RGBA8_SRGB,
    R16_UNORM,
    RG16_UNORM,
    RGB16_UNORM,
    RGBA16_UNORM,
    RGB32_SFLOAT,
    RGBA32_SFLOAT,
}

impl Format {
    pub fn new(component_format: ComponentFormat, numeric_format: NumericFormat) -> Option<Self> {
        match (component_format, numeric_format) {
            (ComponentFormat::R8, NumericFormat::UNORM) => Some(Format::R8_UNORM),
            (ComponentFormat::R8, NumericFormat::SRGB) => Some(Format::R8_SRGB),
            (ComponentFormat::RG8, NumericFormat::UNORM) => Some(Format::RG8_UNORM),
            (ComponentFormat::RG8, NumericFormat::SRGB) => Some(Format::RG8_SRGB),
            (ComponentFormat::RGB8, NumericFormat::UNORM) => Some(Format::RGB8_UNORM),
            (ComponentFormat::RGB8, NumericFormat::SRGB) => Some(Format::RGB8_SRGB),
            (ComponentFormat::RGBA8, NumericFormat::UNORM) => Some(Format::RGBA8_UNORM),
            (ComponentFormat::RGBA8, NumericFormat::SRGB) => Some(Format::RGBA8_SRGB),
            (ComponentFormat::R16, NumericFormat::UNORM) => Some(Format::R16_UNORM),
            (ComponentFormat::RG16, NumericFormat::UNORM) => Some(Format::RG16_UNORM),
            (ComponentFormat::RGB16, NumericFormat::UNORM) => Some(Format::RGB16_UNORM),
            (ComponentFormat::RGBA16, NumericFormat::UNORM) => Some(Format::RGBA16_UNORM),
            (ComponentFormat::RGB32, NumericFormat::SFLOAT) => Some(Format::RGB32_SFLOAT),
            (ComponentFormat::RGBA32, NumericFormat::SFLOAT) => Some(Format::RGBA32_SFLOAT),
            _ => None,
        }
    }

    pub fn size(&self) -> usize {
        ComponentFormat::from(*self).size()
    }
}

impl From<Format> for RenFormat {
    fn from(format: Format) -> Self {
        match format {
            Format::R8_UNORM => REN_FORMAT_R8_UNORM,
            Format::R8_SRGB => REN_FORMAT_R8_SRGB,
            Format::RG8_UNORM => REN_FORMAT_RG8_UNORM,
            Format::RG8_SRGB => REN_FORMAT_RG8_SRGB,
            Format::RGB8_UNORM => REN_FORMAT_RGB8_UNORM,
            Format::RGB8_SRGB => REN_FORMAT_RGB8_SRGB,
            Format::RGBA8_UNORM => REN_FORMAT_RGBA8_UNORM,
            Format::RGBA8_SRGB => REN_FORMAT_RGBA8_SRGB,
            Format::R16_UNORM => REN_FORMAT_R16_UNORM,
            Format::RG16_UNORM => REN_FORMAT_RG16_UNORM,
            Format::RGB16_UNORM => REN_FORMAT_RGB16_UNORM,
            Format::RGBA16_UNORM => REN_FORMAT_RGBA16_UNORM,
            Format::RGB32_SFLOAT => REN_FORMAT_RGB32_SFLOAT,
            Format::RGBA32_SFLOAT => REN_FORMAT_RGBA32_SFLOAT,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ImageID(RenImage);

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ImageDesc<'a> {
    pub format: Format,
    pub width: u32,
    pub height: u32,
    pub data: &'a [u8],
}

impl<'a> From<ImageDesc<'a>> for RenImageDesc {
    fn from(desc: ImageDesc<'a>) -> Self {
        RenImageDesc {
            format: desc.format.into(),
            width: desc.width,
            height: desc.height,
            data: desc.data.as_ptr() as *const c_void,
        }
    }
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Filter {
    Nearest,
    #[default]
    Linear,
}

impl From<Filter> for RenFilter {
    fn from(filter: Filter) -> Self {
        match filter {
            Filter::Nearest => REN_FILTER_NEAREST,
            Filter::Linear => REN_FILTER_LINEAR,
        }
    }
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, Hash)]
pub enum WrappingMode {
    #[default]
    Repeat,
    MirroredRepeat,
    ClampToEdge,
}

impl From<WrappingMode> for RenWrappingMode {
    fn from(mode: WrappingMode) -> Self {
        match mode {
            WrappingMode::Repeat => REN_WRAPPING_MODE_REPEAT,
            WrappingMode::MirroredRepeat => REN_WRAPPING_MODE_MIRRORED_REPEAT,
            WrappingMode::ClampToEdge => REN_WRAPPING_MODE_CLAMP_TO_EDGE,
        }
    }
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Sampler {
    pub mag_filter: Filter,
    pub min_filter: Filter,
    pub mipmap_filter: Filter,
    pub wrap_u: WrappingMode,
    pub wrap_v: WrappingMode,
}

impl From<Sampler> for RenSampler {
    fn from(sampler: Sampler) -> Self {
        RenSampler {
            mag_filter: sampler.mag_filter.into(),
            min_filter: sampler.min_filter.into(),
            mipmap_filter: sampler.mipmap_filter.into(),
            wrap_u: sampler.wrap_u.into(),
            wrap_v: sampler.wrap_v.into(),
        }
    }
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, Hash)]
pub enum TextureChannel {
    #[default]
    Identity,
    Zero,
    One,
    R,
    G,
    B,
    A,
}

impl From<TextureChannel> for RenTextureChannel {
    fn from(channel: TextureChannel) -> Self {
        match channel {
            TextureChannel::Identity => REN_TEXTURE_CHANNEL_IDENTITY,
            TextureChannel::Zero => REN_TEXTURE_CHANNEL_ZERO,
            TextureChannel::One => REN_TEXTURE_CHANNEL_ONE,
            TextureChannel::R => REN_TEXTURE_CHANNEL_R,
            TextureChannel::G => REN_TEXTURE_CHANNEL_G,
            TextureChannel::B => REN_TEXTURE_CHANNEL_B,
            TextureChannel::A => REN_TEXTURE_CHANNEL_A,
        }
    }
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, Hash)]
pub struct TextureChannelSwizzle {
    pub r: TextureChannel,
    pub g: TextureChannel,
    pub b: TextureChannel,
    pub a: TextureChannel,
}

impl From<TextureChannelSwizzle> for RenTextureChannelSwizzle {
    fn from(swizzle: TextureChannelSwizzle) -> Self {
        RenTextureChannelSwizzle {
            r: swizzle.r.into(),
            g: swizzle.g.into(),
            b: swizzle.b.into(),
            a: swizzle.a.into(),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Texture {
    pub image: ImageID,
    pub sampler: Sampler,
    pub swizzle: TextureChannelSwizzle,
}

impl From<Texture> for RenTexture {
    fn from(tex: Texture) -> Self {
        RenTexture {
            image: tex.image.0,
            sampler: tex.sampler.into(),
            swizzle: tex.swizzle.into(),
        }
    }
}

impl From<Option<Texture>> for RenTexture {
    fn from(tex: Option<Texture>) -> Self {
        if let Some(tex) = tex {
            tex.into()
        } else {
            unsafe { mem::zeroed::<RenTexture>() }
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct OcculusionTexture {
    pub strength: f32,
    pub texture: Texture,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct NormalTexture {
    pub scale: f32,
    pub tex: Texture,
}

#[derive(Debug, Default, Clone, Copy, PartialEq)]
pub enum AlphaMode {
    #[default]
    Opaque,
    Mask {
        cutoff: f32,
    },
    Blend,
}

impl From<AlphaMode> for RenAlphaMode {
    fn from(mode: AlphaMode) -> Self {
        match mode {
            AlphaMode::Opaque => REN_ALPHA_MODE_OPAQUE,
            AlphaMode::Mask { .. } => REN_ALPHA_MODE_MASK,
            AlphaMode::Blend => REN_ALPHA_MODE_BLEND,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct MaterialID(RenMaterial);

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct MaterialDesc {
    pub base_color_factor: [f32; 4],
    pub base_color_texture: Option<Texture>,
    pub metallic_factor: f32,
    pub roughness_factor: f32,
    pub metallic_roughness_texture: Option<Texture>,
    pub occlusion_texture: Option<OcculusionTexture>,
    pub normal_texture: Option<NormalTexture>,
    pub emissive_factor: [f32; 3],
    pub emissive_texture: Option<Texture>,
    pub alpha_mode: AlphaMode,
    pub double_sided: bool,
}

impl Default for MaterialDesc {
    fn default() -> Self {
        Self {
            base_color_factor: [1.0, 1.0, 1.0, 1.0],
            base_color_texture: None,
            metallic_factor: 1.0,
            roughness_factor: 1.0,
            metallic_roughness_texture: None,
            occlusion_texture: None,
            normal_texture: None,
            emissive_factor: [0.0; 3],
            emissive_texture: None,
            alpha_mode: Default::default(),
            double_sided: false,
        }
    }
}

impl From<MaterialDesc> for RenMaterialDesc {
    fn from(desc: MaterialDesc) -> Self {
        RenMaterialDesc {
            base_color_factor: desc.base_color_factor,
            color_tex: desc.base_color_texture.into(),
            metallic_factor: desc.metallic_factor,
            roughness_factor: desc.roughness_factor,
            metallic_roughness_tex: desc.metallic_roughness_texture.into(),
            occlusion_strength: desc
                .occlusion_texture
                .map_or(0.0, |occlusion_tex| occlusion_tex.strength),
            occlusion_tex: desc
                .occlusion_texture
                .map(|occlusion_tex| occlusion_tex.texture)
                .into(),
            normal_scale: desc
                .normal_texture
                .map_or(1.0, |normal_tex| normal_tex.scale),
            normal_tex: desc.normal_texture.map(|normal_tex| normal_tex.tex).into(),
            emissive_factor: desc.emissive_factor,
            emissive_tex: desc.emissive_texture.into(),
            alpha_mode: desc.alpha_mode.into(),
            alpha_cutoff: if let AlphaMode::Mask { cutoff } = desc.alpha_mode {
                cutoff
            } else {
                0.0
            },
            double_sided: desc.double_sided.into(),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct MeshInstID(RenMeshInst);

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct MeshInstDesc {
    pub mesh: MeshID,
    pub material: MaterialID,
    pub casts_shadows: bool,
}

impl From<MeshInstDesc> for RenMeshInstDesc {
    fn from(desc: MeshInstDesc) -> Self {
        RenMeshInstDesc {
            mesh: desc.mesh.0,
            material: desc.material.0,
            casts_shadows: desc.casts_shadows.into(),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct DirLightID(RenDirLight);

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct DirLightDesc {
    pub color: [f32; 3],
    pub illuminance: f32,
    pub origin: [f32; 3],
}

impl Default for DirLightDesc {
    fn default() -> Self {
        Self {
            color: [1.0, 1.0, 1.0],
            illuminance: 1.0,
            origin: [0.0, 0.0, 1.0],
        }
    }
}

impl From<DirLightDesc> for RenDirLightDesc {
    fn from(desc: DirLightDesc) -> Self {
        RenDirLightDesc {
            color: desc.color,
            illuminance: desc.illuminance,
            origin: desc.origin,
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
    handle: HScene,
    material_desc_buffer: Vec<RenMaterialDesc>,
    mesh_inst_desc_buffer: Vec<RenMeshInstDesc>,
    dir_light_desc_buffer: Vec<RenDirLightDesc>,
}

new_key_type!(
    pub struct SceneKey;
);

impl Scene {
    fn new(device: &mut HDevice) -> Result<Self, Error> {
        let device = device.get_mut();
        Ok(Self {
            handle: unsafe {
                let mut scene = ptr::null_mut();
                Error::new(ffi::ren_CreateScene(device, &mut scene))?;
                HScene::new(scene)
            },
            material_desc_buffer: vec![],
            mesh_inst_desc_buffer: vec![],
            dir_light_desc_buffer: vec![],
        })
    }

    pub fn set_camera(&mut self, camera: &CameraDesc) -> Result<(), Error> {
        unsafe {
            Error::new(ffi::ren_SetSceneCamera(
                self.handle.get_mut(),
                &(*camera).into(),
            ))
        }
    }

    pub fn set_tone_mapping(&mut self, operator: ToneMappingOperator) -> Result<(), Error> {
        unsafe {
            Error::new(ffi::ren_SetSceneToneMapping(
                self.handle.get_mut(),
                operator.into(),
            ))
        }
    }

    pub fn create_mesh(&mut self, desc: &MeshDesc) -> Result<MeshID, Error> {
        let mut mesh = REN_NULL_MESH;
        unsafe {
            Error::new(ffi::ren_CreateMesh(
                self.handle.get_mut(),
                &(*desc).into(),
                &mut mesh,
            ))?;
        }
        Ok(MeshID(mesh))
    }

    pub fn create_image(&mut self, desc: &ImageDesc) -> Result<ImageID, Error> {
        let mut image = REN_NULL_IMAGE;
        unsafe {
            assert!(desc.data.len() >= (desc.width * desc.height) as usize * desc.format.size());
            Error::new(ffi::ren_CreateImage(
                self.handle.get_mut(),
                &(*desc).into(),
                &mut image,
            ))?;
        }
        Ok(ImageID(image))
    }

    pub fn create_material(&mut self, desc: &MaterialDesc) -> Result<MaterialID, Error> {
        let mut material = REN_NULL_MATERIAL;
        unsafe {
            Error::new(ffi::ren_CreateMaterials(
                self.handle.get_mut(),
                &(*desc).into(),
                1,
                &mut material,
            ))?
        }
        Ok(MaterialID(material))
    }

    pub fn create_materials<'a>(
        &mut self,
        descs: &[MaterialDesc],
        out: &'a mut [MaterialID],
    ) -> Result<&'a mut [MaterialID], Error> {
        let count = cmp::min(descs.len(), out.len());
        self.material_desc_buffer.clear();
        self.material_desc_buffer.extend(
            descs
                .iter()
                .take(count)
                .map(|desc| Into::<RenMaterialDesc>::into(*desc)),
        );
        unsafe {
            Error::new(ffi::ren_CreateMaterials(
                self.handle.get_mut(),
                self.material_desc_buffer.as_ptr(),
                count,
                out.as_mut_ptr() as *mut RenMaterial,
            ))?;
        }
        Ok(&mut out[..count])
    }

    pub fn create_mesh_inst(&mut self, desc: &MeshInstDesc) -> Result<MeshInstID, Error> {
        let mut mesh_inst = REN_NULL_MESH_INST;
        unsafe {
            Error::new(ffi::ren_CreateMeshInsts(
                self.handle.get_mut(),
                &(*desc).into(),
                1,
                &mut mesh_inst,
            ))?;
        }
        Ok(MeshInstID(mesh_inst))
    }

    pub fn create_mesh_insts<'a>(
        &mut self,
        descs: &[MeshInstDesc],
        out: &'a mut [MeshInstID],
    ) -> Result<&'a mut [MeshInstID], Error> {
        let count = cmp::min(descs.len(), out.len());
        self.mesh_inst_desc_buffer.clear();
        self.mesh_inst_desc_buffer.extend(
            descs
                .iter()
                .take(count)
                .map(|desc| Into::<RenMeshInstDesc>::into(*desc)),
        );
        unsafe {
            Error::new(ffi::ren_CreateMeshInsts(
                self.handle.get_mut(),
                self.mesh_inst_desc_buffer.as_ptr(),
                count,
                out.as_mut_ptr() as *mut RenMeshInst,
            ))?;
        }
        Ok(&mut out[..count])
    }

    pub fn destroy_mesh_inst(&mut self, mesh_inst: MeshInstID) {
        unsafe {
            ffi::ren_DestroyMeshInsts(self.handle.get_mut(), &mesh_inst.0, 1);
        }
    }

    pub fn destroy_mesh_insts(&mut self, mesh_insts: &[MeshInstID]) {
        unsafe {
            ffi::ren_DestroyMeshInsts(
                self.handle.get_mut(),
                mesh_insts.as_ptr() as *const RenMeshInst,
                mesh_insts.len(),
            );
        }
    }

    pub fn set_mesh_inst_matrix(&mut self, mesh_inst: MeshInstID, matrix: &[[f32; 4]; 4]) {
        unsafe {
            ffi::ren_SetMeshInstMatrices(self.handle.get_mut(), &mesh_inst.0, matrix, 1);
        }
    }

    pub fn set_mesh_inst_matrices(
        &mut self,
        mesh_insts: &[MeshInstID],
        matrices: &[[[f32; 4]; 4]],
    ) {
        let count = cmp::min(mesh_insts.len(), matrices.len());
        unsafe {
            ffi::ren_SetMeshInstMatrices(
                self.handle.get_mut(),
                mesh_insts.as_ptr() as *const RenMeshInst,
                matrices.as_ptr(),
                count,
            );
        }
    }

    pub fn create_dir_light(&mut self, desc: &DirLightDesc) -> Result<DirLightID, Error> {
        let mut light = REN_NULL_DIR_LIGHT;
        unsafe {
            Error::new(ffi::ren_CreateDirLights(
                self.handle.get_mut(),
                &(*desc).into(),
                1,
                &mut light,
            ))?;
        }
        Ok(DirLightID(light))
    }

    pub fn create_dir_lights<'a>(
        &mut self,
        descs: &[DirLightDesc],
        out: &'a mut [DirLightID],
    ) -> Result<&'a mut [DirLightID], Error> {
        let count = cmp::min(descs.len(), out.len());
        self.dir_light_desc_buffer.clear();
        self.dir_light_desc_buffer.extend(
            descs
                .iter()
                .map(|desc| Into::<RenDirLightDesc>::into(*desc)),
        );
        unsafe {
            Error::new(ffi::ren_CreateDirLights(
                self.handle.get_mut(),
                self.dir_light_desc_buffer.as_ptr(),
                count,
                out.as_mut_ptr() as *mut RenDirLight,
            ))?;
        }
        Ok(&mut out[..count])
    }

    pub fn destroy_dir_light(&mut self, light: DirLightID) {
        unsafe {
            ffi::ren_DestroyDirLights(self.handle.get_mut(), &light.0, 1);
        }
    }

    pub fn destroy_dir_lights(&mut self, lights: &[DirLightID]) {
        unsafe {
            ffi::ren_DestroyDirLights(
                self.handle.get_mut(),
                lights.as_ptr() as *const RenDirLight,
                lights.len(),
            );
        }
    }

    pub fn config_dir_light(
        &mut self,
        light: DirLightID,
        desc: &DirLightDesc,
    ) -> Result<(), Error> {
        unsafe {
            Error::new(ffi::ren_ConfigDirLights(
                self.handle.get_mut(),
                &light.0,
                &(*desc).into(),
                1,
            ))?;
        }
        Ok(())
    }

    pub fn config_dir_lights(
        &mut self,
        lights: &[DirLightID],
        descs: &[DirLightDesc],
    ) -> Result<(), Error> {
        let count = cmp::min(lights.len(), descs.len());
        self.dir_light_desc_buffer.extend(
            descs
                .iter()
                .take(count)
                .map(|desc| Into::<RenDirLightDesc>::into(*desc)),
        );
        unsafe {
            Error::new(ffi::ren_ConfigDirLights(
                self.handle.get_mut(),
                lights.as_ptr() as *const RenDirLight,
                self.dir_light_desc_buffer.as_ptr(),
                count,
            ))?;
        }
        Ok(())
    }
}
