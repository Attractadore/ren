#pragma once

#ifndef SpdT
#error
#endif

#ifndef SPD_DEFAULT_VALUE
#error
#endif

#ifndef SpdArgsT
#error
#endif

#ifndef spd_load
#error
#endif

#ifndef spd_to_vec4
#error
#endif

#ifndef spd_from_vec4
#error
#endif

#ifndef spd_reduce_quad
#error
#endif

namespace ren::sh {

shared SpdT spd_tile[SPD_TILE_SIZE][SPD_TILE_SIZE + 1];
shared bool spd_quit;

void spd_load_tile_from_src(SpdArgsT args, RWTexture2D dst, uvec2 tile_pos) {
  uvec2 base_pos = tile_pos * uvec2(SPD_TILE_SIZE);
  for (uint x = gl_LocalInvocationID.x; x < SPD_TILE_SIZE; x += SPD_THREADS_X) {
    for (uint y = gl_LocalInvocationID.y; y < SPD_TILE_SIZE; y += SPD_THREADS_Y) {
      ivec2 pos = ivec2(base_pos) + ivec2(x, y);
      SpdT value = spd_load(args, pos);
      dst.Store(pos, spd_to_vec4(value));
      spd_tile[y][x] = value;
    }
  }
  barrier();
}

void spd_load_tile_from_dst(Handle<RWTexture2D> dst) {
  uvec2 size = TextureSize(Get(pc.dsts[SPD_NUM_TILE_MIPS - 1]));
  for (uint x = gl_LocalInvocationID.x; x < SPD_TILE_SIZE; x += SPD_THREADS_X) {
    for (uint y = gl_LocalInvocationID.y; y < SPD_TILE_SIZE; y += SPD_THREADS_Y) {
      SpdT value = SPD_DEFAULT_VALUE;
      ivec2 pos = ivec2(x, y);
      if (all(lessThan(pos, size))) {
        value = spd_from_vec4(CoherentLoad(dst, pos));
      }
      spd_tile[y][x] = value;
    }
  }
  barrier();
}

void spd_reduce_tile(Handle<RWTexture2D> dst, uvec2 tile_pos, uint tile_mip) {
  uvec2 tile_size = uvec2(SPD_TILE_SIZE >> tile_mip);
  uvec2 size = max(tile_size, uvec2(SPD_THREADS_X, SPD_THREADS_Y));
  uvec2 base_pos = tile_pos * tile_size;

  for (uint x = gl_LocalInvocationID.x; x < size.x; x += SPD_THREADS_X) {
    for (uint y = gl_LocalInvocationID.y; y < size.y; y += SPD_THREADS_Y) {
      uvec2 dst_pos = uvec2(x, y);
      ivec2 pos = ivec2(base_pos + dst_pos);

      SpdT value;

      bool store = all(lessThan(dst_pos, tile_size));
      if (store) {
        value = spd_reduce_quad(
          spd_tile[2 * y + 0][2 * x + 0],
          spd_tile[2 * y + 0][2 * x + 1],
          spd_tile[2 * y + 1][2 * x + 0],
          spd_tile[2 * y + 1][2 * x + 1]
        );
        if (tile_mip == SPD_NUM_TILE_MIPS - 1) {
          CoherentStore(dst, pos, spd_to_vec4(value));
        } else {
          Get(dst).Store(pos, spd_to_vec4(value));
        }
      }

      barrier();

      if (store) {
        spd_tile[y][x] = value;
      }
    }
  }

  barrier();
}

void spd_main(SpdArgsT args) {
  spd_load_tile_from_src(args, Get(pc.dsts[0]), gl_WorkGroupID.xy);
  for (uint mip = 1; mip < SPD_NUM_TILE_MIPS; ++mip) {
    if (mip == pc.num_mips) {
      break;
    }
    spd_reduce_tile(pc.dsts[mip], gl_WorkGroupID.xy, mip);
  }

  // If all destination mips were filled during the first pass, early out.
  if (pc.num_mips <= SPD_NUM_TILE_MIPS) {
    return;
  }

  if (gl_LocalInvocationIndex == 0) {
    const uint num_groups = gl_NumWorkGroups.x * gl_NumWorkGroups.y;
    uint num_finished = 1 + atomicAdd(*pc.spd_counter, 1);
    spd_quit = num_finished < num_groups;
  }

  barrier();

  if (spd_quit) {
    return;
  }

  global_image_barrier(MemoryOrder::AcquireRelease);

  spd_load_tile_from_dst(pc.dsts[SPD_NUM_TILE_MIPS - 1]);
  for (uint tile_mip = 1; tile_mip < SPD_NUM_TILE_MIPS; ++tile_mip) {
    uint mip = tile_mip + SPD_NUM_TILE_MIPS - 1;
    if (mip == pc.num_mips) {
      break;
    }
    spd_reduce_tile(pc.dsts[mip], uvec2(0, 0), tile_mip);
  }

  if (gl_LocalInvocationIndex == 0) {
    *pc.spd_counter = 0;
  }
}

}
