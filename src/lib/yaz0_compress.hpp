#pragma once
#include <span>
#include <vector>
#include "JSystem/JKernel/JKRDecomp.h"

namespace assets {

std::vector<u8> Yaz0Compress(const std::span<const u8>& src);

}
