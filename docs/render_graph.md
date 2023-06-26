# Render Graph

Ren uses a render graph to manage the submission of work to the GPU.
The render graph handles the following tasks:
* Pass scheduling
* Temporal resource allocation
* Resource state transitions and barrier insertion between passes
* Command buffer recording and submission

## Frame pass definition

The first step is to define all of the passes for the current frame.
A pass represents a set of commands that read from and writes to a specific set of buffers and textures.
During a pass, the state of a resource doesn't change, and barriers can't be inserted.
Therefore, pass commands must not cause any data races.
Furthermore, all accesses to a texture during a pass must assume a single consistent layout.
In practical terms, this means a pass should contain either a single render pass, a single compute shader, or a single transfer command.

### Pass initialization

Passes can be of several types:
* **Graphics**: for graphics commands
* **Compute**: for compute commands that are executed on the graphics queue
* **Transfer**: for transfer commands that are executed on the graphics queue
* **Host**: for host commands

Each pass type has an associated pipeline stage mask, and all of the pass's commands' stage masks must be a subset of it.
The pipeline stage masks for each pass type are as follows:
* **Graphics**: `VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT`
* **Compute**: `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT`
* **Transfer**: `VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT`
* **Host**: `VK_PIPELINE_STAGE_2_NONE`

Host passes is useful for mapping and writing to host-visible buffers that will later be read by multiple passes.

#### C++ API

```C++

struct RGPassCreateInfo {
  /// Debug name
  std::string name;
  RGPassType type = RGPassType::None;
};

auto RGBuilder::create_pass(RGPassCreateInfo&& create_info) -> RGPassID;

```

### Pass resource definition

After creating a new pass, you need to specify its inputs and outputs.

Unlike device resources, render graph resources are virtual.
This means that multiple render graph resources might later be assigned to the same device resource.
The device resources that virtual resources are mapped to are called physical resources.
A virtual resource is also immutable and represents a texture or buffer with fixed data that won't change.

Virtual resources can be accessed in 3 ways:
* **Read**: the virtual resource is read and not modified by the pass
* **Create**: a new virtual resource is created and filled with data by the pass
* **Write**: similar to a read and a create access at the same time

Since virtual resources are immutable, a new virtual resource containing the updated data must be created for writes.
Additionally, to avoid unnecessary copying, each virtual resource can only be written once.
This implies that all reads of a virtual resource must be scheduled before the write to it.

Each access to a resource requires specifying the following information:
* An optional stage mask: it is used to narrow done the set of pipeline stages that the resources will be accessed in. If omitted, it is assumed to be equal to the pass's stage mask
* An access mask: specifies the types of resource accesses performed by the pass
* An optional layout (for textures): if not specified, it will be derived for the access mask. In practice, the layout has to be set manually only for transitioning to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`.

#### Temporal resources

Many rendering techniques require preserving buffers and textures from the previous frame.
Render graph resources can be marked as temporal when creating or writing them.
This allows their handles to be used in the next frame's passes.
Since temporal resources' data must be preserved for the next frame,
a resource can only be read (not written) in the current frame after being marked as temporal.

#### C++ API

```C++

struct RGTextureReadInfo {
  RGTextureID texture;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

void RGBuilder::read_texture(RGPassID pass, RGTextureReadInfo&& read_info);

struct RGTextureWriteInfo {
  /// Debug name for new virtual texture
  std::string name;
  RGTextureID texture;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  bool temporal = false;
};

auto RGBuilder::write_texture(PGPassID pass, RGTextureWriteInfo&& write_info) -> RGTextureID;

struct RGTextureCreateInfo {
  /// Debug name
  std::string name;
  VkImageType type = VK_IMAGE_TYPE_2D;
  VkFormat format = VK_FORMAT_UNDEFINED;
  glm::uvec3 size = {1, 1, 1};
  u32 num_mip_levels = 1;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  bool temporal = false;
};

auto RGBuilder::create_texture(RGPassID pass, RGTextureCreateInfo&& create_info) -> RGTextureID;

```

### External resources

External resources, such as swapchain images, can be imported into the render graph to be managed by it.

To support swapchain images, passes must also be able to wait on and signal semaphores.

#### C++ API

```C++

struct RGTextureImportInfo {
  /// Debug name
  std::string name;
  TextureView texture;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
  VkAccessFlags2 accesses = VK_ACCESS_2_NONE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

auto RGBuilder::import_texture(RGTextureImportInfo &&import_info) -> RGTextureID;

struct RGSemaphoreSignalInfo {
  Handle<Semaphore> semaphore;
  u64 value = 0;
  VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
};

void RGBuilder::wait_semaphore(RGPassID pass, RGSemaphoreSignalInfo &&signal_info);
void RGBuilder::signal_semaphore(RGPassID pass, RGSemaphoreSignalInfo &&signal_info);

```

### Pass execution callback

Once all the resources for a pass have been defined, you need to specify the commands that it will execute.
This is done through a callback function.

The callback function should be a lambda that captures its environment, including the resources that have just been created, by value.
The callback is invoked at render graph execution time to record work into a command buffer.

#### C++ API

The callback function takes three parameters:
* `Device &device`: a reference the device
* `RGRuntime &rg`: a reference to the render graph runtime. It can be used to access device resources assigned to the virtual resources
* `CommandBuffer &cmd`: a reference to the command buffer that pass commands should be recorded into

```C++

using RGCallback = std::function<void(Device &device, RGRuntime &rg, CommandBuffer &cmd)>;

void RGBuilder::set_callback(RGPassID pass, RGCallback cb);

```

## Presenting to the swapchain

After all passes have been defined, a texture must selected to be presented to the swapchain.
Internally, this implemented using two passes.
During the first, the texture is blitted to the swapchain image after waiting on the acquire semaphore.
Then, the swapchain image is transitioned to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`, and the present semaphore is signaled.

### C++ API

```C++

void RGBuilder::present(
    Swapchain &swapchain,
    RGTextureID texture,
    Handle<Semaphore> acquire_semaphore,
    Handle<Semaphore> present_semaphore
);

```

## Building the render graph

The render graph can now be built. This is a process that consists of several steps.

### Pass scheduling

The first step is to build a dependency graph of render graph's passes and try to topologically sort it.

#### Defs and Kills

The Def of a resources is the pass in which it is created (first used).

The Kill of a resource is the pass in which it is overwritten (last used).

For a Create-type access, the current pass is the Def of the virtual resource that will be created.

For a Write-type access, the current pass is the Kill of the old virtual resource and the Def of the new one.

#### Building the dependency graph

For each pass, a list of predecessors and a list successors are generated.
The predecessors of a pass are the Defs of all the resources that it reads or writes.
The successors of a pass are the Kills of all the resources that it reads.
An edge is added from each predecessor pass to the current pass and from the current pass to each successor pass.

#### Dependency graph topological sorting 

If the dependency graph is a DAG, then it can be topologically sorted.
Scheduling is performed using the following simple algorithm.

At each step, a pass is picked from a priority queue of unscheduled passes and scheduled.
Then, all of its successors that no longer have any unscheduled predecessors are pushed to the queue.
This process is repeated until the queue is empty.
If the queue is empty, but the list of scheduled passes is shorter that the total number of passes, then the dependency graph has a cycle, and can't be scheduled.

The queue initially holds all the passes that have no predecessors.
In the current implementation, the priority queue is set up so that passes whose predecessors were scheduled the longest time ago have the highest priority.

### Resource allocation

Resource allocation can be performed before pass scheduling.
However, scheduling information is required if we want to alias resources and save some memory.

Each virtual resource must be mapped for a physical one.
When no aliasing is performed, a new physical resource must be created for every Create-type access.
When a Write-type access is performed, the new virtual resource is mapped the same physical resource as the old one.

For buffers, a single buffer big enough to hold all of the frame's buffers is created,
and each physical buffer is suballocated from this big buffer. No aliasing is performed.
The big buffer is deleted at the end of a frame (after all of a frame's command buffers are recorded).

Textures are generally bigger then buffers, so aliasing is more worthwhile. 
For each scheduled pass, we look at the textures that it reads and writes.
If this pass is the last time a texture is accessed, and the texture is "big enough", then it is added to a pool of free textures.
Then we look at the textures that the current pass creates.
If there is already a compatible texture in the free texture pool, then it is removed from the pool and used.
Otherwise, a new one is created.

Textures can also be reused across frames.
At the start of texture allocation, the free texture pool is initialized to hold all the textures that were in it at the end of the previous frame.
They are also added to a set, and removed from it when are removed from the pool.
The textures that remain in this set were never used during texture allocation.
It's reasonable to assume that render graph resource usage does vary much from frame to frame.
So if a texture was not reused this frame, it probably won't be reused in the next one as well.
So it might as well be deleted.

At the end of a frame, all textures that aren't "big enough" are deleted, since they won't be reused.

### Barrier insertion

Now that all virtual resources have been mapped to a physical one, it's time to insert barriers.

At the beginning of barrier insertion, each physical resource's state (the combination of a stage mask, an access mask and a layout) is initialized.
If it was created this frame, it's state is set to undefined, otherwise it's state is set to the state that it was in after the end of the previous frame.

The passes are then processed in scheduled order.

For each virtual resource in a pass, a barrier is inserted (or not) based on how the it is accessed and the state of its physical resource at the beginning of the pass.
The physical resource's state is then updated based on the access.

All barriers are then batched into a single command at the beginning of a pass.
The command is stored in a callback similar to the way in which pass execution callbacks are stored.

### Pass batching

The last step is to batch passes.
Currently, a batch corresponds to a single `VkSubmitInfo2`, rather than a single command buffer.
A new batch is begun before a pass that waits for a semaphores, and after a pass that signals on a semaphore.

## Render graph execution

After the render graph has been built, it can be executed.
Internally, this just records a command buffer per batch pass and submits the batch to the graphics queue when it's done.
After all batches have been processed, the swapchain image is presented.

### C++ API

```C++

void RenderGraph::execute(CommandAllocator &cmd_alloc);

```

## Sources 
