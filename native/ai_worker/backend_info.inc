
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
