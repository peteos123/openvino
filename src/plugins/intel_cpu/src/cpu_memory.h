// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cpu_shape.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <oneapi/dnnl/dnnl.hpp>
#include <oneapi/dnnl/dnnl_common.hpp>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "cpu_types.h"
#include "dnnl_extension_utils.h"
#include "memory_desc/cpu_memory_desc.h"
#include "openvino/core/type/element_type.hpp"
#include "openvino/core/type/element_type_traits.hpp"

/**
 * @file contains a concept classes to work with memory/tensor/blob abstractions on plugin level.
 *
 * Memory is an abstraction of some real tensor which contains some data. As in short it's a pair of
 * memory descriptor and raw buffer handler to contains data. In case of system memory raw buffer it's simple
 * "void*" on some system memory buffer.
 *
 */

namespace ov::intel_cpu {

class Memory;
class ProxyMemoryBlock;

/**
 * @interface IMemoryBlock
 * @brief An interface to memory control object
 */

class IMemoryBlock {
public:
    virtual ~IMemoryBlock() = default;

    /**
     * @brief Accessor to underlying memory buffer
     * @return A pointer to underlying memory
     */
    [[nodiscard]] virtual void* getRawPtr() const noexcept = 0;

    /**
     * @brief Allows to set externally allocated memory buffer. In that case, the object has no control over the
     * provided memory.
     * @param ptr - pointer to the memory
     * @param size - size of the memory buffer
     */
    virtual void setExtBuff(void* ptr, size_t size) = 0;

    /**
     * @brief Resize underlying memory buffer
     * @param size - new memory size in bytes
     * @return status whether the memory reallocation was performed
     */
    virtual bool resize(size_t size) = 0;

    /**
     * @brief Check if the object has control over underlying memory buffer
     * @return status whether the object has control over underlying memory buffer
     */
    [[nodiscard]] virtual bool hasExtBuffer() const noexcept = 0;
};

/**
 * @brief An implementation of the mem block where memory reallocation occurs only if a bigger buffer is requested.
 */
class MemoryBlockWithReuse : public IMemoryBlock {
public:
    explicit MemoryBlockWithReuse(int numa_node = -1) : m_data(nullptr, release), numa_node(numa_node) {}
    [[nodiscard]] void* getRawPtr() const noexcept override;
    void setExtBuff(void* ptr, size_t size) override;
    bool resize(size_t size) override;
    [[nodiscard]] bool hasExtBuffer() const noexcept override;
    void free();
    [[nodiscard]] size_t size() const;  // in bytes

private:
    bool m_useExternalStorage = false;
    size_t m_memUpperBound = 0UL;
    std::unique_ptr<void, void (*)(void*)> m_data;
    int numa_node;

    static void release(void* ptr);
    static void destroy(void* ptr);
};

class IMemoryBlockObserver : public IMemoryBlock {
public:
    virtual void registerMemory(Memory* memPtr) = 0;
    virtual void unregisterMemory(Memory* memPtr) = 0;
};

/**
 * @brief A proxy object that additionally implements observer pattern
 */
class DnnlMemoryBlock : public IMemoryBlockObserver {
public:
    explicit DnnlMemoryBlock(std::unique_ptr<IMemoryBlock> memBlock) : m_pMemBlock(std::move(memBlock)) {}
    [[nodiscard]] void* getRawPtr() const noexcept override;
    void setExtBuff(void* ptr, size_t size) override;
    bool resize(size_t size) override;
    [[nodiscard]] bool hasExtBuffer() const noexcept override;
    void registerMemory(Memory* memPtr) override;
    void unregisterMemory(Memory* memPtr) override;

private:
    void notifyUpdate();

    std::unordered_set<Memory*> m_setMemPtrs;
    std::unique_ptr<IMemoryBlock> m_pMemBlock;
};

using MemoryBlockPtr = std::shared_ptr<IMemoryBlockObserver>;
using MemoryBlockCPtr = std::shared_ptr<const IMemoryBlockObserver>;

class DnnlMemBlockHandle {
public:
    DnnlMemBlockHandle(MemoryBlockPtr pBlock, Memory* pMem) : m_pMemBlock(std::move(pBlock)), m_pMem(pMem) {
        if (m_pMemBlock) {
            m_pMemBlock->registerMemory(m_pMem);
        }
    }

    DnnlMemBlockHandle(const DnnlMemBlockHandle&) = delete;
    DnnlMemBlockHandle& operator=(const DnnlMemBlockHandle&) = delete;

    DnnlMemBlockHandle(DnnlMemBlockHandle&& source) noexcept {
        std::swap(m_pMemBlock, source.m_pMemBlock);
        std::swap(m_pMem, source.m_pMem);
    }
    DnnlMemBlockHandle& operator=(DnnlMemBlockHandle&& rhs) noexcept {
        std::swap(m_pMemBlock, rhs.m_pMemBlock);
        std::swap(m_pMem, rhs.m_pMem);
        return *this;
    }

    ~DnnlMemBlockHandle() {
        if (m_pMemBlock) {
            m_pMemBlock->unregisterMemory(m_pMem);
        }
    }

    [[nodiscard]] MemoryBlockPtr get() const {
        return m_pMemBlock;
    }

    MemoryBlockPtr::element_type* operator->() const noexcept {
        return m_pMemBlock.get();
    }

private:
    MemoryBlockPtr m_pMemBlock = nullptr;
    Memory* m_pMem = nullptr;
};

class IMemory {
public:
    virtual ~IMemory() = default;

    [[nodiscard]] virtual const MemoryDesc& getDesc() const = 0;
    [[nodiscard]] virtual MemoryDescPtr getDescPtr() const = 0;

    [[nodiscard]] virtual void* getData() const = 0;  // pointer to the actual memory

    template <typename T, typename datatype = std::decay_t<T>>
    [[nodiscard]] T* getDataAs() const {
        /** @todo enabling this check requires all the nodes to follow this requirement
         * OPENVINO_ASSERT(element::from<datatype>() == getPrecision(),
         * "Memory data element type ", getPrecision(), " is not representable as ", element::from<datatype>());
         */
        return static_cast<T*>(getData());
    }

    [[nodiscard]] virtual size_t getSize() const = 0;  // in bytes
    [[nodiscard]] virtual const Shape& getShape() const = 0;
    [[nodiscard]] virtual const VectorDims& getStaticDims() const = 0;

    // Redefines descriptor. The memory descriptor will be replaced with the new one.
    // Memory will not be reallocated according to the dynamic memory block policy
    // Caution!!! This action invalidates the previous data layout. The old data may become unreachable.
    virtual void redefineDesc(MemoryDescPtr desc) = 0;

    virtual void load(const IMemory& src, bool ftz, bool bf16saturation) const = 0;

    [[nodiscard]] virtual MemoryBlockPtr getMemoryBlock() const = 0;

    virtual void nullify() = 0;

    [[nodiscard]] bool isDefined() const noexcept {
        if (auto desc = getDescPtr()) {
            return desc->isDefined();
        }
        return false;
    }

    // oneDNN specifics for backward compatibility
    [[nodiscard]] virtual dnnl::memory getPrimitive() const = 0;

    [[nodiscard]] ov::element::Type getPrecision() const {
        return getDesc().getPrecision();
    }

    [[nodiscard]] dnnl::memory::data_type getDataType() const {
        return DnnlExtensionUtils::ElementTypeToDataType(getDesc().getPrecision());
    }

    template <typename T,
              std::enable_if_t<!std::is_pointer_v<T> && !std::is_reference_v<T>, int> = 0,
              std::enable_if_t<std::is_base_of_v<MemoryDesc, T>, int> = 0>
    [[nodiscard]] std::shared_ptr<T> getDescWithType() const;
};

class StaticMemory final : public IMemory {
public:
    class StaticMemoryBlock : public IMemoryBlockObserver {
    public:
        explicit StaticMemoryBlock(size_t size);
        StaticMemoryBlock(void* data, size_t size);
        [[nodiscard]] void* getRawPtr() const noexcept override;
        void setExtBuff(void* ptr, size_t size) override;
        bool resize(size_t size) override;
        [[nodiscard]] bool hasExtBuffer() const noexcept override;
        void registerMemory(Memory* memPtr) override;
        void unregisterMemory(Memory* memPtr) override;

    private:
        size_t m_size = 0;
        MemoryBlockWithReuse memBlockImpl;
    };

    using MemBlockPtr = std::shared_ptr<StaticMemoryBlock>;

    StaticMemory(dnnl::engine eng, MemoryDescPtr desc, const void* data = nullptr, bool pads_zeroing = true);
    StaticMemory(dnnl::engine eng, const MemoryDesc& desc, const void* data = nullptr, bool pads_zeroing = true);

    StaticMemory(const StaticMemory&) = delete;
    StaticMemory& operator=(const StaticMemory&) = delete;

    StaticMemory(Memory&&) = delete;
    StaticMemory& operator=(StaticMemory&&) = delete;

    [[nodiscard]] const MemoryDesc& getDesc() const override;
    [[nodiscard]] MemoryDescPtr getDescPtr() const override;

    [[nodiscard]] void* getData() const override;  // pointer to the actual memory

    [[nodiscard]] size_t getSize() const override;  // in bytes
    [[nodiscard]] const Shape& getShape() const override;
    [[nodiscard]] const VectorDims& getStaticDims() const override;

    // Always throws since a static memory descriptor should not be modified
    void redefineDesc(MemoryDescPtr desc) override;

    void load(const IMemory& src, bool ftz, bool bf16saturation) const override;

    [[nodiscard]] MemoryBlockPtr getMemoryBlock() const override;

    // oneDNN specifics for backward compatibility
    [[nodiscard]] dnnl::memory getPrimitive() const override;

    void nullify() override;

private:
    dnnl::engine m_eng;
    MemoryDescPtr m_pMemDesc;
    size_t m_size;
    dnnl::memory m_prim;
    MemBlockPtr m_pMemBlock;
    std::string dnnlErrorCtx;
};

class Memory : public IMemory {
public:
    Memory(dnnl::engine eng, MemoryDescPtr desc, const void* data = nullptr, bool pads_zeroing = true);
    Memory(dnnl::engine eng, const MemoryDesc& desc, const void* data = nullptr, bool pads_zeroing = true);
    Memory(dnnl::engine eng, MemoryDescPtr desc, MemoryBlockPtr block);
    Memory(dnnl::engine eng, const MemoryDesc& desc, MemoryBlockPtr block);

    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    Memory(Memory&&) = delete;
    Memory& operator=(Memory&&) = delete;

    dnnl::memory getPrimitive() const override;

    const MemoryDesc& getDesc() const override {
        return *m_pMemDesc;
    }

    MemoryDescPtr getDescPtr() const override {
        return m_pMemDesc;
    }

    void* getData() const override;

    size_t getSize() const override;

    const Shape& getShape() const override {
        return getDesc().getShape();
    }

    const VectorDims& getStaticDims() const override {
        return getDesc().getShape().getStaticDims();
    }

    void redefineDesc(MemoryDescPtr desc) override;

    void load(const IMemory& src, bool ftz, bool bf16saturation) const override;
    void nullify() override;

    dnnl::engine getEngine() const {
        return m_eng;
    }

    MemoryBlockPtr getMemoryBlock() const override {
        return m_blockHandle.get();
    }

private:
    friend DnnlMemoryBlock;
    friend ProxyMemoryBlock;

    void update();

    void create(const MemoryDesc& desc, const void* data = nullptr, bool pads_zeroing = true);
    void create(MemoryDescPtr desc, const void* data = nullptr, bool pads_zeroing = true);

    dnnl::engine m_eng;
    MemoryDescPtr m_pMemDesc;
    DnnlMemBlockHandle m_blockHandle;
    bool m_padsZeroing = true;
    class DnnlMemPrimHandle {
    public:
        explicit DnnlMemPrimHandle(const Memory* memObjPtr) : m_memObjPtr(memObjPtr) {}
        bool isInit() const;
        dnnl::memory getPrim() const;
        void resetDnnlPrim();

    private:
        // Since getPrim should behave as a constant method, even though it changes state, it must be thread safe.
        // To provide thead safety we use this mutex
        mutable std::mutex m_primCachingLock;
        mutable dnnl::memory m_prim;
        const Memory* m_memObjPtr;
    } dnnlMemHandle;

    void* getDataNoThrow() const noexcept {
        return m_blockHandle->getRawPtr();
    }
};

class StringMemory : public IMemory {
public:
    using OvString = ov::element_type_traits<ov::element::string>::value_type;

    class StringMemoryBlock {
    public:
        StringMemoryBlock() : m_data(nullptr, release) {};
        [[nodiscard]] OvString* getStringPtr() const noexcept;
        void setExtBuff(OvString* ptr, size_t size);
        [[nodiscard]] size_t getStrLen() const noexcept;
        [[nodiscard]] void* getRawPtr() const noexcept;
        bool resize(size_t size /* string elements number */);
        [[nodiscard]] bool hasExtBuffer() const noexcept;

    private:
        bool m_use_external_storage = false;
        size_t m_str_upper_bound = 0LU;
        std::unique_ptr<OvString, void (*)(OvString*)> m_data;

        static void release(OvString* ptr) {}
        static void destroy(OvString* ptr);
    };

    using StringMemoryBlockPtr = std::shared_ptr<StringMemoryBlock>;

    StringMemory(dnnl::engine engine, MemoryDescPtr desc, const void* data = nullptr);

    StringMemory(dnnl::engine engine, const MemoryDesc& desc, const void* data = nullptr)
        : StringMemory(std::move(engine), desc.clone(), data) {}

    StringMemory(dnnl::engine engine, MemoryDescPtr desc, StringMemoryBlockPtr block)
        : m_engine(std::move(engine)),
          m_mem_desc(std::move(desc)),
          m_memoryBlock(std::move(block)) {}

    StringMemory(dnnl::engine engine, const MemoryDesc& desc, StringMemoryBlockPtr block)
        : StringMemory(std::move(engine), desc.clone(), std::move(block)) {}

    [[nodiscard]] const MemoryDesc& getDesc() const override {
        return *m_mem_desc;
    }

    [[nodiscard]] MemoryDescPtr getDescPtr() const override {
        return m_mem_desc;
    }

    [[nodiscard]] void* getData() const override;

    [[nodiscard]] size_t getSize() const override;  // In bytes

    [[nodiscard]] const Shape& getShape() const override {
        return m_mem_desc->getShape();
    }

    [[nodiscard]] const VectorDims& getStaticDims() const override {
        return m_mem_desc->getShape().getStaticDims();
    }

    void redefineDesc(MemoryDescPtr desc) override;

    void load(const IMemory& src, bool ftz, bool bf16saturation) const override;

    [[nodiscard]] MemoryBlockPtr getMemoryBlock() const override;

    [[nodiscard]] StringMemoryBlockPtr getStringMemoryBlockPtr() const {
        return m_memoryBlock;
    }

    [[nodiscard]] dnnl::memory getPrimitive() const override;

    void nullify() override;

private:
    dnnl::engine m_engine;
    MemoryDescPtr m_mem_desc;
    StringMemoryBlockPtr m_memoryBlock;
};

using MemoryPtr = std::shared_ptr<IMemory>;
using MemoryCPtr = std::shared_ptr<const IMemory>;
using StringMemoryPtr = std::shared_ptr<StringMemory>;

bool mbind_move(void* data, size_t size, int targetNode);
bool mbind_move(const MemoryCPtr& mem, int numaNodeID);
bool mbind_move(const dnnl::memory& mem, int numaNodeID);

MemoryPtr split_horizontal(const dnnl::engine& eng,
                           const MemoryPtr& src,
                           int dim,
                           int w_rank,
                           int w_size,
                           bool need_fill = true);
MemoryPtr split_vertical(const dnnl::engine& eng,
                         const MemoryPtr& src,
                         int dim,
                         int w_rank,
                         int w_size,
                         bool need_fill = true);

}  // namespace ov::intel_cpu
