#pragma once
#include "Support/Vector.hpp"
#include "Sync.hpp"
#include "Texture.hpp"

namespace ren {
class CommandBuffer;

class CommandAllocator {
  SmallVector<Vector<AnyRef>, 3> m_frame_resource_lists;
  unsigned m_frame_number;
  unsigned m_frame_index;

private:
  Vector<AnyRef> &getFrameResourceList();

protected:
  uint64_t getFrameNumber() const { return m_frame_number; }
  uint64_t getFrameIndex() const { return m_frame_index; }

  virtual void waitForFrame(uint64_t frame) = 0;
  virtual void beginFrameImpl() = 0;
  virtual void endFrameImpl() = 0;

public:
  CommandAllocator(uint64_t pipeline_depth);
  virtual ~CommandAllocator() = default;

  uint64_t getPipelineDepth() const;

  virtual CommandBuffer *allocateCommandBuffer() = 0;

  void beginFrame();
  void endFrame();

  void addFrameResource(AnyRef resoure);

  void addFrameResource(Texture texture) {
    addFrameResource(std::move(texture.handle));
  }

  void addFrameResource(TextureView view) {
    addFrameResource(std::move(view.texture));
  }

  void addFrameResource(SyncObject sync) {
    addFrameResource(std::move(sync.handle));
  }
};
} // namespace ren
