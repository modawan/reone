/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "reone/graphics/format/tgawriter.h"

#include "reone/graphics/dxtutil.h"
#include "reone/graphics/texture.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryoutput.h"

namespace reone {

namespace graphics {

static constexpr int kHeaderSize = 18;

static size_t checkedPixelSize(uint64_t width, uint64_t height, uint64_t channels) {
    if (channels == 0) {
        throw ValidationException("TGA channel count must be positive");
    }
    if (width == 0 || height == 0 || width > std::numeric_limits<uint16_t>::max() ||
        height > std::numeric_limits<uint16_t>::max()) {
        throw ValidationException("TGA dimensions must be between 1 and 65535");
    }
    if (width > std::numeric_limits<uint64_t>::max() / height ||
        width * height > std::numeric_limits<uint64_t>::max() / channels) {
        throw ValidationException("TGA pixel size overflows");
    }
    uint64_t size = width * height * channels;
    if (size > std::numeric_limits<size_t>::max()) {
        throw ValidationException("TGA pixel size exceeds addressable memory");
    }
    return static_cast<size_t>(size);
}

static uint64_t checkedTextureHeight(int height, size_t layers) {
    if (height <= 0 || layers == 0 ||
        layers > std::numeric_limits<uint64_t>::max() / static_cast<uint64_t>(height)) {
        throw ValidationException("TGA texture layer dimensions overflow");
    }
    return static_cast<uint64_t>(height) * layers;
}

TgaWriter::TgaWriter(std::shared_ptr<Texture> texture) :
    _texture(std::move(texture)) {
    if (!_texture) {
        throw ValidationException("TGA writer requires a texture");
    }
}

TgaWriter::TgaWriter(
    uint32_t width,
    uint32_t height,
    PixelFormat format,
    ByteBuffer pixels,
    TgaOrigin origin) :
    _rawImage(RawImage {width, height, format, std::move(pixels), origin}) {
}

void TgaWriter::save(IOutputStream &out, bool compress) {
    TGADataType dataType;
    int depth;
    uint32_t width;
    uint32_t totalHeight;
    TgaOrigin origin;
    std::vector<uint8_t> pixels;
    if (_rawImage) {
        width = _rawImage->width;
        totalHeight = _rawImage->height;
        origin = _rawImage->origin;
        pixels = getRawImagePixels(dataType, depth);
        if (compress) {
            dataType = TGADataType::RGBA_RLE;
        }
    } else {
        width = static_cast<uint32_t>(_texture->width());
        uint64_t height = checkedTextureHeight(
            _texture->height(),
            _texture->layers().size());
        origin = TgaOrigin::BottomLeft;
        checkedPixelSize(width, height, 1);
        totalHeight = static_cast<uint32_t>(height);
        pixels = getTexturePixels(compress, dataType, depth);
    }
    size_t expectedSize = checkedPixelSize(width, totalHeight, depth / 8);
    if (pixels.size() != expectedSize) {
        throw ValidationException("TGA pixel buffer length does not match its dimensions and format");
    }

    // Write Header

    uint8_t header[kHeaderSize] {0};
    header[0] = 0; // ID length
    header[1] = 0; // color map type
    header[2] = static_cast<uint8_t>(dataType);
    memset(header + 4, 0, 5);
    header[8] = 0;                    // X origin (lo)
    header[9] = 0;                    // X origin (hi)
    header[10] = 0;                   // Y origin (lo)
    header[11] = 0;                   // Y origin (hi)
    header[12] = width % 256;         // width (lo)
    header[13] = width / 256;         // width (hi)
    header[14] = totalHeight % 256;   // height (lo)
    header[15] = totalHeight / 256;   // height (hi)
    header[16] = depth;               // pixel size
    header[17] = (depth == 32 ? 8 : 0) |
                 (origin == TgaOrigin::TopLeft ? 0x20 : 0);
    out.write(reinterpret_cast<char *>(header), kHeaderSize);

    // Write Scanlines

    size_t scanlineSize = checkedPixelSize(width, 1, depth / 8);
    if (depth >= 24 && compress) {
        for (uint32_t wrote = 0; wrote < totalHeight; ++wrote) {
            size_t offset = wrote * scanlineSize;
            writeRLE(&pixels[offset], static_cast<int>(width), depth, out);
        }
    } else {
        out.writeAll(reinterpret_cast<char *>(pixels.data()), pixels.size());
    }
}

ByteBuffer TgaWriter::toBytes(bool compress) {
    ByteBuffer result;
    MemoryOutputStream out(result);
    save(out, compress);
    return result;
}

std::vector<uint8_t> TgaWriter::getTexturePixels(bool compress, TGADataType &dataType, int &depth) const {
    switch (_texture->pixelFormat()) {
    case PixelFormat::R8:
        dataType = TGADataType::Grayscale;
        depth = 8;
        break;
    case PixelFormat::RGB8:
    case PixelFormat::BGR8:
    case PixelFormat::DXT1:
        dataType = compress ? TGADataType::RGBA_RLE : TGADataType::RGBA;
        depth = 24;
        break;
    case PixelFormat::RGBA8:
    case PixelFormat::BGRA8:
    case PixelFormat::DXT5:
        dataType = compress ? TGADataType::RGBA_RLE : TGADataType::RGBA;
        depth = 32;
        break;
    default:
        throw ValidationException("Unsupported texture pixel format: " + std::to_string(static_cast<int>(_texture->pixelFormat())));
    }

    size_t numLayers = _texture->layers().size();
    size_t numPixels = checkedPixelSize(_texture->width(), _texture->height(), 1);
    uint64_t totalHeight = checkedTextureHeight(_texture->height(), numLayers);
    size_t resultSize = checkedPixelSize(_texture->width(), totalHeight, depth / 8);
    std::vector<uint8_t> result(resultSize);
    uint8_t *pixels = result.data();

    for (size_t i = 0; i < numLayers; ++i) {
        auto &layer = _texture->layers()[i];
        if (!layer.pixels) {
            throw ValidationException("TGA texture layer has no pixels");
        }
        auto layerPixelsPtr = reinterpret_cast<const uint8_t *>(layer.pixels->data());
        size_t expectedLayerSize;
        switch (_texture->pixelFormat()) {
        case PixelFormat::R8:
            expectedLayerSize = numPixels;
            break;
        case PixelFormat::RGB8:
        case PixelFormat::BGR8:
            expectedLayerSize = numPixels * 3;
            break;
        case PixelFormat::RGBA8:
        case PixelFormat::BGRA8:
            expectedLayerSize = numPixels * 4;
            break;
        case PixelFormat::DXT1:
            expectedLayerSize =
                ((static_cast<size_t>(_texture->width()) + 3) / 4) *
                ((static_cast<size_t>(_texture->height()) + 3) / 4) * 8;
            break;
        case PixelFormat::DXT5:
            expectedLayerSize =
                ((static_cast<size_t>(_texture->width()) + 3) / 4) *
                ((static_cast<size_t>(_texture->height()) + 3) / 4) * 16;
            break;
        default:
            throw ValidationException("Unsupported texture pixel format");
        }
        if (layer.pixels->size() != expectedLayerSize) {
            throw ValidationException("TGA texture layer length does not match its dimensions and format");
        }

        switch (_texture->pixelFormat()) {
        case PixelFormat::R8:
            memcpy(pixels, layerPixelsPtr, numPixels);
            pixels += numPixels;
            break;
        case PixelFormat::RGB8:
            for (size_t j = 0; j < numPixels; ++j) {
                *(pixels++) = layerPixelsPtr[2];
                *(pixels++) = layerPixelsPtr[1];
                *(pixels++) = layerPixelsPtr[0];
                layerPixelsPtr += 3;
            }
            break;
        case PixelFormat::RGBA8:
            for (size_t j = 0; j < numPixels; ++j) {
                *(pixels++) = layerPixelsPtr[2];
                *(pixels++) = layerPixelsPtr[1];
                *(pixels++) = layerPixelsPtr[0];
                *(pixels++) = layerPixelsPtr[3];
                layerPixelsPtr += 4;
            }
            break;
        case PixelFormat::BGR8:
            memcpy(pixels, layerPixelsPtr, 3 * numPixels);
            pixels += 3 * numPixels;
            break;
        case PixelFormat::BGRA8:
            memcpy(pixels, layerPixelsPtr, 4 * numPixels);
            pixels += 4 * numPixels;
            break;
        case PixelFormat::DXT1: {
            std::vector<uint32_t> decompPixels(numPixels);
            decompressDXT1(_texture->width(), _texture->height(), layerPixelsPtr, &decompPixels[0]);
            uint32_t *decompPtr = &decompPixels[0];
            for (size_t j = 0; j < numPixels; ++j) {
                uint32_t rgb = *(decompPtr++);
                *(pixels++) = (rgb >> 8) & 0xff;
                *(pixels++) = (rgb >> 16) & 0xff;
                *(pixels++) = (rgb >> 24) & 0xff;
            }
            break;
        }
        case PixelFormat::DXT5: {
            std::vector<uint32_t> decompPixels(numPixels);
            decompressDXT5(_texture->width(), _texture->height(), layerPixelsPtr, &decompPixels[0]);
            uint32_t *decompPtr = &decompPixels[0];
            for (size_t j = 0; j < numPixels; ++j) {
                uint32_t rgba = *(decompPtr++);
                *(pixels++) = (rgba >> 8) & 0xff;
                *(pixels++) = (rgba >> 16) & 0xff;
                *(pixels++) = (rgba >> 24) & 0xff;
                *(pixels++) = rgba & 0xff;
            }
            break;
        }
        default:
            break;
        }
    }

    return result;
}

std::vector<uint8_t> TgaWriter::getRawImagePixels(TGADataType &dataType, int &depth) const {
    dataType = TGADataType::RGBA;
    size_t channels;
    switch (_rawImage->format) {
    case PixelFormat::RGB8:
    case PixelFormat::BGR8:
        channels = 3;
        depth = 24;
        break;
    case PixelFormat::RGBA8:
    case PixelFormat::BGRA8:
        channels = 4;
        depth = 32;
        break;
    default:
        throw ValidationException("Raw TGA output supports only RGB8, BGR8, RGBA8, and BGRA8 pixels");
    }

    size_t expectedSize = checkedPixelSize(_rawImage->width, _rawImage->height, channels);
    if (_rawImage->pixels.size() != expectedSize) {
        throw ValidationException("TGA pixel buffer length does not match its dimensions and format");
    }

    std::vector<uint8_t> result(expectedSize);
    bool alreadyBgr = _rawImage->format == PixelFormat::BGR8 ||
                      _rawImage->format == PixelFormat::BGRA8;
    for (size_t offset = 0; offset < expectedSize; offset += channels) {
        result[offset] = _rawImage->pixels[offset + (alreadyBgr ? 0 : 2)];
        result[offset + 1] = _rawImage->pixels[offset + 1];
        result[offset + 2] = _rawImage->pixels[offset + (alreadyBgr ? 2 : 0)];
        if (channels == 4) {
            result[offset + 3] = _rawImage->pixels[offset + 3];
        }
    }
    return result;
}

void TgaWriter::writeRLE(const uint8_t *pixels, int width, int depth, IOutputStream &out) {
    size_t bytes = static_cast<size_t>(depth / 8);
    int x = 0;
    while (x < width) {
        int run = 1;
        while (x + run < width && run < 128 &&
               memcmp(pixels + static_cast<size_t>(x) * bytes,
                      pixels + static_cast<size_t>(x + run) * bytes,
                      bytes) == 0) {
            ++run;
        }
        if (run >= 2) {
            out.writeByte(static_cast<char>(0x80 | (run - 1)));
            out.write(
                reinterpret_cast<const char *>(pixels + static_cast<size_t>(x) * bytes),
                bytes);
            x += run;
            continue;
        }

        int rawStart = x++;
        while (x < width && x - rawStart < 128) {
            run = 1;
            while (x + run < width && run < 128 &&
                   memcmp(pixels + static_cast<size_t>(x) * bytes,
                          pixels + static_cast<size_t>(x + run) * bytes,
                          bytes) == 0) {
                ++run;
            }
            if (run >= 2) {
                break;
            }
            ++x;
        }
        int rawLength = x - rawStart;
        out.writeByte(static_cast<char>(rawLength - 1));
        out.write(
            reinterpret_cast<const char *>(pixels + static_cast<size_t>(rawStart) * bytes),
            static_cast<size_t>(rawLength) * bytes);
    }
}

} // namespace graphics

} // namespace reone
