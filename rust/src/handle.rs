use crate::ffi::RenScene;

pub trait DestroyHandle {
    unsafe fn destroy(handle: *mut Self);
}

#[derive(Clone)]
pub struct Handle<T: DestroyHandle>(*mut T);

impl<T: DestroyHandle> Handle<T> {
    pub unsafe fn new(handle: *mut T) -> Self {
        Self(handle)
    }

    pub fn get(&self) -> *const T {
        self.0
    }

    pub fn get_mut(&mut self) -> *mut T {
        self.0
    }
}

impl<T: DestroyHandle> Drop for Handle<T> {
    fn drop(&mut self) {
        unsafe { T::destroy(self.0) }
    }
}

pub trait DestroySceneHandle {
    unsafe fn destroy(scene: *mut RenScene, handle: Self);
}

pub struct SceneHandle<T: DestroySceneHandle + Copy>(T, *mut RenScene);

impl<T: DestroySceneHandle + Copy> SceneHandle<T> {
    pub unsafe fn new(scene: *mut RenScene, handle: T) -> Self {
        Self(handle, scene)
    }

    pub fn get(&self) -> T {
        self.0
    }

    pub fn get_scene_mut(&mut self) -> *mut RenScene {
        self.1
    }
}

impl<T: DestroySceneHandle + Copy> Drop for SceneHandle<T> {
    fn drop(&mut self) {
        unsafe { T::destroy(self.1, self.0) }
    }
}
