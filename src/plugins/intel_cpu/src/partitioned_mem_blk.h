// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cstddef>
#include <utility>

#include "cpu_memory.h"
#include "openvino/core/except.hpp"

namespace ov::intel_cpu {

/**
 * This is a memory block that represents a view on a subblock inside another continuous dynamic memory block
 *
 */
class PartitionedMemoryBlock : public IMemoryBlockObserver {
public:
    explicit PartitionedMemoryBlock(MemoryBlockPtr pBlock,
                                    size_t total_chunks = 1,
                                    ptrdiff_t offset_chunks = 0,
                                    size_t size_chunks = 1)
        : m_pBlock(std::move(pBlock)),
          m_total_chunks(total_chunks),
          m_offset_chunks(offset_chunks),
          m_size_chunks(size_chunks) {
        OPENVINO_ASSERT(m_pBlock, "Memory block is uninitialized");
    }

    [[nodiscard]] void* getRawPtr() const noexcept override;
    void setExtBuff(void* ptr, size_t size) override;
    bool resize(size_t size) override;
    [[nodiscard]] bool hasExtBuffer() const noexcept override;
    void registerMemory(Memory* memPtr) override;
    void unregisterMemory(Memory* memPtr) override;

private:
    MemoryBlockPtr m_pBlock;
    size_t m_total_chunks = 1;      // size of the parent memory in abstract chunks
    ptrdiff_t m_offset_chunks = 0;  // offset from the beginning of the external memory in abstract chunks
    size_t m_size_chunks = 1;       // size of the viewed partition in abstract chunks
    size_t m_size = 0;              // size of the viewed partition in bytes
};

}  // namespace ov::intel_cpu
