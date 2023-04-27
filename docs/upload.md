# Resource uploads

There are several resource usage patterns that need to be supported:

* Static buffers
* Per-frame buffers
* Dynamic buffers
* Static textures
* Per-frame textures

# Static buffers

These buffers contain mesh vertex and index data and are uploaded once and never touched again.
If the destination buffer is host-visible, it's enough to just memcpy the data directly to it.
Otherwise, the process to upload a buffer is as follows:
* Copy buffer data to a staging buffer
* Submit a copy command from this staging buffer to the destination buffer
* Issue a barrier and/or wait on a semaphore (if a separate upload queue is used)
* Use the buffer

# Per-frame buffers

For data that changes almost 100% from frame to frame such as uniforms, compute shader outputs, etc,
buffers can be suballocated every frame from a bigger buffer managed by the render graph.
For data uploaded from the CPU, it's possible to either keep it in host-visible memory,
or use a staging buffer and copy it to device-local buffer, however the second approach is
more complicated and not guaranteed to be faster.

# Dynamic buffers

For semi-dynamic data such as transform/normal matrices, materials, lights, etc, there are several possible strategies:
* Just treat it like per-frame data
* Use a linear allocator and upload only new elements
* Use a free list allocator with deferred deletion

# Static textures

Unlike buffers, it's not possible to memcpy pixel data directly to a destination texture even if it is host-visible.
So the process to upload a texture is as follows:
* Copy pixel data to a staging buffer
* Submit a copy command from this staging to the destination texture
* Issue a barrier and/or wait on a semaphore (if a separate upload queue is used)
* Generate mipmaps
* Issue another barrier
* Use the texture

# Per-frame textures

Shadow maps, the depth buffer, and render targets are regenerated every frame,
so they should be managed by the render graph.
It also possible to try caching these textures' backing memory instead of reallocating it every frame.
