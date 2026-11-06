#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace luma::encoder {
// Throws before the external encoder is initialized if any identity check fails.
void verifyEncoderIdentity(const std::filesystem::path &model,
                           const std::filesystem::path &encoderXml);

namespace detail {
// The caller has already authenticated the source file. The public entry point
// supplies only the pinned model identity; the artifact verifier also serves
// bounded fixtures without loading a 538 MB Whisper model.
struct VerifiedSourceIdentity {
    std::string sha256;
    std::uintmax_t bytes;
};
void verifyEncoderArtifact(const std::filesystem::path &encoderXml,
                           const VerifiedSourceIdentity &source);
} // namespace detail
} // namespace luma::encoder
