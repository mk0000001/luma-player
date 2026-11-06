
// LUMA_BACKEND_INFO_BEGIN: inspect initialized compute backends, not a log
// emitted before initialization. This small extension is applied at configure.
extern "C" WHISPER_API bool luma_whisper_has_gpu_backend(struct whisper_context *context) {
    if (!context || !context->state) return false;
    for (auto backend : context->state->backends) {
        auto device = ggml_backend_get_device(backend);
        if (device && (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU ||
                       ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_IGPU)) return true;
    }
    return false;
}
extern "C" WHISPER_API const char *luma_whisper_gpu_device(struct whisper_context *context) {
    if (!context || !context->state) return nullptr;
    for (auto backend : context->state->backends) {
        auto device = ggml_backend_get_device(backend);
        if (device && (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU ||
                       ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_IGPU)) return ggml_backend_dev_name(device);
    }
    return nullptr;
}
extern "C" WHISPER_API const char *luma_whisper_gpu_backend(struct whisper_context *context) {
    if (!context || !context->state) return nullptr;
    for (auto backend : context->state->backends) {
        auto device = ggml_backend_get_device(backend);
        if (device && (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU ||
                       ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_IGPU)) return ggml_backend_reg_name(ggml_backend_dev_backend_reg(device));
    }
    return nullptr;
}
extern "C" WHISPER_API const struct ggml_tensor *luma_whisper_encoder_tensor(const struct whisper_context *context,const char *name) {
    if (!context || !name || std::strncmp(name,"encoder.",8)!=0) return nullptr;
    size_t length=0;
    while (length<128 && name[length]) ++length;
    if (length==128) return nullptr;
    const auto found=context->model.tensors.find(name);
    return found==context->model.tensors.end()?nullptr:found->second;
}
extern "C" WHISPER_API size_t luma_whisper_encoder_tensor_count(const struct whisper_context *context) {
    if (!context) return 0;
    size_t count=0;
    for (const auto &entry:context->model.tensors) if (entry.first.compare(0,8,"encoder.")==0) ++count;
    return count;
}
extern "C" WHISPER_API bool luma_whisper_copy_encoder_features(const struct whisper_context *context,float *output,int64_t count) {
    if (!context || !context->state || !output || count<=0) return false;
    const auto tensor=context->state->embd_enc;
    if (!tensor || !tensor->buffer || !tensor->data || tensor->type!=GGML_TYPE_F32 ||
        !ggml_is_contiguous(tensor) || count!=ggml_nelements(tensor)) return false;
    ggml_backend_tensor_get(tensor,output,0,static_cast<size_t>(count)*sizeof(float));
    return true;
}
extern "C" WHISPER_API bool luma_whisper_encoder_statistics(const struct whisper_context *context,double *totalMs,int32_t *rawCalls) {
    if (!context || !context->state || !totalMs || !rawCalls) return false;
    *totalMs=static_cast<double>(context->state->t_encode_us)/1000.0;
    *rawCalls=context->state->n_encode;
    return true;
}
