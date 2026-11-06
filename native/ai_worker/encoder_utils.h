#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace luma::encoder {
inline constexpr const char *PinnedModelSha256="4a3b92192b5d3578ff854a5876213e2e27af0c2d357492c2d14271e82c303658";
inline constexpr uintmax_t PinnedModelBytes=537819875;
std::string sha256File(const std::filesystem::path &path);
void verifyPinnedModel(const std::filesystem::path &path);
struct Metrics {
    size_t count=0;
    double mae=0, rmse=0, maxAbsolute=0, referenceRms=0, normalizedRmse=0, cosine=0;
};
Metrics compare(const std::vector<float> &reference,const std::vector<float> &actual);
} // namespace luma::encoder
