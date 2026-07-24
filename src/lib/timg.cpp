#include "timg.hpp"
#include <algorithm>
#include <fstream>
#include <gx/GXEnum.h>
#include <nlohmann/json.hpp>
#include <png.h>
#include "assets.hpp"
#include "dusk/io.hpp"

namespace assets {

bool readPNG_RGBA8(const std::filesystem::path& filename, u32& outWidth, u32& outHeight,
    std::vector<GXColor>& outData) {
    FILE* fp = fopen(filename.string().c_str(), "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for reading\n", filename.generic_string().c_str());
        return false;
    }

    // Verify PNG signature
    u8 sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8) != 0) {
        fprintf(stderr, "%s is not a valid PNG file\n", filename.generic_string().c_str());
        fclose(fp);
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);  // we already read the 8-byte signature

    png_read_info(png, info);

    u32 width = png_get_image_width(png, info);
    u32 height = png_get_image_height(png, info);
    png_byte colorType = png_get_color_type(png, info);
    png_byte bitDepth = png_get_bit_depth(png, info);

    // Normalize everything to 8-bit RGBA regardless of source format
    if (bitDepth == 16)
        png_set_strip_16(png);

    if (colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);

    // Expand grayscale bit depths < 8 to 8 bits
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
        png_set_expand_gray_1_2_4_to_8(png);

    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);

    // Add opaque alpha channel if missing
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    outData.resize(width * height);

    std::vector<png_bytep> rowPointers(height);
    for (u32 y = 0; y < height; y++) {
        rowPointers[y] = (u8*)outData.data() + (size_t)y * width * 4;
    }

    png_read_image(png, rowPointers.data());

    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);

    outWidth = width;
    outHeight = height;
    return true;
}

bool writePNG_RGBA8(const std::filesystem::path& filename, u32 width, u32 height,
    const std::vector<GXColor>& rgbaData) {
    FILE* fp = fopen(filename.string().c_str(), "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for writing\n", filename.generic_string().c_str());
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);

    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    // Build row pointers directly into the source buffer (no copy needed)
    std::vector<png_bytep> rowPointers(height);
    for (u32 y = 0; y < height; y++) {
        rowPointers[y] = const_cast<png_bytep>((const u8*)rgbaData.data() + (size_t)y * width * 4);
    }

    png_write_image(png, rowPointers.data());
    png_write_end(png, nullptr);

    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return true;
}

struct ImageConvertBase {
    // Image conversion base class; the base class is the same for packing/unpacking to keep the
    // code size down
    const ResTIMG& mHeader;
    const std::span<const u8> mInputBuffer;

    // Used for both
    std::vector<GXColor> mRgba;
    u32 mWidth;
    u32 mHeight;

    // Packing images only
    std::vector<u8> mOutputBuffer;
    bool mHasAlpha = false;

    virtual u8 getBlockSize() const = 0;
    virtual u8 getBlockWidth() const = 0;
    virtual u8 getBlockHeight() const = 0;
    virtual void unpackBlock(int x, int y, const std::span<const u8>& data) = 0;
    virtual std::vector<u8> packBlock(int blockX, int blockY) = 0;

    void writePixel(int x, int y, GXColor pixel) {
        if (x < mWidth && y < mHeight) {
            mRgba[(y * mWidth) + x] = pixel;
        }
    }

    GXColor readPixel(int x, int y) {
        if (x < mWidth && y < mHeight) {
            GXColor pixel = mRgba[(y * mWidth) + x];
            if (mHasAlpha == false && pixel.a < 0xFF) {
                mHasAlpha = true;
            }
            return pixel;
        }
        return {0, 0, 0, 0};
    }

    ImageConvertBase(const ResTIMG& header, const std::span<const u8>& buffer)
        : mHeader(header), mInputBuffer(buffer), mRgba(header.width * header.height), mWidth(header.width), mHeight(header.height) {}

    ImageConvertBase(const ResTIMG& header, std::vector<GXColor>& rgba)
        : mHeader(header), mRgba(rgba), mWidth(header.width), mHeight(header.height) {}

    std::vector<GXColor>& toRGBA() {
        int widthInBlocks = (mWidth + getBlockWidth() - 1) / getBlockWidth();
        int heightInBlocks = (mHeight + getBlockHeight() - 1) / getBlockHeight();

        int offset = mHeader.imageOffset;
        for (int y = 0; y < heightInBlocks; y++) {
            for (int x = 0; x < widthInBlocks; x++) {
                unpackBlock(x * getBlockWidth(), y * getBlockHeight(),
                    mInputBuffer.subspan(offset, getBlockSize()));
                offset += getBlockSize();
            }
        }

        return mRgba;
    }

    // Converts srgb in range 0-1 to linear color space
    static constexpr float srgbToLinear(float srgb) {
        return srgb <= 0.04045f ? srgb / 12.92 : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    }

    // Converts linear in range 0-1 to srgb color space
    static constexpr float linearToSrgb(float srgb) {
        return srgb <= 0.0031308f ? srgb * 12.92f : 1.055f * std::pow(srgb, 1.0f / 2.4f) - 0.055f;
    }


    void mipmap() {
        // Mutates the mRgba image and halves width and height
        u32 newWidth = mWidth / 2;
        u32 newHeight = mHeight / 2;

        std::vector<GXColor> newRgba(newWidth*newHeight);
        for (u32 y = 0; y < newHeight; y++) {
            u32 y0 = std::min(y*2, mHeight - 1);
            u32 y1 = std::min((y*2)+1, mHeight - 1);

            for (u32 x = 0; x < newWidth; x++) {
                u32 x0 = std::min(x*2, mWidth - 1);
                u32 x1 = std::min((x*2)+1, mWidth - 1);

                GXColor p0 = readPixel(x0,y0);
                GXColor p1 = readPixel(x1,y0);
                GXColor p2 = readPixel(x0,y1);
                GXColor p3 = readPixel(x1,y1);

                float r = linearToSrgb((srgbToLinear(((float)p0.r)/255.0f)+srgbToLinear(((float)p1.r)/255.0f)+srgbToLinear(((float)p2.r)/255.0f)+srgbToLinear(((float)p3.r)/255.0f))/4.0f);
                float g = linearToSrgb((srgbToLinear(((float)p0.g)/255.0f)+srgbToLinear(((float)p1.g)/255.0f)+srgbToLinear(((float)p2.g)/255.0f)+srgbToLinear(((float)p3.g)/255.0f))/4.0f);
                float b = linearToSrgb((srgbToLinear(((float)p0.b)/255.0f)+srgbToLinear(((float)p1.b)/255.0f)+srgbToLinear(((float)p2.b)/255.0f)+srgbToLinear(((float)p3.b)/255.0f))/4.0f);
                u8 a = (u8)std::clamp((int)std::lround(((float)(p0.a+p1.a+p2.a+p3.a))/4.0f),0,255);
                
                newRgba[(y * newWidth) + x] = {(u8)std::clamp((int)std::lround(r*255.0f),0,255),(u8)std::clamp((int)std::lround(g*255.0f),0,255),(u8)std::clamp((int)std::lround(b*255.0f),0,255),a};
            }
        }

        mWidth = newWidth;
        mHeight = newHeight;
        mRgba = std::move(newRgba);
    }

    virtual std::vector<u8> toTIMG() {
        std::vector<u8> timg;

        for (int m = 0; m < mHeader.mipmapCount; m++) {
            int widthInBlocks = (mWidth + getBlockWidth() - 1) / getBlockWidth();
            int heightInBlocks = (mHeight + getBlockHeight() - 1) / getBlockHeight();
            for (int y = 0; y < heightInBlocks; y++) {
                for (int x = 0; x < widthInBlocks; x++) {
                    const auto block = packBlock(x * getBlockWidth(), y * getBlockHeight());
                    timg.insert(timg.end(), block.begin(), block.end());
                }
            }
            if (m != mHeader.mipmapCount-1) {
                mipmap();
            }
        }

        return timg;
    }
};

struct ImageConvertI4 : ImageConvertBase {
    static GXColor I4LowerToRGBA(u8 i4Lower) {
        u8 x = ExpandTo8<4>(i4Lower & 0xF);
        return {x, x, x, 0xFF};
    }
    static GXColor I4HighToRGBA(u8 i4High) {
        u8 x = ExpandTo8<4>(i4High >> 4);
        return {x, x, x, 0xFF};
    }
    static u8 RGBAtoI4(GXColor colorHigh, GXColor colorLow) {
        u8 high = ((colorHigh.r + colorHigh.g + colorHigh.b) / 3) >> 4;
        u8 low = ((colorLow.r + colorLow.g + colorLow.b) / 3) >> 4;
        return (high << 4) | low;
    }

    virtual u8 getBlockSize() const { return 32; }
    virtual u8 getBlockWidth() const { return 8; }
    virtual u8 getBlockHeight() const { return 8; }

    virtual void unpackBlock(int blockX, int blockY, const std::span<const u8>& data) {
        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                writePixel(blockX + x, blockY + y, I4HighToRGBA(data[offset]));
                x++;
                writePixel(blockX + x, blockY + y, I4LowerToRGBA(data[offset]));
                offset++;
            }
        }
    }
    virtual std::vector<u8> packBlock(int blockX, int blockY) {
        std::vector<u8> block(getBlockSize());

        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                GXColor colorHigh = readPixel(blockX + x, blockY + y);
                x++;
                GXColor colorLow = readPixel(blockX + x, blockY + y);
                block[offset] = RGBAtoI4(colorHigh, colorLow);
                offset++;
            }
        }

        return block;
    }

    ImageConvertI4(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertBase(header, buffer) {}
    ImageConvertI4(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertBase(header, rgba) {}
};

struct ImageConvertI8 : ImageConvertBase {
    static GXColor I8ToRGBA(u8 i8) {
        u8 x = i8;
        return {x, x, x, 0xFF};
    }
    static u8 RGBAtoI8(GXColor color) { return (color.r + color.g + color.b) / 3; }

    virtual u8 getBlockSize() const { return 32; }
    virtual u8 getBlockWidth() const { return 8; }
    virtual u8 getBlockHeight() const { return 4; }

    virtual void unpackBlock(int blockX, int blockY, const std::span<const u8>& data) {
        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                writePixel(blockX + x, blockY + y, I8ToRGBA(data[offset]));
                offset++;
            }
        }
    }
    virtual std::vector<u8> packBlock(int blockX, int blockY) {
        std::vector<u8> block(getBlockSize());

        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                GXColor color = readPixel(blockX + x, blockY + y);
                block[offset] = RGBAtoI8(color);
                offset++;
            }
        }

        return block;
    }

    ImageConvertI8(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertBase(header, buffer) {}
    ImageConvertI8(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertBase(header, rgba) {}
};

struct ImageConvertIA4 : ImageConvertBase {
    static GXColor IA4ToRGBA(u8 ia4) {
        u8 x = ExpandTo8<4>(ia4 & 0xf);
        return {x, x, x, ExpandTo8<4>(ia4 >> 4)};
    }
    static u8 RGBAtoIA4(GXColor color) {
        return ((color.a >> 4) << 4) | (((color.r + color.g + color.b) / 3) >> 4);
    }

    virtual u8 getBlockSize() const { return 32; }
    virtual u8 getBlockWidth() const { return 8; }
    virtual u8 getBlockHeight() const { return 4; }

    virtual void unpackBlock(int blockX, int blockY, const std::span<const u8>& data) {
        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                writePixel(blockX + x, blockY + y, IA4ToRGBA(data[offset]));
                offset++;
            }
        }
    }
    virtual std::vector<u8> packBlock(int blockX, int blockY) {
        std::vector<u8> block(getBlockSize());

        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                GXColor color = readPixel(blockX + x, blockY + y);
                block[offset] = RGBAtoIA4(color);
                offset++;
            }
        }

        return block;
    }

    ImageConvertIA4(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertBase(header, buffer) {}
    ImageConvertIA4(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertBase(header, rgba) {}
};

struct ImageConvertIA8 : ImageConvertBase {
    static GXColor IA8ToRGBA(u16 ia8) {
        u8 x = ia8 & 0xFF;
        return {x, x, x, (u8)(ia8 >> 8)};
    }
    static u16 RGBAtoIA8(GXColor color) {
        return (color.a << 8) | (((color.r + color.g + color.b) / 3) & 0xFF);
    }

    virtual u8 getBlockSize() const { return 32; }
    virtual u8 getBlockWidth() const { return 4; }
    virtual u8 getBlockHeight() const { return 4; }

    virtual void unpackBlock(int blockX, int blockY, const std::span<const u8>& data) {
        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                writePixel(
                    blockX + x, blockY + y, IA8ToRGBA((data[offset] << 8) | (data[offset + 1])));
                offset += 2;
            }
        }
    }
    virtual std::vector<u8> packBlock(int blockX, int blockY) {
        std::vector<u8> block(getBlockSize());

        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                GXColor color = readPixel(blockX + x, blockY + y);
                u16 ia8 = RGBAtoIA8(color);
                block[offset] = (ia8 >> 8);
                block[offset + 1] = ia8 & 0xFF;
                offset += 2;
            }
        }

        return block;
    }

    ImageConvertIA8(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertBase(header, buffer) {}
    ImageConvertIA8(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertBase(header, rgba) {}
};

struct ImageConvertRGB565 : ImageConvertBase {
    static GXColor RGB565ToRGBA(u16 rgb565) {
        return {ExpandTo8<5>((rgb565 >> 11) & 0b11111), ExpandTo8<6>((rgb565 >> 5) & 0b111111),
            ExpandTo8<5>(rgb565 & 0b11111), 0xFF};
    }
    static u16 RGBAToRGB565(GXColor color) {
        return ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);
    }

    virtual u8 getBlockSize() const { return 32; }
    virtual u8 getBlockWidth() const { return 4; }
    virtual u8 getBlockHeight() const { return 4; }

    virtual void unpackBlock(int blockX, int blockY, const std::span<const u8>& data) {
        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                writePixel(
                    blockX + x, blockY + y, RGB565ToRGBA((data[offset] << 8) | (data[offset + 1])));
                offset += 2;
            }
        }
    }
    virtual std::vector<u8> packBlock(int blockX, int blockY) {
        std::vector<u8> block(getBlockSize());

        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                GXColor color = readPixel(blockX + x, blockY + y);
                u16 rgb565 = RGBAToRGB565(color);
                block[offset] = (rgb565 >> 8);
                block[offset + 1] = rgb565 & 0xFF;
                offset += 2;
            }
        }

        return block;
    }

    ImageConvertRGB565(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertBase(header, buffer) {}
    ImageConvertRGB565(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertBase(header, rgba) {}
};

struct ImageConvertRGB5A3 : ImageConvertBase {
    static GXColor RGB5a3ToRGBA(u16 rgb5a3) {
        bool hasAlpha = (rgb5a3 & 0x8000) == 0;
        if (hasAlpha) {
            return {ExpandTo8<4>((rgb5a3 >> 8) & 0xf), ExpandTo8<4>((rgb5a3 >> 4) & 0xf),
                ExpandTo8<4>(rgb5a3 & 0xf), ExpandTo8<3>((rgb5a3 >> 12) & 0b111)};
        } else {
            return {ExpandTo8<5>((rgb5a3 >> 10) & 0x1f), ExpandTo8<5>((rgb5a3 >> 5) & 0x1f),
                ExpandTo8<5>(rgb5a3 & 0x1f), 0xFF};
        }
    }
    static u16 RGBAToRGB5a3(GXColor color) {
        if (color.a != 0xFF) {
            return ((color.a >> 5) << 12) | ((color.r >> 4) << 8) | ((color.g >> 4) << 4) |
                   (color.b >> 4);
        } else {
            return 0x8000 | ((color.r >> 3) << 10) | ((color.g >> 3) << 5) | (color.b >> 3);
        }
    }

    virtual u8 getBlockSize() const { return 32; }
    virtual u8 getBlockWidth() const { return 4; }
    virtual u8 getBlockHeight() const { return 4; }

    virtual void unpackBlock(int blockX, int blockY, const std::span<const u8>& data) {
        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                writePixel(
                    blockX + x, blockY + y, RGB5a3ToRGBA((data[offset] << 8) | (data[offset + 1])));
                offset += 2;
            }
        }
    }
    virtual std::vector<u8> packBlock(int blockX, int blockY) {
        std::vector<u8> block(getBlockSize());

        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                GXColor color = readPixel(blockX + x, blockY + y);
                u16 rgb5a3 = RGBAToRGB5a3(color);
                block[offset] = (rgb5a3 >> 8);
                block[offset + 1] = rgb5a3 & 0xFF;
                offset += 2;
            }
        }

        return block;
    }

    ImageConvertRGB5A3(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertBase(header, buffer) {}
    ImageConvertRGB5A3(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertBase(header, rgba) {}
};

struct ImageConvertRGBA8 : ImageConvertBase {
    virtual u8 getBlockSize() const { return 64; }
    virtual u8 getBlockWidth() const { return 4; }
    virtual u8 getBlockHeight() const { return 4; }

    virtual void unpackBlock(int blockX, int blockY, const std::span<const u8>& data) {
        int rOffset = 0;
        int gOffset = 1;
        int bOffset = 32;
        int aOffset = 33;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                GXColor color = {data[rOffset], data[gOffset], data[bOffset], data[aOffset]};
                writePixel(blockX + x, blockY + y, color);
                rOffset += 2;
                gOffset += 2;
                bOffset += 2;
                aOffset += 2;
            }
        }
    }
    virtual std::vector<u8> packBlock(int blockX, int blockY) {
        std::vector<u8> block(getBlockSize());

        int rOffset = 0;
        int gOffset = 1;
        int bOffset = 32;
        int aOffset = 33;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                GXColor color = readPixel(blockX + x, blockY + y);
                block[rOffset] = color.r;
                block[gOffset] = color.g;
                block[bOffset] = color.b;
                block[aOffset] = color.a;
                rOffset += 2;
                gOffset += 2;
                bOffset += 2;
                aOffset += 2;
            }
        }

        return block;
    }

    ImageConvertRGBA8(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertBase(header, buffer) {}
    ImageConvertRGBA8(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertBase(header, rgba) {}
};

struct ImageConvertPaletteBase : ImageConvertBase {
    std::vector<GXColor> mPaletteColors;
    std::vector<u16> mIndexBuffer;
    u16 mNumColors;
    s32 mPaletteOffset;

    virtual size_t getPaletteSize() const = 0;

    std::vector<GXColor> decodePaletteColors() {
        const std::span<const u8> mPaletteBuffer = mInputBuffer.subspan(mHeader.paletteOffset);
        std::vector<GXColor> paletteColors(mHeader.numColors);

        int offset = 0;
        for (int i = 0; i < mHeader.numColors; i++) {
            GXColor color = {};
            u16 val = mPaletteBuffer[offset] << 8 | mPaletteBuffer[offset + 1];
            switch (mHeader.colorFormat) {
            case GX_TL_IA8:
                color = ImageConvertIA8::IA8ToRGBA(val);
                break;
            case GX_TL_RGB565:
                color = ImageConvertRGB565::RGB565ToRGBA(val);
                break;
            case GX_TL_RGB5A3:
                color = ImageConvertRGB5A3::RGB5a3ToRGBA(val);
                break;
            }
            paletteColors[i] = color;
            offset += 2;
        }
        return paletteColors;
    }

    std::vector<GXColor> encodePaletteColors(size_t size) {
        // Use the median cut algorithm on the rgba buffer to get the colors used in the palette

        // Get all unique colors (most palette images being re-converted don't need to be quantized
        // again)
        std::vector<GXColor> uniqueColors = mRgba;
        std::sort(uniqueColors.begin(), uniqueColors.end(), [](const GXColor& a, const GXColor& b) {
            return std::tie(a.r, a.g, a.b, a.a) < std::tie(b.r, b.g, b.b, b.a);
        });
        uniqueColors.erase(std::unique(uniqueColors.begin(), uniqueColors.end(),
                               [](const GXColor& a, const GXColor& b) {
                                   return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
                               }),
            uniqueColors.end());

        // If we don't need to quantize, don't
        if (uniqueColors.size() <= size) {
            return uniqueColors;
        }

        std::vector<std::vector<GXColor> > buckets;
        buckets.push_back({uniqueColors});  // Set the original bucket to all the pixels
        while (buckets.size() < size) {
            // Find which bucket needs to be split next
            int splitIdx = -1;
            int bestRange = -1;
            for (size_t i = 0; i < buckets.size(); i++) {
                if (buckets[i].size() < 2)
                    continue;
                int channel = getLargestChannel(buckets[i]);
                u8 mn = 255, mx = 0;
                for (const auto& c : buckets[i]) {
                    u8 v = (channel == 0) ? c.r : (channel == 1) ? c.g : (channel == 2) ? c.b : c.a;
                    mn = std::min(mn, v);
                    mx = std::max(mx, v);
                }
                int range = mx - mn;
                if (range > bestRange) {
                    bestRange = range;
                    splitIdx = i;
                }
            }
            if (splitIdx == -1) {
                break;  // Can't split any more
            }

            // Sort the bucket
            std::vector<GXColor>& bucket = buckets[splitIdx];
            int channel = getLargestChannel(bucket);
            std::sort(bucket.begin(), bucket.end(), [channel](const GXColor& a, const GXColor& b) {
                u8 va = (channel == 0) ? a.r : (channel == 1) ? a.g : (channel == 2) ? a.b : a.a;
                u8 vb = (channel == 0) ? b.r : (channel == 1) ? b.g : (channel == 2) ? b.b : b.a;
                return va < vb;
            });

            // Split the bucket at the middle
            size_t mid = bucket.size() / 2;
            std::vector<GXColor> newBucket;
            newBucket.assign(bucket.begin() + mid, bucket.end());
            bucket.resize(mid);
            buckets.push_back(std::move(newBucket));
        }

        std::vector<GXColor> palette;
        palette.reserve(buckets.size());
        for (const auto& b : buckets) {
            if (!b.empty()) {
                palette.push_back(averageColor(b));
            }
        }

        return palette;
    }

    u16 readIndexBuffer(int x, int y) {
        if (x < mWidth && y < mHeight) {
            return mIndexBuffer[(y * mWidth) + x];
        }
        return 0;
    }

    std::vector<u16> getIndexBuffer() {
        std::vector<u16> indices(mRgba.size());
        for (size_t i = 0; i < indices.size(); i++) {
            GXColor pixel = mRgba[i];
            u16 bestIdx = 0;
            int bestDist = std::numeric_limits<int>::max();

            for (u16 j = 0; j < mPaletteColors.size(); j++) {
                int dr = (int)pixel.r - mPaletteColors[j].r;
                int dg = (int)pixel.g - mPaletteColors[j].g;
                int db = (int)pixel.b - mPaletteColors[j].b;
                int da = (int)pixel.a - mPaletteColors[j].a;
                int dist = dr * dr + dg * dg + db * db + da * da;

                if (dist < bestDist) {
                    bestDist = dist;
                    bestIdx = j;
                }
            }

            indices[i] = bestIdx;
        }
        return indices;
    }

    virtual std::vector<u8> toTIMG() {
        mPaletteColors = encodePaletteColors(getPaletteSize());
        for (const auto& c : mPaletteColors) {
            if (c.a < 0xFF) {
                mHasAlpha = true;
                break;
            }
        }
        mNumColors = mPaletteColors.size();
        mIndexBuffer = getIndexBuffer();
        auto timg = ImageConvertBase::toTIMG();
        mPaletteOffset = timg.size() + mHeader.imageOffset;

        for (int i = 0; i < mNumColors; i++) {
            u16 color = 0;
            switch (mHeader.colorFormat) {
            case GX_TL_IA8:
                color = ImageConvertIA8::RGBAtoIA8(mPaletteColors[i]);
                break;
            case GX_TL_RGB565:
                color = ImageConvertRGB565::RGBAToRGB565(mPaletteColors[i]);
                break;
            case GX_TL_RGB5A3:
                color = ImageConvertRGB5A3::RGBAToRGB5a3(mPaletteColors[i]);
                break;
            }
            timg.push_back(color >> 8);
            timg.push_back(color & 0xFF);
        }

        return timg;
    }

    ImageConvertPaletteBase(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertBase(header, buffer), mPaletteColors(decodePaletteColors()) {}
    ImageConvertPaletteBase(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertBase(header, rgba) {}

private:
    static int getLargestChannel(const std::vector<GXColor>& colors) {
        u8 minR = 255, maxR = 0, minG = 255, maxG = 0;
        u8 minB = 255, maxB = 0, minA = 255, maxA = 0;
        for (const auto& c : colors) {
            minR = std::min(minR, c.r);
            maxR = std::max(maxR, c.r);
            minG = std::min(minG, c.g);
            maxG = std::max(maxG, c.g);
            minB = std::min(minB, c.b);
            maxB = std::max(maxB, c.b);
            minA = std::min(minA, c.a);
            maxA = std::max(maxA, c.a);
        }
        int rangeR = maxR - minR;
        int rangeG = maxG - minG;
        int rangeB = maxB - minB;
        int rangeA = maxA - minA;
        int maxRange = std::max({rangeR, rangeG, rangeB, rangeA});
        if (maxRange == rangeR)
            return 0;
        if (maxRange == rangeG)
            return 1;
        if (maxRange == rangeB)
            return 2;
        return 3;
    }

    static GXColor averageColor(const std::vector<GXColor>& colors) {
        u64 sumR = 0, sumG = 0, sumB = 0, sumA = 0;
        for (const auto& c : colors) {
            sumR += c.r;
            sumG += c.g;
            sumB += c.b;
            sumA += c.a;
        }
        size_t n = colors.size();
        return {(u8)(sumR / n), (u8)(sumG / n), (u8)(sumB / n), (u8)(sumA / n)};
    }
};

struct ImageConvertC4 : ImageConvertPaletteBase {
    GXColor C4HighToRGBA(u8 indexHigh) const {
        if ((indexHigh >> 4) >= mHeader.numColors) {
            return {};
        }
        return mPaletteColors[indexHigh >> 4];
    }
    GXColor C4LowToRGBA(u8 indexLow) const {
        if ((indexLow & 0xF) >= mHeader.numColors) {
            return {};
        }
        return mPaletteColors[indexLow & 0xF];
    }

    virtual u8 getBlockSize() const { return 32; }
    virtual u8 getBlockWidth() const { return 8; }
    virtual u8 getBlockHeight() const { return 8; }
    virtual size_t getPaletteSize() const { return 16; };

    virtual void unpackBlock(int blockX, int blockY, const std::span<const u8>& data) {
        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                writePixel(blockX + x, blockY + y, C4HighToRGBA(data[offset]));
                x++;
                writePixel(blockX + x, blockY + y, C4LowToRGBA(data[offset]));
                offset++;
            }
        }
    }
    virtual std::vector<u8> packBlock(int blockX, int blockY) {
        std::vector<u8> block(getBlockSize());

        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                u16 indexHigh = readIndexBuffer(blockX + x, blockY + y);
                x++;
                u16 indexLow = readIndexBuffer(blockX + x, blockY + y);
                block[offset] = (indexHigh << 4) | (indexLow & 0xF);
                offset++;
            }
        }

        return block;
    }

    ImageConvertC4(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertPaletteBase(header, buffer) {};
    ImageConvertC4(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertPaletteBase(header, rgba) {};
};

struct ImageConvertC8 : ImageConvertPaletteBase {
    GXColor C8ToRGBA(u8 index) const {
        if (index >= mHeader.numColors) {
            return {};
        }
        return mPaletteColors[index];
    }

    virtual u8 getBlockSize() const { return 32; }
    virtual u8 getBlockWidth() const { return 8; }
    virtual u8 getBlockHeight() const { return 4; }
    virtual size_t getPaletteSize() const { return 256; };

    virtual void unpackBlock(int blockX, int blockY, const std::span<const u8>& data) {
        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                writePixel(blockX + x, blockY + y, C8ToRGBA(data[offset]));
                offset++;
            }
        }
    }
    virtual std::vector<u8> packBlock(int blockX, int blockY) {
        std::vector<u8> block(getBlockSize());

        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                u16 index = readIndexBuffer(blockX + x, blockY + y);
                block[offset] = (u8)index;
                offset++;
            }
        }

        return block;
    }

    ImageConvertC8(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertPaletteBase(header, buffer) {};
    ImageConvertC8(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertPaletteBase(header, rgba) {};
};

struct ImageConvertC14X2 : ImageConvertPaletteBase {
    GXColor C14X2ToRGBA(u16 index) const {
        index = index & 0x3FFF;
        if (index >= mHeader.numColors) {
            return {};
        }
        return mPaletteColors[index];
    }

    virtual u8 getBlockSize() const { return 32; }
    virtual u8 getBlockWidth() const { return 4; }
    virtual u8 getBlockHeight() const { return 4; }
    virtual size_t getPaletteSize() const { return 16384; };

    virtual void unpackBlock(int blockX, int blockY, const std::span<const u8>& data) {
        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                writePixel(
                    blockX + x, blockY + y, C14X2ToRGBA((data[offset] << 8) | (data[offset + 1])));
                offset += 2;
            }
        }
    }
    virtual std::vector<u8> packBlock(int blockX, int blockY) {
        std::vector<u8> block(getBlockSize());

        int offset = 0;
        for (int y = 0; y < getBlockHeight(); y++) {
            for (int x = 0; x < getBlockWidth(); x++) {
                u16 index = readIndexBuffer(blockX + x, blockY + y);
                index = index & 0x3FFF;
                block[offset] = index >> 8;
                block[offset + 1] = index & 0xFF;
                offset += 2;
            }
        }

        return block;
    }

    ImageConvertC14X2(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertPaletteBase(header, buffer) {};
    ImageConvertC14X2(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertPaletteBase(header, rgba) {};
};

struct ImageConvertCMPR : ImageConvertBase {
    static GXColor interpolateRGBA(GXColor c1, GXColor c2, int num, int den) {
        auto ch = [&](u8 a, u8 b) -> u8 {
            return (u8)(((den - num) * (int)a + num * (int)b) / den);
        };
        return {ch(c1.r, c2.r), ch(c1.g, c2.g), ch(c1.b, c2.b), 0xFF};
    }

    virtual u8 getBlockSize() const { return 32; }
    virtual u8 getBlockWidth() const { return 8; }
    virtual u8 getBlockHeight() const { return 8; }

    virtual void unpackBlock(int blockX, int blockY, const std::span<const u8>& data) {
        int offset = 0;
        for (int subY = 0; subY < 2; subY++) {
            for (int subX = 0; subX < 2; subX++) {
                u16 v1 = (data[offset] << 8) | (data[offset + 1]);
                u16 v2 = (data[offset + 2] << 8) | (data[offset + 3]);
                offset += 4;
                std::array<GXColor, 4> c;
                c[0] = ImageConvertRGB565::RGB565ToRGBA(v1);
                c[1] = ImageConvertRGB565::RGB565ToRGBA(v2);

                if (v1 > v2) {
                    // No alpha, lerp the colors
                    c[2] = interpolateRGBA(c[0], c[1], 1, 3);
                    c[3] = interpolateRGBA(c[0], c[1], 2, 3);
                } else {
                    // Has alpha, get the middle color
                    c[2] = interpolateRGBA(c[0], c[1], 1, 2);
                    c[3] = {0, 0, 0, 0};
                }

                u32 b = (data[offset] << 24) | (data[offset + 1] << 16) | (data[offset + 2] << 8) |
                        data[offset + 3];
                offset += 4;

                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++) {
                        writePixel(x + (subX * 4) + blockX, y + (subY * 4) + blockY, c[b >> 30]);
                        b = b << 2;
                    }
                }
            }
        }
    }

    bool isExactBlock(const std::vector<GXColor>& pixels, std::vector<u8>& outBlock) const {
        // Check if our original pixels were dxt-1 compressed, and give the original output
        bool isTransparent = false;

        // Get distinct colors, check if there are more than 4 (or 3) colors in the block
        std::vector<GXColor> distinct;
        for (const auto& p : pixels) {
            if (p.a == 0) {
                isTransparent = true;
                continue;
            }
            if (p.a != 0xFF) {
                // We have a block with non-zero transperency, the source couldn't have been dxt1
                return false;
            }
            if (std::find_if(distinct.begin(), distinct.end(),
                    [&](const GXColor& c) {
                        return c.r == p.r && c.g == p.g && c.b == p.b && c.a == p.a;
                    }) == distinct.end())
            {
                if (distinct.size() >= 4) {
                    return false;
                }
                distinct.push_back(p);
            }
        }
        if (isTransparent && distinct.size() > 3) {
            return false;
        }

        // Get the pixel values that were originally rgb565 encoded
        std::vector<std::pair<GXColor, u16> > candidates;
        for (const auto& c : distinct) {
            u16 rgb565 = ImageConvertRGB565::RGBAToRGB565(c);
            GXColor u = ImageConvertRGB565::RGB565ToRGBA(rgb565);
            if (c.r == u.r && c.g == u.g && c.b == u.b && c.a == u.a) {
                candidates.push_back({u, rgb565});
            }
        }

        for (const auto& [c0, rgb565_0] : candidates) {
            for (const auto& [c1, rgb565_1] : candidates) {
                bool punchThrough = (rgb565_0 <= rgb565_1);
                if (punchThrough != isTransparent) {
                    continue;
                }

                std::array<GXColor, 4> pal = {c0, c1};
                if (!punchThrough) {
                    pal[2] = interpolateRGBA(c0, c1, 1, 3);
                    pal[3] = interpolateRGBA(c0, c1, 2, 3);
                } else {
                    pal[2] = interpolateRGBA(c0, c1, 1, 2);
                    pal[3] = {0, 0, 0, 0};
                }

                u32 indices = 0;
                bool ok = true;
                for (int i = 0; i < 16 && ok; i++) {
                    int match = -1;
                    if (pixels[i].a == 0 && punchThrough) {
                        match = 3;
                    } else {
                        for (int k = 0; k < (punchThrough ? 3 : 4); k++) {
                            if (pal[k].r == pixels[i].r && pal[k].g == pixels[i].g &&
                                pal[k].b == pixels[i].b)
                            {
                                match = k;
                                break;
                            }
                        }
                    }
                    if (match < 0) {
                        ok = false;
                        break;
                    }
                    indices = (indices << 2) | ((u8)match);
                }
                if (ok) {
                    outBlock.push_back(rgb565_0 >> 8);
                    outBlock.push_back(rgb565_0 & 0xFF);
                    outBlock.push_back(rgb565_1 >> 8);
                    outBlock.push_back(rgb565_1 & 0xFF);
                    outBlock.push_back(indices >> 24);
                    outBlock.push_back((indices >> 16) & 0xFF);
                    outBlock.push_back((indices >> 8) & 0xFF);
                    outBlock.push_back(indices & 0xFF);
                    return true;
                }
            }
        }

        return false;
    }

    float dxt1Lum(const GXColor& c) const { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }
    int dxt1distSq(const GXColor& a, const GXColor& b) const {
        int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
        return dr * dr + dg * dg + db * db;
    }

    void encodeDXT1(const std::vector<GXColor>& pixels, std::vector<u8>& outBlock) {
        bool isTransparent = false;
        GXColor lo = {};
        GXColor hi = {};
        float loL = 1e9f;
        float hiL = -1e9f;
        for (const auto& p : pixels) {
            if (p.a < 128) {
                isTransparent = true;
                continue;
            }
            float l = dxt1Lum(p);
            if (l < loL) {
                loL = l;
                lo = p;
            }
            if (l > hiL) {
                hiL = l;
                hi = p;
            }
        }

        // Put them in the correct order based on if we have transparency
        u16 rgb565_0 = ImageConvertRGB565::RGBAToRGB565(hi);
        u16 rgb565_1 = ImageConvertRGB565::RGBAToRGB565(lo);
        if (isTransparent) {
            if (rgb565_0 > rgb565_1) {
                std::swap(rgb565_0, rgb565_1);
            }
        } else {
            if (rgb565_0 <= rgb565_1 && rgb565_0 != rgb565_1) {
                std::swap(rgb565_0, rgb565_1);
            }
        }

        GXColor c0 = ImageConvertRGB565::RGB565ToRGBA(rgb565_0);
        GXColor c1 = ImageConvertRGB565::RGB565ToRGBA(rgb565_1);
        bool punchThrough = (rgb565_0 <= rgb565_1);
        std::array<GXColor, 4> pal = {c0, c1};
        if (!punchThrough) {
            pal[2] = interpolateRGBA(c0, c1, 1, 3);
            pal[3] = interpolateRGBA(c0, c1, 2, 3);
        } else {
            pal[2] = interpolateRGBA(c0, c1, 1, 2);
            pal[3] = {0, 0, 0, 0};
        }

        u32 indices = 0;
        for (int i = 0; i < 16; i++) {
            int best = 0;
            int bestD = std::numeric_limits<int>::max();
            // Find the best palette value to assign to each pixel
            if (punchThrough && pixels[i].a < 128) {
                best = 3;
            } else {
                for (int k = 0; k < (punchThrough ? 3 : 4); k++) {
                    int d = dxt1distSq(pixels[i], pal[k]);
                    if (d < bestD) {
                        bestD = d;
                        best = k;
                    }
                }
            }
            indices = (indices << 2) | best;
        }
        outBlock.push_back(rgb565_0 >> 8);
        outBlock.push_back(rgb565_0 & 0xFF);
        outBlock.push_back(rgb565_1 >> 8);
        outBlock.push_back(rgb565_1 & 0xFF);
        outBlock.push_back(indices >> 24);
        outBlock.push_back((indices >> 16) & 0xFF);
        outBlock.push_back((indices >> 8) & 0xFF);
        outBlock.push_back(indices & 0xFF);
    }

    virtual std::vector<u8> packBlock(int blockX, int blockY) {
        std::vector<u8> block;
        for (int subY = 0; subY < 2; subY++) {
            for (int subX = 0; subX < 2; subX++) {
                std::vector<GXColor> pixels;
                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++) {
                        pixels.push_back(
                            readPixel(x + (subX * 4) + blockX, y + (subY * 4) + blockY));
                    }
                }

                // Check first if our original picture was dxt1 encoded,
                // This currently has an issue where it doesn't detect near-solid blocks, but we get
                // okay Results anyways from the actual dxt1 encode fallback
                if (isExactBlock(pixels, block)) {
                    continue;
                }

                // The source wasn't dxt1 encoded, use an algorithm to get it
                encodeDXT1(pixels, block);
            }
        }
        return block;
    }

    ImageConvertCMPR(const ResTIMG& header, const std::span<const u8>& buffer)
        : ImageConvertBase(header, buffer) {}
    ImageConvertCMPR(const ResTIMG& header, std::vector<GXColor>& rgba)
        : ImageConvertBase(header, rgba) {}
};

const std::vector<GXColor> timgToRGBA(const ResTIMG& header, const std::span<const u8>& buffer) {
    std::vector<GXColor> RGBA;
    switch (header.format) {
    case GX_TF_I4: {
        ImageConvertI4 unpacker(header, buffer);
        RGBA = unpacker.toRGBA();
        break;
    }
    case GX_TF_I8: {
        ImageConvertI8 unpacker(header, buffer);
        RGBA = unpacker.toRGBA();
        break;
    }
    case GX_TF_IA4: {
        ImageConvertIA4 unpacker(header, buffer);
        RGBA = unpacker.toRGBA();
        break;
    }
    case GX_TF_IA8: {
        ImageConvertIA8 unpacker(header, buffer);
        RGBA = unpacker.toRGBA();
        break;
    }
    case GX_TF_RGB565: {
        ImageConvertRGB565 unpacker(header, buffer);
        RGBA = unpacker.toRGBA();
        break;
    }
    case GX_TF_RGB5A3: {
        ImageConvertRGB5A3 unpacker(header, buffer);
        RGBA = unpacker.toRGBA();
        break;
    }
    case GX_TF_RGBA8: {
        ImageConvertRGBA8 unpacker(header, buffer);
        RGBA = unpacker.toRGBA();
        break;
    }
    case GX_TF_C4: {
        ImageConvertC4 unpacker(header, buffer);
        RGBA = unpacker.toRGBA();
        break;
    }
    case GX_TF_C8: {
        ImageConvertC8 unpacker(header, buffer);
        RGBA = unpacker.toRGBA();
        break;
    }
    case GX_TF_C14X2: {
        ImageConvertC14X2 unpacker(header, buffer);
        RGBA = unpacker.toRGBA();
        break;
    }
    case GX_TF_CMPR: {
        ImageConvertCMPR unpacker(header, buffer);
        RGBA = unpacker.toRGBA();
        break;
    }
    default:
        assert("Unkown Image Type!!");
    }
    return RGBA;
}

static std::string SerializeFormat(u8 format) {
    switch (format) {
    case GX_TF_I4:
        return "GX_TF_I4";
    case GX_TF_I8:
        return "GX_TF_I8";
    case GX_TF_IA4:
        return "GX_TF_IA4";
    case GX_TF_IA8:
        return "GX_TF_IA8";
    case GX_TF_RGB565:
        return "GX_TF_RGB565";
    case GX_TF_RGB5A3:
        return "GX_TF_RGB5A3";
    case GX_TF_RGBA8:
        return "GX_TF_RGBA8";
    case GX_TF_C4:
        return "GX_TF_C4";
    case GX_TF_C8:
        return "GX_TF_C8";
    case GX_TF_C14X2:
        return "GX_TF_C14X2";
    case GX_TF_CMPR:
        return "GX_TF_CMPR";
    }
    return "Unknown";
}

static u8 DeserializeFormat(const std::string& format) {
    if (format == "GX_TF_I4")
        return GX_TF_I4;
    if (format == "GX_TF_I8")
        return GX_TF_I8;
    if (format == "GX_TF_IA4")
        return GX_TF_IA4;
    if (format == "GX_TF_IA8")
        return GX_TF_IA8;
    if (format == "GX_TF_RGB565")
        return GX_TF_RGB565;
    if (format == "GX_TF_RGB5A3")
        return GX_TF_RGB5A3;
    if (format == "GX_TF_RGBA8")
        return GX_TF_RGBA8;
    if (format == "GX_TF_C4")
        return GX_TF_C4;
    if (format == "GX_TF_C8")
        return GX_TF_C8;
    if (format == "GX_TF_C14X2")
        return GX_TF_C14X2;
    if (format == "GX_TF_CMPR")
        return GX_TF_CMPR;
    return GX_TF_RGBA8;
}

static std::string SerializeWrapMode(u8 mode) {
    switch (mode) {
    case GX_CLAMP:
        return "GX_CLAMP";
    case GX_REPEAT:
        return "GX_REPEAT";
    case GX_MIRROR:
        return "GX_MIRROR";
    }
    return "Unknown";
}

static u8 DeserializeWrapMode(const std::string& mode) {
    if (mode == "GX_CLAMP")
        return GX_CLAMP;
    if (mode == "GX_REPEAT")
        return GX_REPEAT;
    if (mode == "GX_MIRROR")
        return GX_MIRROR;
    return GX_CLAMP;
}

static std::string SerializeFilter(u8 mode) {
    switch (mode) {
    case GX_NEAR:
        return "GX_NEAR";
    case GX_LINEAR:
        return "GX_LINEAR";
    case GX_NEAR_MIP_NEAR:
        return "GX_NEAR_MIP_NEAR";
    case GX_LIN_MIP_NEAR:
        return "GX_LIN_MIP_NEAR";
    case GX_NEAR_MIP_LIN:
        return "GX_NEAR_MIP_LIN";
    case GX_LIN_MIP_LIN:
        return "GX_LIN_MIP_LIN";
    }
    return "Unknown";
}

static u8 DeserializeFilter(const std::string& mode) {
    if (mode == "GX_NEAR")
        return GX_NEAR;
    if (mode == "GX_LINEAR")
        return GX_LINEAR;
    if (mode == "GX_NEAR_MIP_NEAR")
        return GX_NEAR_MIP_NEAR;
    if (mode == "GX_LIN_MIP_NEAR")
        return GX_LIN_MIP_NEAR;
    if (mode == "GX_NEAR_MIP_LIN")
        return GX_NEAR_MIP_LIN;
    if (mode == "GX_LIN_MIP_LIN")
        return GX_LIN_MIP_LIN;
    return GX_LINEAR;
}

static std::string SerializeColorFormat(u8 format) {
    switch (format) {
    case GX_TL_IA8:
        return "GX_TL_IA8";
    case GX_TL_RGB565:
        return "GX_TL_RGB565";
    case GX_TL_RGB5A3:
        return "GX_TL_RGB5A3";
    }
    return "Unknown";
}

static u8 DeserializeColorFormat(const std::string& format) {
    if (format == "GX_TL_IA8")
        return GX_TL_IA8;
    if (format == "GX_TL_RGB565")
        return GX_TL_RGB565;
    if (format == "GX_TL_RGB5A3")
        return GX_TL_RGB5A3;
    return GX_TL_RGB5A3;
}

const std::filesystem::path bti_unpack(
    const std::filesystem::path& outputName, const std::span<const u8>& buffer) {
    const ResTIMG& header = *(const ResTIMG*)buffer.data();

    const std::vector<GXColor> RGBA = timgToRGBA(header, buffer);

    std::filesystem::create_directories(outputName);
    std::filesystem::path pngName = outputName / (outputName.stem().generic_string() + ".png");
    std::filesystem::path jsonName = outputName / (outputName.stem().generic_string() + ".json");
    writePNG_RGBA8(pngName, header.width, header.height, RGBA);

    nlohmann::ordered_json btiJson = {{"tool_version", 1},
        {"format", SerializeFormat(header.format)}, {"wrapS", SerializeWrapMode(header.wrapS)},
        {"wrapT", SerializeWrapMode(header.wrapT)}, {"doEdgeLOD", (bool)header.doEdgeLOD},
        {"biasClamp", header.biasClamp}, {"maxAnisotropy", header.maxAnisotropy},
        {"minFilter", SerializeFilter(header.minFilter)},
        {"magFilter", SerializeFilter(header.magFilter)}, {"minLOD", header.minLOD},
        {"maxLOD", header.maxLOD}, {"mipmapCount", header.mipmapCount},
        {"LODBias", header.LODBias.host()}};

    if (header.indexTexture) {
        btiJson["paletteColorFormat"] = SerializeColorFormat(header.colorFormat);
    }

    auto json_fs = dusk::io::FileStream::Create(jsonName);
    fs_writeString(json_fs, btiJson.dump(4));

    return outputName;
}

std::vector<u8> RGBAtoTimg(ResTIMG& header, std::vector<GXColor> rgba) {
    std::vector<u8> timg;
    bool hasAlpha = false;
    switch (header.format) {
    case GX_TF_I4: {
        ImageConvertI4 packer(header, rgba);
        timg = packer.toTIMG();
        hasAlpha = packer.mHasAlpha;
        break;
    }
    case GX_TF_I8: {
        ImageConvertI8 packer(header, rgba);
        timg = packer.toTIMG();
        hasAlpha = packer.mHasAlpha;
        break;
    }
    case GX_TF_IA4: {
        ImageConvertIA4 packer(header, rgba);
        timg = packer.toTIMG();
        hasAlpha = packer.mHasAlpha;
        break;
    }
    case GX_TF_IA8: {
        ImageConvertIA8 packer(header, rgba);
        timg = packer.toTIMG();
        hasAlpha = packer.mHasAlpha;
        break;
    }
    case GX_TF_RGB565: {
        ImageConvertRGB565 packer(header, rgba);
        timg = packer.toTIMG();
        hasAlpha = packer.mHasAlpha;
        break;
    }
    case GX_TF_RGB5A3: {
        ImageConvertRGB5A3 packer(header, rgba);
        timg = packer.toTIMG();
        hasAlpha = packer.mHasAlpha;
        break;
    }
    case GX_TF_RGBA8: {
        ImageConvertRGBA8 packer(header, rgba);
        timg = packer.toTIMG();
        hasAlpha = packer.mHasAlpha;
        break;
    }
    case GX_TF_C4: {
        ImageConvertC4 packer(header, rgba);
        timg = packer.toTIMG();
        hasAlpha = packer.mHasAlpha;
        header.numColors = packer.mNumColors;
        header.paletteOffset = packer.mPaletteOffset;
        break;
    }
    case GX_TF_C8: {
        ImageConvertC8 packer(header, rgba);
        timg = packer.toTIMG();
        hasAlpha = packer.mHasAlpha;
        header.numColors = packer.mNumColors;
        header.paletteOffset = packer.mPaletteOffset;
    } break;
    case GX_TF_C14X2: {
        ImageConvertC14X2 packer(header, rgba);
        timg = packer.toTIMG();
        hasAlpha = packer.mHasAlpha;
        header.numColors = packer.mNumColors;
        header.paletteOffset = packer.mPaletteOffset;
        break;
    }
    case GX_TF_CMPR: {
        ImageConvertCMPR packer(header, rgba);
        timg = packer.toTIMG();
        hasAlpha = packer.mHasAlpha;
        break;
    }
    default:
        break;
    }

    if (hasAlpha) {
        header.alphaEnabled = 2;
    }

    return timg;
}

const std::vector<u8> bti_pack(const std::filesystem::path& source) {
    // Get the two files within the bti dir
    const auto pngPath = source / (source.stem().generic_string() + ".png");
    const auto jsonPath = source / (source.stem().generic_string() + ".json");

    u32 width, height;
    std::vector<GXColor> rgba;

    // Convert the PNG to RGBA
    readPNG_RGBA8(pngPath, width, height, rgba);

    // Get the data from the json
    std::ifstream btiJsonFile(jsonPath);
    if (!btiJsonFile.is_open()) {
        throw std::runtime_error(std::string("Could not open ") + jsonPath.generic_string());
    }
    auto j = nlohmann::json::parse(btiJsonFile);

    std::vector<u8> data(sizeof(ResTIMG));
    ResTIMG& header = *(ResTIMG*)data.data();
    header = {};
    header.format = DeserializeFormat(j["format"]);
    header.width = width;
    header.height = height;
    header.wrapS = DeserializeWrapMode(j["wrapS"]);
    header.wrapT = DeserializeWrapMode(j["wrapT"]);
    header.indexTexture =
        (header.format == GX_TF_C4 || header.format == GX_TF_C8 || header.format == GX_TF_C14X2);
    if (header.indexTexture) {
        header.colorFormat = DeserializeColorFormat(j["paletteColorFormat"]);
    }
    header.mipmapCount = j["mipmapCount"];
    if (header.mipmapCount > 1) {
        header.mipmapEnabled = 1;
    }
    header.doEdgeLOD = j["doEdgeLOD"];
    header.biasClamp = j["biasClamp"];
    header.maxAnisotropy = j["maxAnisotropy"];
    header.minFilter = DeserializeFilter(j["minFilter"]);
    header.magFilter = DeserializeFilter(j["magFilter"]);
    header.minLOD = j["minLOD"];
    header.maxLOD = j["maxLOD"];
    header.LODBias = (s16)j["LODBias"];
    header.imageOffset = sizeof(ResTIMG);

    const auto timg = RGBAtoTimg(header, rgba);

    // Append the timg data to the end of the header
    data.insert(data.end(), timg.begin(), timg.end());

    return data;
}

GXColor rgb565_to_rgba(u16 rgb565) {
    return ImageConvertRGB565::RGB565ToRGBA(rgb565);
}

};  // namespace assets
