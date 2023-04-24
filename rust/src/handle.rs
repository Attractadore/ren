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
