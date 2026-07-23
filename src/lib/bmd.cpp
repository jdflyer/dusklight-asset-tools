#include "bmd.hpp"
#include <aurora/dl.hpp>
#include <nlohmann/json.hpp>
#include "JSystem/J3DGraphLoader/J3DModelLoader.h"
#include "JSystem/J3DGraphLoader/J3DShapeFactory.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JSupport/JSupport.h"
#include "sjis.hpp"
#include "timg.hpp"
#include "dusk/io.hpp"
#include "assets.hpp"

namespace assets {

struct BMDUnpacker {
    const std::span<const u8>& mBuffer;
    const std::filesystem::path& mPath;

    u16 mFlags;
    enum MTXCalc { BASIC, MAYA, SOFTIMAGE };
    MTXCalc mMtxCalc;
    const J3DModelHierarchy* mHierarchy;
    u32 mVtxNum;
    u32 mPacketNum;

    std::vector<GXVtxAttrFmtList> mVtxAttrList;
    struct VtxAttr {
        u32 mSourceNum;
        GXVtxAttrFmtList fmt;
        u32 mElementCountPerVertex;
        u32 mElementStridePerVertex;
        u32 mFullStridePerVertex;
        std::vector<u8> mSourceBuffer;

        std::vector<u8> mOutBuffer;

        u32 mAccessorIdx;  // Gets set after the accessor gets created
    };
    std::unordered_map<GXAttr, VtxAttr> mVtxAttrMap;
    u32 mIndiciesPos;  // Gets incremented as values get added to each VtxAttr's outBuffer

    std::vector<u8> mOutBuffer;
    nlohmann::ordered_json mBufferViews;
    nlohmann::ordered_json mAccessors;

    struct Primitive {
        std::vector<u32> mIndexBuffer;
        std::vector<GXAttr> mAttrs;
        u32 mGLTFPrimitive;  // 4 = TRIANGLES, 5 = TRIANGLE_STRIP, 6 = TRIANGLE_FAN
    };

    struct Shape {
        u32 idx;
        std::string name;
        std::vector<Primitive> mPrimitives;
        Vec mMin;
        Vec mMax;
    };
    std::vector<Shape> mShapes;
    std::vector<GXVtxDescList> mVtxDescList;

    nlohmann::ordered_json mNodesInScene;
    nlohmann::ordered_json mNodesInNodes;
    nlohmann::ordered_json mNodesInMeshes;

    struct TextureData {
        std::string mName;
        intptr_t mTextureHeaderOffset;
    };
    std::vector<TextureData> mTextureData;

    nlohmann::ordered_json gltf;

    static constexpr GXAttr VertexBlockAttrOrder[13] = {
        GX_VA_POS,
        GX_VA_NRM,
        GX_VA_NBT,
        GX_VA_CLR0,
        GX_VA_CLR1,
        GX_VA_TEX0,
        GX_VA_TEX1,
        GX_VA_TEX2,
        GX_VA_TEX3,
        GX_VA_TEX4,
        GX_VA_TEX5,
        GX_VA_TEX6,
        GX_VA_TEX7,
    };

    BMDUnpacker(const std::filesystem::path& path, const std::span<const u8>& buffer)
        : mPath(path), mBuffer(buffer) {}
    ~BMDUnpacker() {}

    static GXVtxAttrFmtList getFmt(GXVtxAttrFmtList* i_fmtList, GXAttr i_attr) {
        for (; i_fmtList->attr != GX_VA_NULL; i_fmtList++) {
            if (i_fmtList->attr == i_attr) {
                return *i_fmtList;
            }
        }

        OSPanic(__FILE__, __LINE__, "Unable to find vertex attribute format!");
    }

    static GXCompType getFmtType(GXVtxAttrFmtList* i_fmtList, GXAttr i_attr) {
        for (; i_fmtList->attr != GX_VA_NULL; i_fmtList++) {
            if (i_fmtList->attr == i_attr) {
                return i_fmtList->type;
            }
        }
        return GX_F32;
    }

    void readInformation(J3DModelInfoBlock const* i_block) {
        mFlags = i_block->mFlags;

        switch (mFlags & 0xf) {
        case 0:
            mMtxCalc = BASIC;
            break;
        case 1:
            mMtxCalc = SOFTIMAGE;
            break;
        case 2:
            mMtxCalc = MAYA;
            break;
        default:
            break;
        }

        mPacketNum = i_block->mPacketNum;
        mVtxNum = i_block->mVtxNum;
        mHierarchy = JSUConvertOffsetToPtr<J3DModelHierarchy>(i_block, i_block->mpHierarchy);
    }

    u32 insertAccessor(
        const std::vector<u8>& buffer, u32 count, u16 componentType, const std::string& type) {
        u32 accessor = mAccessors.size();
        mAccessors.push_back({{"bufferView", mBufferViews.size()}, {"componentType", componentType},
            {"count", count}, {"type", type}});
        mBufferViews.push_back(
            {{"buffer", 0}, {"byteOffset", mOutBuffer.size()}, {"byteLength", buffer.size()}});
        mOutBuffer.insert(mOutBuffer.end(), buffer.begin(), buffer.end());
        return accessor;
    }

    GXVtxAttrFmtList getFmt(const std::vector<GXVtxAttrFmtList>& i_fmtList, GXAttr i_attr) {
        for (const auto& fmt : i_fmtList) {
            if (fmt.attr == i_attr) {
                return fmt;
            }
        }
        return {};
    }

    u8 getStride(GXAttr attr, GXCompType type) {
        if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
            switch (type) {
            case GX_RGBA8:
            case GX_RGBX8:
                return 4;
            case GX_RGB8:
            case GX_RGBA6:
                return 3;
            case GX_RGB565:
            case GX_RGBA4:
                return 2;
            }
        } else {
            switch (type) {
            case GX_F32:
                return 4;
            case GX_U16:
            case GX_S16:
                return 2;
            case GX_U8:
            case GX_S8:
                return 1;
            }
        }
        return 1;
    }

    const std::string_view getGLTFAttrName(GXAttr attr) {
        switch (attr) {
        case GX_VA_POS:
            return "POSITION";
        case GX_VA_NRM:
            return "NORMAL";
        case GX_VA_NBT:
            return "TANGENT";
        case GX_VA_CLR0:
            return "COLOR_0";
        case GX_VA_CLR1:
            return "COLOR_1";
        case GX_VA_TEX0:
            return "TEXCOORD_0";
        case GX_VA_TEX1:
            return "TEXCOORD_1";
        case GX_VA_TEX2:
            return "TEXCOORD_2";
        case GX_VA_TEX3:
            return "TEXCOORD_3";
        case GX_VA_TEX4:
            return "TEXCOORD_4";
        case GX_VA_TEX5:
            return "TEXCOORD_5";
        case GX_VA_TEX6:
            return "TEXCOORD_6";
        case GX_VA_TEX7:
            return "TEXCOORD_7";
        }
        return "";
    }

    u16 getGLTFComponentType(GXCompType type) {
        switch (type) {
        case GX_U8:
            return 5121;
        case GX_S8:
            return 5120;
        case GX_U16:
            return 5123;
        case GX_S16:
            return 5122;
        case GX_F32:
        default:
            return 5126;
        }
    }

    u8 getCnt(GXAttr attr, GXCompCnt cnt) {
        if (attr == GX_VA_POS) {
            return cnt == GX_POS_XY ? 2 : 3;
        } else if (attr == GX_VA_NRM) {
            return cnt == GX_NRM_XYZ ? 3 : 9;  // NBT3 is lies
        } else if (attr >= GX_VA_CLR0 && attr <= GX_VA_CLR1) {
            return cnt == GX_CLR_RGB ? 3 : 4;  // clr is special anyway
        } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
            return cnt == GX_TEX_S ? 1 : 2;
        }
        return 1;
    }

    std::string getGLTFType(u8 cnt) {
        switch (cnt) {
        case 1:
            return "SCALAR";
        case 2:
            return "VEC2";
        case 3:
            return "VEC3";
        case 4:
            return "VEC4";
        case 9:
            return "MAT3";
        case 16:
            return "MAT4";
        default:
            return "SCALAR";
        }
    }

    std::string getMTXGLTFType(u8 cnt) {
        std::string type = getGLTFType(cnt);
        if (type == "VEC4") {
            return "MAT2";
        }
        return type;
    }

    template <typename T>
    static void FixArrayEndian(void* arrayStart, void* arrayEnd) {
#if TARGET_LITTLE_ENDIAN
        u32 itemCount = ((u8*)arrayEnd - (u8*)arrayStart) / sizeof(T);
        be_swap((T*)arrayStart, itemCount);
#endif
    }

    static void FixArrayEndian_24(void* arrayStart, void* arrayEnd) {
#if TARGET_LITTLE_ENDIAN
        for (u8* work = (u8*)arrayStart; work != (u8*)arrayEnd; work += 3)
            std::swap(work[0], work[2]);
#endif
    }

    static void FixArrayEndian(void* arrayStart, void* arrayEnd, u32 stride) {
        switch (stride) {
        case 1:
            // Nothing needs to happen here!
            break;
        case 2:
            FixArrayEndian<u16>(arrayStart, arrayEnd);
            break;
        case 3:
            FixArrayEndian_24(arrayStart, arrayEnd);
            break;
        case 4:
            FixArrayEndian<u32>(arrayStart, arrayEnd);
            break;
        default:
            OSPanic(__FILE__, __LINE__, "Unknown component type?");
        }
    }

    void readVertex(J3DVertexBlock const* i_block) {
        for (const GXVtxAttrFmtList* attrFmt =
                 JSUConvertOffsetToPtr<GXVtxAttrFmtList>(i_block, i_block->mpVtxAttrFmtList);
            ; attrFmt++)
        {
            mVtxAttrList.push_back(BE<GXVtxAttrFmtList>::swap(*attrFmt));

            if (attrFmt->attr == GX_VA_NULL) {
                break;
            }
        }

        const BE(u32)* attrPtrBase = &i_block->mpVtxPosArray;
        for (int i = 0; i < ARRAY_SIZEU(VertexBlockAttrOrder); i++) {
            GXAttr attr = VertexBlockAttrOrder[i];

            if (attrPtrBase[i] == 0) {
                continue;
            }

            GXVtxAttrFmtList fmt = getFmt(mVtxAttrList, attr);

            u32 startOffset = attrPtrBase[i];
            u32 endOffset = i_block->mBlockSize;
            if (i + 1 < ARRAY_SIZE(VertexBlockAttrOrder)) {
                for (int j = i + 1; j < ARRAY_SIZE(VertexBlockAttrOrder); j++) {
                    if (attrPtrBase[j] == 0) {
                        continue;
                    }
                    endOffset = attrPtrBase[j];
                    break;
                }
            }

            u8 compStride = getStride(attr, fmt.type);
            u8 compCnt = getCnt(attr, fmt.cnt);
            u32 vertStride = compStride * compCnt;

            u32 byteSize = endOffset - startOffset;
            u32 num = byteSize / vertStride;

            std::vector<u8> buffer(byteSize);
            memcpy(buffer.data(), JSUConvertOffsetToPtr<u8>(i_block, startOffset), byteSize);
            if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
                // Convert colors to rgba float vec4
                std::vector<float> colorBuffer;
                for (int i = 0; i < buffer.size(); i += compStride) {
                    GXColor color = {};
                    switch (fmt.type) {
                    case GX_RGB565:
                        color = rgb565_to_rgba((buffer[i] << 8) | buffer[i + 1]);
                        break;
                    case GX_RGB8:
                        color = {buffer[i], buffer[i + 1], buffer[i + 2], 255};
                        break;
                    case GX_RGBX8:
                        color = {buffer[i], buffer[i + 1], buffer[i + 2], 255};
                        break;
                    case GX_RGBA4:
                        color = {(u8)(buffer[i] >> 4), (u8)(buffer[i] & 0xF),
                            (u8)(buffer[i + 1] >> 4), (u8)(buffer[i] & 0xF)};
                        color = {ExpandTo8<4>(color.r), ExpandTo8<4>(color.g),
                            ExpandTo8<4>(color.b), ExpandTo8<4>(color.a)};
                        break;
                    case GX_RGBA6:
                        color = {(u8)(buffer[i] >> 2),
                            (u8)(((buffer[i] & 3) << 4) | ((buffer[i + 2] & 0xF0) >> 4)),
                            (u8)(((buffer[i + 2] & 0xF) << 2) | ((buffer[i + 3] & 0xC0) >> 6)),
                            (u8)(buffer[i + 3] & 0x3F)};
                        color = {ExpandTo8<6>(color.r), ExpandTo8<6>(color.g),
                            ExpandTo8<6>(color.b), ExpandTo8<6>(color.a)};
                    case GX_RGBA8:
                        color = {buffer[i], buffer[i + 1], buffer[i + 2], buffer[i + 3]};
                        break;
                    }
                    colorBuffer.push_back(color.r / 255.0f);
                    colorBuffer.push_back(color.g / 255.0f);
                    colorBuffer.push_back(color.b / 255.0f);
                    if (compCnt == 4) {
                        colorBuffer.push_back(color.a / 255.0f);
                    }
                }
                compCnt = 4;
                compStride = sizeof(float);
                vertStride = compCnt * compStride;
                fmt.type = GX_F32;
                buffer.resize(colorBuffer.size() * sizeof(float));
                memcpy(buffer.data(), colorBuffer.data(), colorBuffer.size());
            } else if (fmt.type != GX_F32) {
                // Convert these to float values, gltf will like them better
                std::vector<float> floatBuffer;
                floatBuffer.reserve(byteSize / compStride);
                for (int i = 0; i < buffer.size(); i += compStride) {
                    float val = 0.0f;
                    switch (fmt.type) {
                    case GX_U8:
                        val = (u8)buffer[i];
                        break;
                    case GX_S8:
                        val = (s8)buffer[i];
                        break;
                    case GX_U16:
                        val = (u16)(buffer[i] << 8 | buffer[i + 1]);
                        break;
                    case GX_S16:
                        val = (s16)(buffer[i] << 8 | buffer[i + 1]);
                        break;
                    }
                    floatBuffer.push_back(val);
                }
                fmt.type = GX_F32;
                compStride = sizeof(float);
                vertStride = compCnt * compStride;
                buffer.resize(floatBuffer.size() * sizeof(float));
                memcpy(buffer.data(), floatBuffer.data(), floatBuffer.size() * sizeof(float));
                // FixArrayEndian(buffer.data(), buffer.data() + buffer.size(), compStride);
            }
            mVtxAttrMap[attr] = {
                num, fmt, compCnt, compStride, vertStride, std::move(buffer), std::vector<u8>()};
        }
    }

    std::vector<Primitive> parseDList(u8* dlist, u32 size) {
        std::vector<Primitive> primitives;
        // Use only one primitive for and convert trianglestrips and trianglefans to normal
        // triangles (saves on space in the gltf file)
        Primitive primitive;
        for (const u8* dl = dlist; (dl - dlist) < size;) {
            u8 cmd = *(u8*)dl;
            dl++;
            if (cmd != GX_TRIANGLEFAN && cmd != GX_TRIANGLESTRIP)
                break;
            int vtxNum = be16(*((u16*)(dl)));
            dl += 2;

            u8 gltfMode = 4;  // Normal Triangles
            // Force all primitives
            // switch (cmd) {
            // case GX_TRIANGLESTRIP:
            //     gltfMode = 5;
            //     break;
            // case GX_TRIANGLEFAN:
            //     gltfMode = 6;
            //     break;
            // }

            std::vector<u32> srcIndices;
            for (int i = 0; i < vtxNum; i++) {
                for (const auto& desc : mVtxDescList) {
                    if (desc.attr == GX_VA_NULL) {
                        break;
                    }
                    primitive.mAttrs.push_back(desc.attr);
                    auto& attr = mVtxAttrMap.at(desc.attr);
                    u32 index = 0;
                    u32 length = 0;
                    const u8* vtxDataSource = nullptr;
                    switch (desc.type) {
                    case GX_NONE:
                        break;
                    case GX_INDEX8:
                        length = 1;
                        index = *dl;
                        vtxDataSource =
                            attr.mSourceBuffer.data() + (attr.mFullStridePerVertex * index);
                        break;
                    case GX_INDEX16:
                        length = 2;
                        index = be16(*((u16*)(dl)));
                        vtxDataSource =
                            attr.mSourceBuffer.data() + (attr.mFullStridePerVertex * index);
                        break;
                    case GX_DIRECT: {
                        length = attr.mFullStridePerVertex;
                        vtxDataSource = dl;
                        break;
                    }
                    }
                    dl += length;

                    // Copy the whole vertex into the outBuffer from the source index
                    u32 offset = attr.mOutBuffer.size();
                    attr.mOutBuffer.resize(attr.mOutBuffer.size() + attr.mFullStridePerVertex);
                    memcpy(
                        attr.mOutBuffer.data() + offset, vtxDataSource, attr.mFullStridePerVertex);
                }
                srcIndices.push_back(mIndiciesPos);
                mIndiciesPos += 1;
            }

            // Convert triangle strips and fans to normal triangles (gltf likes it better)
            std::vector<u32> indices;
            switch (cmd) {
            case GX_TRIANGLESTRIP:
                for (int i = 0; i < vtxNum - 2; i++) {
                    u32 a, b, c;
                    if (i % 2 == 0) {
                        a = srcIndices[i];
                        b = srcIndices[i + 1];
                        c = srcIndices[i + 2];
                    } else {
                        a = srcIndices[i + 1];
                        b = srcIndices[i];
                        c = srcIndices[i + 2];
                    }
                    if (a == b || b == c || a == c) {
                        continue;
                    }
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(c);
                }
                break;
            case GX_TRIANGLEFAN: {
                u32 a = srcIndices[0];
                for (int i = 1; i < vtxNum - 1; i++) {
                    u32 b = srcIndices[i];
                    u32 c = srcIndices[i + 1];
                    if (a == b || b == c || a == c) {
                        continue;
                    }
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(c);
                }
                break;
            }
            }

            for (const auto& index : indices) {
                primitive.mIndexBuffer.push_back(index);
            }
            primitive.mGLTFPrimitive = gltfMode;
        }
        primitives.emplace_back(primitive);
        return primitives;
    }

    void readShape(J3DShapeBlock const* i_block) {
        J3DShapeInitData* shapeInitTable =
            JSUConvertOffsetToPtr<J3DShapeInitData>(i_block, (uintptr_t)i_block->mpShapeInitData);
        BE(u16)* indexTable =
            JSUConvertOffsetToPtr<BE(u16)>(i_block, (uintptr_t)i_block->mpIndexTable);
        GXVtxDescList* vtxDescList =
            JSUConvertOffsetToPtr<GXVtxDescList>(i_block, (uintptr_t)i_block->mpVtxDescList);
        BE(u16)* mtxTable = JSUConvertOffsetToPtr<BE(u16)>(i_block, (uintptr_t)i_block->mpMtxTable);
        u8* displayListData =
            JSUConvertOffsetToPtr<u8>(i_block, (uintptr_t)i_block->mpDisplayListData);
        J3DShapeMtxInitData* mtxInitData =
            JSUConvertOffsetToPtr<J3DShapeMtxInitData>(i_block, (uintptr_t)i_block->mpMtxInitData);
        J3DShapeDrawInitData* drawInitTable = JSUConvertOffsetToPtr<J3DShapeDrawInitData>(
            i_block, (uintptr_t)i_block->mpDrawInitData);

#if TARGET_LITTLE_ENDIAN
        // mVtxDescList is in big endian, swap to little endian.
        int maxVtxDescListStart = 0;
        for (int shapeIdx = 0; shapeIdx < i_block->mShapeNum; shapeIdx++) {
            u16 thisIndex = shapeInitTable[indexTable[shapeIdx]].mVtxDescListIndex;
            maxVtxDescListStart =
                std::max(maxVtxDescListStart, (int)(thisIndex / sizeof(GXVtxDescList)));
        }

        GXVtxDescList* lastEntry = vtxDescList + maxVtxDescListStart;
        while (lastEntry->attr != BE(GXAttr)::swap(GX_VA_NULL)) {
            lastEntry++;
        }

        for (GXVtxDescList* entry = vtxDescList; entry <= lastEntry; entry++) {
            mVtxDescList.push_back(BE(GXVtxDescList)::swap(*entry));
        }
#endif

        std::vector<std::string> shapeNames;
        if (i_block->mpNameTable != (uintptr_t)NULL) {
            ResNTAB* names = JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable);
            for (int i = 0; i < names->mEntryNum; i++) {
                std::string name = sjis_to_utf8(std::string(names->getName(i)));
                shapeNames.push_back(name);
            }
        }

        for (int shapeNo = 0; shapeNo < i_block->mShapeNum; shapeNo++) {
            const J3DShapeInitData& shapeInitData = shapeInitTable[indexTable[shapeNo]];
            std::string name = shapeNames.size() > 0 ?
                                   shapeNames[shapeNo] :
                                   mPath.stem().string() + "_" + std::to_string(shapeNo);

            Shape shape;
            shape.idx = shapeNo;
            shape.name = name;
            shape.mMin = shapeInitData.mMin;
            shape.mMax = shapeInitData.mMax;

            std::vector<Primitive> fullPrimitives;
            for (int mtxNo = 0; mtxNo < shapeInitData.mMtxGroupNum; mtxNo++) {
                const J3DShapeDrawInitData& drawInitData =
                    (&drawInitTable[shapeInitData.mDrawInitDataIndex])[mtxNo];
                u8* dlist = &displayListData[drawInitData.mDisplayListIndex];

                std::vector<Primitive> primitives =
                    parseDList(dlist, drawInitData.mDisplayListSize);
                fullPrimitives.insert(fullPrimitives.end(), primitives.begin(), primitives.end());
            }
            shape.mPrimitives = std::move(fullPrimitives);
            mShapes.emplace_back(shape);
        }
    }

    void readTexture(J3DTextureBlock const* i_block) {
        u16 texture_num = i_block->mTextureNum;
        ResTIMG* texture_res = JSUConvertOffsetToPtr<ResTIMG>(i_block, i_block->mpTextureRes);
        ResNTAB* names = NULL;
        if (i_block->mpNameTable != (uintptr_t)NULL) {
            names = JSUConvertOffsetToPtr<ResNTAB>(i_block, i_block->mpNameTable);
        }

        for (int i = 0; i < texture_num; i++) {
            std::string name;
            if (names) {
                std::string nameSJIS = std::string(names->getName(i));
                name = sjis_to_utf8(nameSJIS);
            }
            mTextureData.push_back({name, (u8*)&texture_res[i] - mBuffer.data()});
        }
    }

    void unpack() {
        J3DModelFileData const* data = (J3DModelFileData*)mBuffer.data();
        J3DModelBlock const* block = data->mBlocks;
        for (u32 block_no = 0; block_no < data->mBlockNum; block_no++) {
            switch (block->mBlockType) {
            case 'INF1':
                readInformation((J3DModelInfoBlock*)block);
                break;
            case 'VTX1':
                readVertex((J3DVertexBlock*)block);
                break;
            case 'EVP1':
                // readEnvelop((J3DEnvelopeBlock*)block);
                break;
            case 'DRW1':
                // readDraw((J3DDrawBlock*)block);
                break;
            case 'JNT1':
                // readJoint((J3DJointBlock*)block);
                break;
            case 'MAT3':
                // readMaterial((J3DMaterialBlock*)block, (s32)i_flags);
                break;
            case 'MAT2':
                // readMaterial_v21((J3DMaterialBlock_v21*)block, (s32)i_flags);
                break;
            case 'SHP1':
                readShape((J3DShapeBlock*)block);
                break;
            case 'TEX1':
                readTexture((J3DTextureBlock*)block);
                break;
            default:
                OSReport("Unknown data block\n");
                break;
            }
            block = (J3DModelBlock*)((uintptr_t)block + block->mBlockSize);
        }

        for (int i = 0; i < mTextureData.size(); i++) {
            intptr_t offset = mTextureData[i].mTextureHeaderOffset;
            ResTIMG* timg = JSUConvertOffsetToPtr<ResTIMG>(mBuffer.data(), offset);

            const std::vector<GXColor> rgba = timgToRGBA(*timg, mBuffer.subspan(offset));
            writePNG_RGBA8(
                mPath / (mTextureData[i].mName + ".png"), timg->width, timg->height, rgba);
        }

        std::string pathStem = mPath.stem().string();

        gltf["asset"] = {{"version", "2.0"}, {"generator", "Dusklight bmd to gltf converter"}};
        gltf["scene"] = 0;

        // for (int i = 0; i < mShapeTable.mShapeNodes.size(); i++) {
        //     std::string name = mShapeTable.mShapeNames.size() > 0 ?
        //                            mShapeTable.mShapeNames[i] :
        //                            pathStem + "_" + std::to_string(i);
        //     nodes_in_scene.push_back(i);
        //     nodes_in_nodes.push_back({
        //         {"mesh", i},
        //         {"name", name},
        //     });
        //     nlohmann::ordered_json primitives = {};
        //     for (int j = 0; j < mShapeTable.mShapeNodes[i].mDrawCmds.size(); j++) {
        //         const auto& cmds = mShapeTable.mShapeNodes[i].mDrawCmds[j];
        //         for (int k = 0; k < cmds.size(); k++) {
        //             const auto& cmd = cmds[k];
        //             u8 mode = 4;
        //             switch (cmd.prim) {
        //             case GX_POINTS:
        //                 mode = 0;
        //                 break;
        //             case GX_LINES:
        //                 mode = 1;
        //                 break;
        //             case GX_LINESTRIP:
        //                 mode = 3;
        //                 break;
        //             case GX_TRIANGLES:
        //                 mode = 4;
        //                 break;
        //             case GX_TRIANGLESTRIP:
        //                 mode = 5;
        //                 break;
        //             case GX_TRIANGLEFAN:
        //                 mode = 6;
        //                 break;
        //             }
        //             size_t indexByteLength = cmd.vtxCount * 2;
        //             primitives.push_back({{"attributes", {{"POSITION", vtxBufferAccessor}}},
        //                 {"indices", accessors.size()}, {"mode", mode}});
        //             accessors.push_back({{"bufferView", bufferViews.size()},
        //                 {"componentType", 5123}, {"count", cmd.vtxCount}, {"type",
        //                 "SCALAR"}});
        //             bufferViews.push_back({{"buffer", 0}, {"byteOffset", outBuffer.size()},
        //                 {"byteLength", indexByteLength}});
        //             u32 offset = outBuffer.size();
        //             outBuffer.resize(offset + indexByteLength);
        //             memcpy(outBuffer.data() + offset, cmd.vertices, indexByteLength);
        //         }
        //     }
        //     nodes_in_meshes.push_back({{"primitives", primitives}});
        // }

        for (int i = 0; i < ARRAY_SIZEU(VertexBlockAttrOrder); i++) {
            GXAttr attr = VertexBlockAttrOrder[i];
            if (mVtxAttrMap.find(attr) == mVtxAttrMap.end()) {
                continue;
            }
            auto& vtxAttrListEntry = mVtxAttrMap.at(attr);
            vtxAttrListEntry.mAccessorIdx = mAccessors.size();

            u32 vertexDataSize = vtxAttrListEntry.mOutBuffer.size();
            mAccessors.push_back({{"bufferView", mBufferViews.size()},
                {"componentType", getGLTFComponentType(vtxAttrListEntry.fmt.type)},
                {"count", mIndiciesPos},
                {"type", getGLTFType(getCnt(attr, vtxAttrListEntry.fmt.cnt))}});
            if (attr == GX_VA_POS) {
                // Calculate the min/max for the whole model
                Vec min = {0.0f, 0.0f, 0.0f};
                Vec max = {0.0f, 0.0f, 0.0f};
                for (const auto& shape : mShapes) {
                    min.x = std::min(min.x, shape.mMin.x);
                    min.y = std::min(min.y, shape.mMin.y);
                    min.z = std::min(min.z, shape.mMin.z);

                    max.x = std::max(max.x, shape.mMax.x);
                    max.y = std::max(max.y, shape.mMax.y);
                    max.z = std::max(max.z, shape.mMax.z);
                }
                mAccessors[mAccessors.size() - 1]["min"] = {min.x, min.y, min.z};
                mAccessors[mAccessors.size() - 1]["max"] = {max.x, max.y, max.z};
            }
            mBufferViews.push_back({{"buffer", 0}, {"byteOffset", mOutBuffer.size()},
                {"byteLength", vertexDataSize}, {"target", 34962}});
            u32 offset = mOutBuffer.size();
            mOutBuffer.resize(offset + vertexDataSize);
            memcpy(mOutBuffer.data() + offset, vtxAttrListEntry.mOutBuffer.data(), vertexDataSize);
        }

        for (const auto& shape : mShapes) {
            mNodesInScene.push_back(shape.idx);
            mNodesInNodes.push_back({{"mesh", shape.idx}, {"name", shape.name}});

            nlohmann::ordered_json primitives;
            for (const auto& primitive : shape.mPrimitives) {
                nlohmann::ordered_json attributes;
                u32 count = 0;
                for (const auto& attr : primitive.mAttrs) {
                    const auto& vtxAttrListEntry = mVtxAttrMap.at(attr);
                    attributes[getGLTFAttrName(attr)] = vtxAttrListEntry.mAccessorIdx;
                }
                u32 indexBufferSize = primitive.mIndexBuffer.size() * sizeof(u32);
                primitives.push_back({{"attributes", attributes}, {"indices", mAccessors.size()},
                    {"mode", primitive.mGLTFPrimitive}});
                mAccessors.push_back({{"bufferView", mBufferViews.size()}, {"componentType", 5125},
                    {"count", primitive.mIndexBuffer.size()}, {"type", "SCALAR"}});
                mBufferViews.push_back({{"buffer", 0}, {"byteOffset", mOutBuffer.size()},
                    {"byteLength", indexBufferSize}, {"target", 34963}});
                u32 offset = mOutBuffer.size();
                mOutBuffer.resize(offset + indexBufferSize);
                memcpy(mOutBuffer.data() + offset, primitive.mIndexBuffer.data(), indexBufferSize);
            }

            mNodesInMeshes.push_back({{"primitives", primitives}});
        }

        gltf["scenes"] = {{{"nodes", mNodesInScene}}};
        gltf["nodes"] = mNodesInNodes;
        gltf["meshes"] = mNodesInMeshes;
        // gltf["meshes"] = {{{"primitives", {{{"attributes", {{"POSITION", 0}}}}}}}};

        std::string outBinName = (mPath.stem().string() + ".bin");
        gltf["buffers"] = {{{"uri", outBinName}, {"byteLength", mOutBuffer.size()}}};

        gltf["bufferViews"] = mBufferViews;
        // gltf["bufferViews"] = {{{"buffer", 0}, {"byteOffset", 0}, {"byteLength",
        // vtxDataSize}}};

        gltf["accessors"] = mAccessors;
        // gltf["accessors"] = {{{"bufferView", 0}, {"byteOffset", 0}, {"componentType", 5122},
        //     {"count", mVertexData.mVtxNum}, {"type", "VEC3"}, {"min", {0.0f, 0.0f, 0.0f}},
        //     {"max", {1.0f, 1.0f, 1.0f}}}};

        auto bin = dusk::io::FileStream::Create(mPath / outBinName);
        fs_writeBuf(bin,mOutBuffer);
    }
};

const std::filesystem::path bmd_unpack(
    const std::filesystem::path& outputName, const std::span<const u8>& buffer) {
    // J3DModelData* mdl = J3DModelLoaderDataBase::load(buffer.data(), 0);
    const J3DModelFileData* header = (const J3DModelFileData*)buffer.data();
    if ((header->mMagic1 == 'J3D1' && header->mMagic2 == 'bmd1') ||
        (header->mMagic1 == 'J3D2' && header->mMagic2 == 'bmd2') ||
        (header->mMagic1 != 'J3D2' || header->mMagic2 != 'bmd3'))
    {
        auto bin = dusk::io::FileStream::Create(outputName);
        bin.Write((char*)buffer.data(), buffer.size());
        return outputName;
    }

    std::filesystem::create_directories(outputName);

    auto unpacker = BMDUnpacker(outputName, buffer);
    unpacker.unpack();

    // {
    //     OSInit();
    //     JKRExpHeap* heap = JKRExpHeap::createRoot(2, false);
    //     J3DModelData* data = J3DModelLoaderDataBase::load(buffer.data(), 0);
    //     heap->destroy();
    // }

    const auto gltfPath = outputName / (outputName.stem().string() + ".gltf");

    auto gltf_fs = dusk::io::FileStream::Create(gltfPath); 
    fs_writeString(gltf_fs,unpacker.gltf.dump(4));

    return outputName;
}
const std::vector<u8> bmd_pack(const std::filesystem::path& source) {
    return {};
}

}  // namespace assets
