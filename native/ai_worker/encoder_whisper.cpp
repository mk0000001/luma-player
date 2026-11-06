#include "encoder_whisper.h"
#include "encoder_bridge.h"
#include <ggml.h>
#include <ggml-backend.h>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace luma::encoder {
WhisperSource::WhisperSource(const std::filesystem::path &model,bool withState,bool flashAttention):context_(nullptr,whisper_free){
    ggml_backend_load_all();
    auto params=whisper_context_default_params();params.use_gpu=false;params.flash_attn=flashAttention;
    const auto path=model.u8string();
    context_.reset(withState?whisper_init_from_file_with_params(path.c_str(),params):
                             whisper_init_from_file_with_params_no_state(path.c_str(),params));
    if(!context_)throw std::runtime_error("cannot load Whisper model");
    if(tensorCount()!=7+15*dimensions().layers)throw std::runtime_error("incomplete encoder tensor catalog");
}
TensorData WhisperSource::read(const std::string &name,const ov::Shape &expected)const{
    const auto tensor=luma_whisper_encoder_tensor(context_.get(),name.c_str());
    if(!tensor)throw std::runtime_error("missing encoder tensor: "+name);
    if(!ggml_is_contiguous(tensor) || !tensor->buffer || !tensor->data ||
       !ggml_backend_buffer_is_host(tensor->buffer))throw std::runtime_error("encoder tensor requires unmodified CPU storage: "+name);
    ov::Shape shape;
    if(name=="encoder.conv1.bias" || name=="encoder.conv2.bias") {
        if(tensor->ne[0]!=1 || tensor->ne[2]!=1 || tensor->ne[3]!=1)throw std::runtime_error("wrong convolution bias shape: "+name);
        shape={static_cast<size_t>(tensor->ne[1])};
    } else {
        int rank=4;while(rank>1 && tensor->ne[rank-1]==1)--rank;
        for(int i=rank-1;i>=0;--i)shape.push_back(static_cast<size_t>(tensor->ne[i]));
    }
    if(shape!=expected)throw std::runtime_error("wrong encoder tensor shape: "+name);
    if(tensor->type!=GGML_TYPE_F32 && tensor->type!=GGML_TYPE_F16 && tensor->type!=GGML_TYPE_Q5_0)
        throw std::runtime_error("unsupported encoder tensor type: "+name);
    std::vector<unsigned char> packed(ggml_nbytes(tensor));
    ggml_backend_tensor_get(tensor,packed.data(),0,packed.size());
    TensorData result{shape,std::vector<float>(static_cast<size_t>(ggml_nelements(tensor)))};
    if(tensor->type==GGML_TYPE_F32)std::memcpy(result.values.data(),packed.data(),packed.size());
    else {
        const auto traits=ggml_get_type_traits(tensor->type);
        if(!traits || !traits->to_float)throw std::runtime_error("missing GGML dequantizer: "+name);
        traits->to_float(packed.data(),result.values.data(),static_cast<int64_t>(result.values.size()));
    }
    for(auto value:result.values)if(!std::isfinite(value))throw std::runtime_error("nonfinite encoder tensor: "+name);
    return result;
}
Dimensions WhisperSource::dimensions()const{
    return {static_cast<size_t>(whisper_model_n_mels(context_.get())),static_cast<size_t>(whisper_model_n_audio_ctx(context_.get())),
            static_cast<size_t>(whisper_model_n_audio_state(context_.get())),static_cast<size_t>(whisper_model_n_audio_head(context_.get())),
            static_cast<size_t>(whisper_model_n_audio_layer(context_.get()))};
}
size_t WhisperSource::tensorCount()const{return luma_whisper_encoder_tensor_count(context_.get());}
std::vector<float> WhisperSource::encode(const std::vector<float> &mel,int threads)const{
    const auto dims=dimensions();
    if(mel.size()!=dims.mels*dims.context*2 || threads<1 || threads>16)throw std::runtime_error("invalid native reference input");
    if(whisper_set_mel(context_.get(),mel.data(),static_cast<int>(dims.context*2),static_cast<int>(dims.mels))!=0 ||
       whisper_encode(context_.get(),0,threads)!=0)throw std::runtime_error("native GGML CPU encoder failed");
    std::vector<float> result(dims.context*dims.state);
    if(!luma_whisper_copy_encoder_features(context_.get(),result.data(),static_cast<int64_t>(result.size())))
        throw std::runtime_error("cannot copy native encoder features");
    return result;
}
}
