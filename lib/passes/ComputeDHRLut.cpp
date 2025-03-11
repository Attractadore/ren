#include "ComputeDHRLut.hpp"
#include "../PipelineLoading.hpp"
#include "ComputeDHRLut.comp.hpp"

namespace ren {

void setup_compute_dhr_lut_pass(const PassCommonConfig &ccfg,
                                const ComputeDHRLutPassConfig &cfg) {
  *cfg.dhr_lut = ccfg.rcs->dhr_lut;
  if (!*cfg.dhr_lut) {
    constexpr usize DHR_LUT_SIZE = 128;
    ccfg.rcs->dhr_lut = ccfg.rgp->create_texture({
        .name = "dhr-lut",
        .format = TinyImageFormat_R16G16_UNORM,
        .width = DHR_LUT_SIZE,
        .height = DHR_LUT_SIZE,
        .ext = RgTexturePersistentInfo{},
    });
    auto pass = ccfg.rgb->create_pass({
        .name = "compute-dhr-lut",
        .queue = RgQueue::Async,
    });
    RgComputeDHRLutArgs args = {
        .lut = pass.write_texture("dhr-lut", ccfg.rcs->dhr_lut, cfg.dhr_lut),
    };
    pass.dispatch(ccfg.pipelines->compute_dhr_lut, args, DHR_LUT_SIZE,
                  DHR_LUT_SIZE);
  }
}

} // namespace ren
