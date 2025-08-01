// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

#include "compiled_model.h"
#include "cpu_memory.h"
#include "cpu_shape.h"
#include "cpu_tensor.h"
#include "graph.h"
#include "memory_state.h"
#include "openvino/core/node.hpp"
#include "openvino/core/node_output.hpp"
#include "openvino/core/type/element_type.hpp"
#include "openvino/itt.hpp"
#include "openvino/runtime/isync_infer_request.hpp"
#include "openvino/runtime/itensor.hpp"
#include "openvino/runtime/ivariable_state.hpp"
#include "openvino/runtime/profiling_info.hpp"
#include "openvino/runtime/so_ptr.hpp"
#include "proxy_mem_blk.h"

namespace ov::intel_cpu {

class AsyncInferRequest;

class SyncInferRequest : public ov::ISyncInferRequest {
public:
    explicit SyncInferRequest(CompiledModelHolder compiled_model);

    void infer() override;

    std::vector<ov::ProfilingInfo> get_profiling_info() const override;

    std::vector<ov::SoPtr<ov::IVariableState>> query_state() const override;

    void set_tensor(const ov::Output<const ov::Node>& port, const ov::SoPtr<ov::ITensor>& tensor) override;

    void set_tensors_impl(ov::Output<const ov::Node> port, const std::vector<ov::SoPtr<ov::ITensor>>& tensors) override;

    ov::SoPtr<ov::ITensor> get_tensor(const ov::Output<const ov::Node>& port) const override;
    std::vector<ov::SoPtr<ov::ITensor>> get_tensors(const ov::Output<const ov::Node>& _port) const override;

    void check_tensors() const override;

    /**
     * @brief      Sets the pointer to asynchronous inference request that holds this request
     * @param[in]  asyncRequest Pointer to asynchronous inference request
     */
    void set_async_request(AsyncInferRequest* asyncRequest);

    /**
     * @brief If `m_asyncRequest` is initialized throw exception with `ov::Cancelled` status if inference request is
     * canceled
     */

    void throw_if_canceled() const;

private:
    class OutputControlBlock {
    public:
        using MemBlockPtr = std::shared_ptr<MemoryBlockWithReuse>;

        OutputControlBlock(const ov::element::Type& precision, const Shape& shape);

        OutputControlBlock(const OutputControlBlock&) = delete;
        OutputControlBlock& operator=(const OutputControlBlock&) = delete;

        OutputControlBlock(OutputControlBlock&&) = default;
        OutputControlBlock& operator=(OutputControlBlock&&) = default;

        [[nodiscard]] std::shared_ptr<Tensor> tensor() const {
            return m_tensor;
        }

        [[nodiscard]] const void* rawPtr() const {
            return m_tensor->get_memory()->getData();
        }

        [[nodiscard]] MemBlockPtr currentMemBlock() const {
            return m_buffers[m_buffIndx];
        }

        MemBlockPtr nextMemBlock() {
            m_buffIndx ^= 0x1;
            if (!m_buffers[m_buffIndx]) {
                m_buffers[m_buffIndx] = std::make_shared<MemoryBlockWithReuse>();
            }
            return m_buffers[m_buffIndx];
        }

        void update() {
            m_proxyMemBlock->setMemBlockResize(currentMemBlock());
        }

    private:
        std::shared_ptr<Tensor> m_tensor = nullptr;
        ProxyMemoryBlockPtr m_proxyMemBlock = nullptr;
        std::array<MemBlockPtr, 2> m_buffers;
        int m_buffIndx = 0;
    };

    void create_infer_request();
    void init_tensor(const std::size_t& port_index, const ov::ISyncInferRequest::FoundPort::Type& type);

    void push_input_data(Graph& graph);
    void redefine_memory_for_input_nodes(Graph& graph);
    void update_external_tensor_ptrs();
    void change_default_ptr(Graph& graph);

    const ov::Output<const ov::Node>& get_internal_port(const ov::Output<const ov::Node>& port) const;

    void sub_streams_infer();

    std::unordered_map<std::size_t, OutputControlBlock> m_outputControlBlocks;

    std::unordered_map<std::size_t, ov::SoPtr<ov::ITensor>> m_input_external_ptr;
    std::unordered_map<std::size_t, ov::SoPtr<ov::ITensor>> m_output_external_ptr;

    openvino::itt::handle_t m_profiling_task = nullptr;
    std::vector<MemStatePtr> m_memory_states;
    AsyncInferRequest* m_asyncRequest = nullptr;
    CompiledModelHolder m_compiled_model;

    std::unordered_map<std::size_t, ov::Output<const ov::Node>> m_input_ports_map;
    std::unordered_map<std::size_t, ov::Output<const ov::Node>> m_output_ports_map;
    std::unordered_map<std::size_t, ov::SoPtr<ov::ITensor>> m_outputs;
};

}  // namespace ov::intel_cpu
