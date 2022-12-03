#include "CommandAllocator.hpp"

namespace ren {
CommandAllocator::CommandAllocator(uint64_t pipeline_depth) {
  m_frame_resource_lists.resize(pipeline_depth);
  m_frame_index = m_frame_number = getPipelineDepth() - 1;
}

Vector<AnyRef> &CommandAllocator::getFrameResourceList() {
  return m_frame_resource_lists[getFrameIndex()];
}

uint64_t CommandAllocator::getPipelineDepth() const {
  return m_frame_resource_lists.size();
}

void CommandAllocator::addFrameResource(AnyRef resource) {
  getFrameResourceList().push_back(std::move(resource));
}

void CommandAllocator::beginFrame() {
  ++m_frame_number;
  m_frame_index = m_frame_number % getPipelineDepth();
  waitForFrame(getFrameNumber() - getPipelineDepth());
  getFrameResourceList().clear();
  beginFrameImpl();
}

void CommandAllocator::endFrame() { endFrameImpl(); }
} // namespace ren
