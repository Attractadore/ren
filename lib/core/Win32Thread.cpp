#if _WIN32
#include "Win32.hpp"
#include "ren/core/Array.hpp"
#include "ren/core/Futex.hpp"
#include "ren/core/Math.hpp"
#include "ren/core/Thread.hpp"
#include "ren/core/Vm.hpp"

#include <Windows.h>
#include <atomic>
#include <common/TracySystem.hpp>

namespace ren {

namespace {

HANDLE thread_handle(Thread thread) { return (HANDLE)thread.m_handle; }

struct Win32ThreadParam {
  int *launched = nullptr;
  const ThreadDesc *desc = nullptr;
};

DWORD win32_thread_start(void *void_param) {
  auto *param = (const Win32ThreadParam *)void_param;
  ThreadDesc desc = *param->desc;

  std::atomic_ref(*param->launched).store(true, std::memory_order_release);
  futex_wake_one(param->launched);

  tracy::SetThreadName(desc.name);

  desc.proc(desc.param);

  return EXIT_SUCCESS;
}

constexpr u32 NUM_GROUP_PROCESSORS = 64;

} // namespace

Span<Processor> cpu_topology(NotNull<Arena *> arena) {
  ScratchArena scratch;

  DynamicArray<Processor> processors;

  DWORD buffer_size = 0;
  bool success =
      GetLogicalProcessorInformationEx(RelationAll, nullptr, &buffer_size);
  ren_assert(not success);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    WIN32_CHECK_ERROR(GetLastError(), "GetLogicalProcessorInformationEx");
  }
  u8 *buffer = (u8 *)scratch->allocate(
      buffer_size, alignof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX));
  WIN32_CHECK(GetLogicalProcessorInformationEx(
      RelationAll, (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)buffer,
      &buffer_size));

  u32 core_id = 0;

  usize offset = 0;
  while (offset < buffer_size) {
    auto *processor_information =
        (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)&buffer[offset];
    switch (processor_information->Relationship) {
    default:
      break;
    case RelationProcessorCore: {
      PROCESSOR_RELATIONSHIP processor = processor_information->Processor;
      u32 group_id = processor.GroupMask[0].Group;
      u64 group_mask = processor.GroupMask[0].Mask;
      while (group_mask) {
        u32 group_processor_id = find_msb(group_mask);
        u64 processor_id = NUM_GROUP_PROCESSORS * group_id + group_processor_id;
        while (processor_id >= processors.m_size) {
          processors.push(scratch);
        }
        processors[processor_id].cpu = processor_id;
        processors[processor_id].core = core_id;
        group_mask = group_mask & ~(u64(1) << group_processor_id);
      }
      core_id++;
    } break;
    case RelationNumaNodeEx: {
      NUMA_NODE_RELATIONSHIP node = processor_information->NumaNode;
      for (GROUP_AFFINITY group : Span(node.GroupMasks, node.GroupCount)) {
        u32 group_id = group.Group;
        u64 group_mask = group.Mask;
        while (group_mask) {
          u32 group_processor_id = find_msb(group_mask);
          u64 processor_id =
              NUM_GROUP_PROCESSORS * group_id + group_processor_id;
          while (processor_id >= processors.m_size) {
            processors.push(scratch);
          }
          processors[processor_id].numa = node.NodeNumber;
        }
      }
    } break;
    }
    offset += processor_information->Size;
  }

  return Span(processors).copy(arena);
}

usize thread_min_stack_size() { return vm_allocation_granularity(); }

Thread thread_create(const ThreadDesc &desc) {
  int launched = false;
  Win32ThreadParam param = {
      .launched = &launched,
      .desc = &desc,
  };
  HANDLE handle = CreateThread(nullptr, desc.stack_size, win32_thread_start,
                               &param, 0, nullptr);
  if (!handle) {
    WIN32_CHECK_ERROR(GetLastError(), "CreateThread");
  }

  if (desc.affinity.m_size > 0) {
    ScratchArena scratch;
    ULONG buffer_size = 0;
    bool success =
        GetSystemCpuSetInformation(nullptr, 0, &buffer_size, nullptr, 0);
    ren_assert(!success);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      WIN32_CHECK_ERROR(GetLastError(), "GetSystemCpuSetInformation");
    }
    u8 *buffer = (u8 *)scratch->allocate(buffer_size,
                                         alignof(SYSTEM_CPU_SET_INFORMATION));
    WIN32_CHECK(GetSystemCpuSetInformation((SYSTEM_CPU_SET_INFORMATION *)buffer,
                                           buffer_size, &buffer_size, nullptr,
                                           0));
    DynamicArray<ULONG> cpu_set_ids;
    usize offset = 0;
    while (offset < buffer_size) {
      auto *cpu_set_information =
          (const SYSTEM_CPU_SET_INFORMATION *)&buffer[offset];
      switch (cpu_set_information->Type) {
      default:
        break;
      case CpuSetInformation: {
        const auto &cpu_set = cpu_set_information->CpuSet;
        for (u32 cpu : desc.affinity) {
          if (cpu == cpu_set.Group * NUM_GROUP_PROCESSORS +
                         cpu_set.LogicalProcessorIndex) {
            cpu_set_ids.push(scratch, cpu_set.Id);
          }
        }
      } break;
      }
      offset += cpu_set_information->Size;
    }
    WIN32_CHECK(SetThreadSelectedCpuSets(handle, cpu_set_ids.m_data,
                                         cpu_set_ids.m_size));
  }

wait:
  if (not std::atomic_ref(launched).load(std::memory_order_acquire)) {
    futex_wait(&launched, false);
    goto wait;
  }

  return {(void *)handle};
}

void thread_exit(int code) { ExitThread(code); }

int thread_join(Thread thread) {
  HANDLE handle = thread_handle(thread);
  WaitForSingleObject(handle, INFINITE);
  DWORD ret;
  WIN32_CHECK(GetExitCodeThread(handle, &ret));
  CloseHandle(handle);
  return ret;
}

static const DWORD MAIN_THREAD = GetCurrentThreadId();
bool is_main_thread() { return GetCurrentThreadId() == MAIN_THREAD; }

} // namespace ren

#endif
