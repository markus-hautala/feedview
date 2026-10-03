#pragma once

#include <cstdint>
#include <vector>

// Encodes packed 8-bit RGB as JPEG (stb_image_write). Returns an empty vector on failure.
std::vector<uint8_t> encodeJpeg(const uint8_t* rgb, int width, int height, int quality = 75);
