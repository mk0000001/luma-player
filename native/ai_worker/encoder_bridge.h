#pragma once
#include <cstddef>
#include <cstdint>

struct whisper_context;
struct ggml_tensor;
extern "C" {
const ggml_tensor *luma_whisper_encoder_tensor(const whisper_context *context,const char *name);
size_t luma_whisper_encoder_tensor_count(const whisper_context *context);
bool luma_whisper_copy_encoder_features(const whisper_context *context,float *output,int64_t count);
bool luma_whisper_encoder_statistics(const whisper_context *context,double *totalMs,int32_t *rawCalls);
}
