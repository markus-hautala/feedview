// Implementations of single-header third-party code, compiled separately so their
// internals don't trip the warning flags used for FeedView's own sources.
#include "jpeg_encode.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

std::vector<uint8_t> encodeJpeg(const uint8_t* rgb, int width, int height, int quality) {
    std::vector<uint8_t> out;
    if (!rgb || width <= 0 || height <= 0) return out;
    out.reserve(size_t(width) * size_t(height) / 4);
    auto sink = [](void* ctx, void* data, int size) {
        auto* v = static_cast<std::vector<uint8_t>*>(ctx);
        auto* p = static_cast<const uint8_t*>(data);
        v->insert(v->end(), p, p + size);
    };
    if (!stbi_write_jpg_to_func(sink, &out, width, height, 3, rgb, quality)) out.clear();
    return out;
}
