// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "blocked_desc_creator.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <utility>
#include <vector>

#include "cpu_shape.h"
#include "cpu_types.h"
#include "memory_desc/cpu_blocked_memory_desc.h"
#include "memory_desc/cpu_memory_desc.h"
#include "openvino/core/except.hpp"
#include "openvino/core/type/element_type.hpp"

namespace ov::intel_cpu {
namespace {

constexpr size_t channelsPos = 1LU;

class PlainFormatCreator : public BlockedDescCreator {
public:
    [[nodiscard]] CpuBlockedMemoryDesc createDesc(const ov::element::Type& precision,
                                                  const Shape& srcShape) const override {
        VectorDims order(srcShape.getRank());
        std::iota(order.begin(), order.end(), 0);
        return {precision, srcShape, srcShape.getDims(), order};
    }
    [[nodiscard]] size_t getMinimalRank() const override {
        return 0LU;
    }
};

class PerChannelCreator : public BlockedDescCreator {
public:
    [[nodiscard]] CpuBlockedMemoryDesc createDesc(const ov::element::Type& precision,
                                                  const Shape& srcShape) const override {
        VectorDims order(srcShape.getRank());
        std::iota(order.begin(), order.end(), 0);
        VectorDims blkDims = srcShape.getDims();
        if (srcShape.getRank() > 2) {
            auto moveElementBack = [](VectorDims& vector, size_t indx) {
                auto itr = vector.begin() + indx;
                std::rotate(itr, itr + 1, vector.end());
            };

            moveElementBack(order, channelsPos);
            moveElementBack(blkDims, channelsPos);
        }

        return {precision, srcShape, blkDims, order};
    }
    [[nodiscard]] size_t getMinimalRank() const override {
        return 3LU;
    }
};

class ChannelBlockedCreator : public BlockedDescCreator {
public:
    explicit ChannelBlockedCreator(size_t blockSize) : _blockSize(blockSize) {}
    [[nodiscard]] CpuBlockedMemoryDesc createDesc(const ov::element::Type& precision,
                                                  const Shape& srcShape) const override {
        OPENVINO_ASSERT(srcShape.getRank() >= 2, "Can't create blocked tensor descriptor!");

        VectorDims order(srcShape.getRank());
        std::iota(order.begin(), order.end(), 0);
        order.push_back(channelsPos);

        VectorDims blkDims = srcShape.getDims();
        if (Shape::UNDEFINED_DIM != blkDims[channelsPos]) {
            blkDims[channelsPos] = blkDims[channelsPos] / _blockSize + (blkDims[channelsPos] % _blockSize ? 1 : 0);
        }
        blkDims.push_back(_blockSize);

        return {precision, srcShape, blkDims, order};
    }
    [[nodiscard]] size_t getMinimalRank() const override {
        return 3LU;
    }

private:
    size_t _blockSize;
};

}  // namespace

const BlockedDescCreator::CreatorsMap& BlockedDescCreator::getCommonCreators() {
    static const CreatorsMap map{{LayoutType::nspc, CreatorConstPtr(new PerChannelCreator)},
                                 {LayoutType::nCsp8c, CreatorConstPtr(new ChannelBlockedCreator(8))},
                                 {LayoutType::nCsp16c, CreatorConstPtr(new ChannelBlockedCreator(16))},
                                 {LayoutType::ncsp, CreatorConstPtr(new PlainFormatCreator)}};
    return map;
}

std::pair<CreatorsMapFilterConstIterator, CreatorsMapFilterConstIterator> BlockedDescCreator::makeFilteredRange(
    const CreatorsMap& map,
    unsigned int rank) {
    auto rankFilter = [rank](const CreatorsMap::value_type& item) {
        return item.second->getMinimalRank() <= rank;
    };

    auto first = CreatorsMapFilterConstIterator(std::move(rankFilter), map.begin(), map.end());
    auto last = first.end();
    return std::make_pair(first, last);
}

std::pair<CreatorsMapFilterConstIterator, CreatorsMapFilterConstIterator> BlockedDescCreator::makeFilteredRange(
    const CreatorsMap& map,
    unsigned rank,
    const std::vector<LayoutType>& supportedTypes) {
    unsigned bitMask = 0UL;
    for (const auto& item : supportedTypes) {
        bitMask |= 1 << static_cast<unsigned>(item);
    }

    auto rankTypesFilter = [rank, bitMask](const CreatorsMap::value_type& item) {
        if (!(bitMask & (1 << static_cast<unsigned>(item.first)))) {
            return false;
        }
        return item.second->getMinimalRank() <= rank;
    };

    auto first = CreatorsMapFilterConstIterator(std::move(rankTypesFilter), map.begin(), map.end());
    auto last = first.end();
    return std::make_pair(first, last);
}

std::pair<CreatorsMapFilterConstIterator, CreatorsMapFilterConstIterator> BlockedDescCreator::makeFilteredRange(
    const CreatorsMap& map,
    BlockedDescCreator::Predicate predicate) {
    auto first = CreatorsMapFilterConstIterator(std::move(predicate), map.begin(), map.end());
    auto last = first.end();
    return std::make_pair(first, last);
}

}  // namespace ov::intel_cpu
