#include "warps/dsp/warps_fx_c_api.h"

#include "warps/dsp/warps_fx.h"

namespace {
WarpsFX g_warps_fx;
}

extern "C" {

void warps_fx_engine_init(float sample_rate) {
  g_warps_fx.Init(sample_rate);
}

void warps_fx_engine_process(
    float* inL,
    float* inR,
    float* outL,
    float* outR,
    int size) {
  g_warps_fx.Process(inL, inR, outL, outR, size);
}

void warps_fx_engine_set_algorithm(float algo) {
  g_warps_fx.SetAlgorithm(algo);
}

void warps_fx_engine_set_parameter(float param) {
  g_warps_fx.SetParameter(param);
}

void warps_fx_engine_set_drive(float d1, float d2) {
  g_warps_fx.SetDrive(d1, d2);
}

}  // extern "C"
