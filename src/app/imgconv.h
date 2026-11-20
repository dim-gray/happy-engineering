// imgconv.h -- image decode / resize / re-encode (stb) for re-texturing.
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "core/executor.h"

namespace he {

struct Image { int w = 0, h = 0; std::vector<uint8_t> rgba; bool hadAlpha = false; };

bool decodeImage(const std::vector<uint8_t>& bytes, Image& out, std::string& err);
// Resample to exactly w x h. fit: "cover" (fill + centre-crop), "contain" (letterbox), "stretch".
Image fitImage(const Image& src, int w, int h, const std::string& fit);
// ext: "png" "jpg" "bmp" "tga"
bool encodeImage(const Image& img, const std::string& ext, std::vector<uint8_t>& out, std::string& err);

class ImageConverter : public heng::IImageConverter {
public:
    bool convert(const std::vector<uint8_t>& orig, const std::string& origName,
                 const std::vector<uint8_t>& src, const std::string& fit,
                 std::vector<uint8_t>& out, std::string& note, std::string& err) override;
};

} // namespace he
