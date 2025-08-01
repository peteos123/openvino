// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "paged_attn.h"

#include <common/utils.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <oneapi/dnnl/dnnl_common.hpp>
#include <string>
#include <vector>

#include "config.h"
#include "cpu_memory.h"
#include "cpu_types.h"
#include "graph_context.h"
#include "memory_desc/cpu_memory_desc.h"
#include "node.h"
#include "nodes/common/blocked_desc_creator.h"
#include "nodes/kernels/scaled_attn/executor_pa_common.hpp"
#include "nodes/node_config.h"
#include "onednn/iml_type_mapper.h"
#include "openvino/core/except.hpp"
#include "openvino/core/node.hpp"
#include "openvino/core/type/element_type.hpp"
#include "openvino/runtime/system_conf.hpp"
#include "shape_inference/shape_inference_internal_dyn.hpp"
#include "utils/general_utils.h"

#if defined(OPENVINO_ARCH_X86) || defined(OPENVINO_ARCH_X86_64) || defined(OPENVINO_ARCH_ARM64)
#    include "kernels/scaled_attn/executor_pa.hpp"

using namespace ov::Extensions::Cpu::XARCH;
#endif

using namespace ov::Extensions::Cpu;
using namespace dnnl::impl;
using namespace dnnl::impl::cpu::x64;

namespace ov::intel_cpu::node {

struct PagedAttentionKey {
    ov::element::Type rtPrecision;

    [[nodiscard]] size_t hash() const;
    bool operator==(const PagedAttentionKey& rhs) const;
};

size_t PagedAttentionKey::hash() const {
    size_t seed = 0;
    seed = hash_combine(seed, rtPrecision.hash());

    return seed;
}

bool PagedAttentionKey::operator==(const PagedAttentionKey& rhs) const {
    auto retVal = rtPrecision == rhs.rtPrecision;

    return retVal;
}

PagedAttention::PagedAttention(const std::shared_ptr<ov::Node>& op, const GraphContext::CPtr& context)
    : Node(op, context, InternalDynShapeInferFactory()) {
    std::string errorMessage;
    if (!isSupportedOperation(op, errorMessage)) {
        OPENVINO_THROW_NOT_IMPLEMENTED(errorMessage);
    }
    // output score may have no child
    m_hasScore = !op->get_output_target_inputs(1).empty();
}

void PagedAttention::initSupportedPrimitiveDescriptors() {
    if (!supportedPrimitiveDescriptors.empty()) {
        return;
    }
    auto rtPrecision = getRuntimePrecision();

    NodeConfig config;
    const auto& creatorsMap = BlockedDescCreator::getCommonCreators();
    auto orgInputNumber = getOriginalInputsNumber();
    config.inConfs.resize(orgInputNumber);
    config.outConfs.resize(getOriginalOutputsNumber());
    config.inConfs[PagedAttentionExecutor::ID_Q].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(rtPrecision, getInputShapeAtPort(PagedAttentionExecutor::ID_Q)));
    config.inConfs[PagedAttentionExecutor::ID_K].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(rtPrecision, getInputShapeAtPort(PagedAttentionExecutor::ID_K)));
    config.inConfs[PagedAttentionExecutor::ID_V].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(rtPrecision, getInputShapeAtPort(PagedAttentionExecutor::ID_V)));

    CPU_NODE_ASSERT(orgInputNumber == 20U, "The input number of PagedAttention should be 20.");
    // kvcache, float, []
    auto past_key_input_mem_precision = getOriginalInputPrecisionAtPort(PagedAttentionExecutor::ID_KCACHE);
    auto past_value_input_mem_precision = getOriginalInputPrecisionAtPort(PagedAttentionExecutor::ID_VCACHE);
    config.inConfs[PagedAttentionExecutor::ID_KCACHE].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(past_key_input_mem_precision, getInputShapeAtPort(PagedAttentionExecutor::ID_KCACHE)));
    config.inConfs[PagedAttentionExecutor::ID_VCACHE].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(past_value_input_mem_precision, getInputShapeAtPort(PagedAttentionExecutor::ID_VCACHE)));
    // past_lens, int, [b_seq]
    config.inConfs[PagedAttentionExecutor::ID_PAST_LENS].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::i32, getInputShapeAtPort(PagedAttentionExecutor::ID_PAST_LENS)));
    // subsequence_begins, int, [b_seq]
    config.inConfs[PagedAttentionExecutor::ID_SUBSEQUENCE_BEGINS].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::i32, getInputShapeAtPort(PagedAttentionExecutor::ID_SUBSEQUENCE_BEGINS)));
    // block_indices, int, [num_blocks]
    config.inConfs[PagedAttentionExecutor::ID_BLOCK_INDICES].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::i32, getInputShapeAtPort(PagedAttentionExecutor::ID_BLOCK_INDICES)));
    // block_indices_begins, int, [b_seq]
    config.inConfs[PagedAttentionExecutor::ID_BLOCK_INDICES_BEGINS].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::i32, getInputShapeAtPort(PagedAttentionExecutor::ID_BLOCK_INDICES_BEGINS)));
    // scale, float, []
    config.inConfs[PagedAttentionExecutor::ID_SCALE].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::f32, getInputShapeAtPort(PagedAttentionExecutor::ID_SCALE)));
    // sliding_window, int, []
    config.inConfs[PagedAttentionExecutor::ID_SLIDING_WINDOW].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::i32, getInputShapeAtPort(PagedAttentionExecutor::ID_SLIDING_WINDOW)));
    // alibi_slopes, float, [H|0]
    config.inConfs[PagedAttentionExecutor::ID_ALIBI_SLOPES].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::f32, getInputShapeAtPort(PagedAttentionExecutor::ID_ALIBI_SLOPES)));
    // max_context_len, int, []
    config.inConfs[PagedAttentionExecutor::ID_MAX_CONTEXT_LEN].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::i32, getInputShapeAtPort(PagedAttentionExecutor::ID_MAX_CONTEXT_LEN)));

    config.outConfs[0].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)->createSharedDesc(rtPrecision, getOutputShapeAtPort(0)));
    config.outConfs[1].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)->createSharedDesc(ov::element::f32, getOutputShapeAtPort(1)));

    // score_aggregation_window, float, [batch_size_in_sequences || 0]
    config.inConfs[PagedAttentionExecutor::ID_SCORE_AGGREGATION_WINDOW].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::i32,
                               getInputShapeAtPort(PagedAttentionExecutor::ID_SCORE_AGGREGATION_WINDOW)));
    // rotated_block_indices, int, [num_rotated_blocks || 0]
    config.inConfs[PagedAttentionExecutor::ID_ROTATED_BLOCK_INDICES].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::i32,
                               getInputShapeAtPort(PagedAttentionExecutor::ID_ROTATED_BLOCK_INDICES)));
    // rotation_deltas, int, [num_rotated_blocks, block_size || 1] || [0]
    config.inConfs[PagedAttentionExecutor::ID_ROTATION_DELTAS].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::i32, getInputShapeAtPort(PagedAttentionExecutor::ID_ROTATION_DELTAS)));
    // rotation_trig_lut, float, [max_context_len, embedding_size (aka S) || 0]
    config.inConfs[PagedAttentionExecutor::ID_ROTATION_TRIG_LUT].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::f32, getInputShapeAtPort(PagedAttentionExecutor::ID_ROTATION_TRIG_LUT)));
    // xattention_threshold, float, [B_seq]
    config.inConfs[PagedAttentionExecutor::ID_XATTENTION_THRESHOLD].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::f32, getInputShapeAtPort(PagedAttentionExecutor::ID_XATTENTION_THRESHOLD)));
    // xattention_block_size, float, []
    config.inConfs[PagedAttentionExecutor::ID_XATTENTION_BLOCK_SIZE].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::i32,
                               getInputShapeAtPort(PagedAttentionExecutor::ID_XATTENTION_BLOCK_SIZE)));
    // xattention_stride, int, []
    config.inConfs[PagedAttentionExecutor::ID_XATTENTION_STRIDE].setMemDesc(
        creatorsMap.at(LayoutType::ncsp)
            ->createSharedDesc(ov::element::i32,
                               getInputShapeAtPort(PagedAttentionExecutor::ID_XATTENTION_BLOCK_SIZE)));

    supportedPrimitiveDescriptors.emplace_back(config, impl_desc_type::ref_any);
}

bool PagedAttention::isQuantByChannel(const Config::CacheQuantMode mode,
                                      const ov::element::Type precision,
                                      const bool isKey) {
    // AUTO means select by primitive
    // for non-x86 platform, by-channel quantization is disabled
    // By default, by-channel should only be enabled when precision is integral
    bool byChannel = precision.is_integral() && isKey;
    if (!precision.is_integral() || mode == Config::CacheQuantMode::BY_TOKEN) {
        byChannel = false;
    }
#if defined(OPENVINO_ARCH_ARM64)
    byChannel = false;
#endif
    return byChannel;
}

void PagedAttention::createPrimitive() {
    auto rtPrecision = getRuntimePrecision();

    // in one model, kvCachePrecision could not be changed so no need to care whether it may be changed.
    PagedAttentionKey key = {rtPrecision};

    auto builder = [&]([[maybe_unused]] const PagedAttentionKey& key) -> std::shared_ptr<PagedAttentionExecutor> {
#if defined(OPENVINO_ARCH_X86_64) || (defined(OPENVINO_ARCH_ARM64))
        // Since we are quantize only last dim it's safe to use the last dim of KV.
        auto kCachePrecision = getOriginalInputPrecisionAtPort(PagedAttentionExecutor::ID_KCACHE);
        auto vCachePrecision = getOriginalInputPrecisionAtPort(PagedAttentionExecutor::ID_VCACHE);
        const auto& cpuConfig = context->getConfig();

        bool quantKeybyChannel = isQuantByChannel(cpuConfig.keyCacheQuantMode, cpuConfig.keyCachePrecision, true);
        bool quantValuebyChannel =
            isQuantByChannel(cpuConfig.valueCacheQuantMode, cpuConfig.valueCachePrecision, false);
        PagedAttnQuantParams params{cpuConfig.keyCacheGroupSize,
                                    cpuConfig.valueCacheGroupSize,
                                    quantKeybyChannel,
                                    quantValuebyChannel,
                                    cpuConfig.enableSageAttn};
        return make_pa_executor(rtPrecision, kCachePrecision, vCachePrecision, params);
#else
        return nullptr;
#endif
    };

    auto cache = context->getParamsCache();
    auto result = cache->getOrCreate(key, builder);
    if (!result.first) {
        CPU_NODE_THROW("AttentionExecutor creation fails with precision " + rtPrecision.to_string());
    }
    m_executor = result.first;
}

void PagedAttention::execute([[maybe_unused]] const dnnl::stream& strm) {
    auto orginInputNumber = getOriginalInputsNumber();
    std::vector<MemoryPtr> inputs(orginInputNumber);
    std::vector<MemoryPtr> outputs(m_hasScore ? 2 : 1);

    for (size_t i = 0; i < orginInputNumber; i++) {
        inputs[i] = getSrcMemoryAtPort(i);
    }

    auto outDims = inputs[0]->getStaticDims();
    const auto& keyDims = inputs[1]->getStaticDims();
    const auto& valueDims = inputs[2]->getStaticDims();
    // value head_size may be not same with key
    if (keyDims[1] != valueDims[1]) {
        // The outDims[1] should be `num_heads * v_head_size`, it can be got from:
        // because:
        //   q: query_ps[1] = num_heads * head_size
        //   k: key_ps[1] = num_kv_heads * head_size
        //   v: value_ps[1] = num_kv_heads * v_head_size
        // therefore:
        //   q * v / k = (num_heads * head_size) * (num_kv_heads * v_head_size) /
        //               (num_kv_heads * head_size) = num_heads * v_head_size
        outDims[1] = outDims[1] * valueDims[1] / keyDims[1];
    }
    if (m_hasScore) {
        size_t len = 0;
        const auto& pastLensDims = inputs[5]->getStaticDims();
        const auto* pastLens = inputs[5]->getDataAs<const int32_t>();
        for (size_t i = 0; i < pastLensDims[0]; i++) {
            len += pastLens[i];
        }
        len += outDims[0];
        VectorDims scoreDims{len};
        redefineOutputMemory({outDims, scoreDims});
    } else {
        redefineOutputMemory({outDims, {0}});
    }

    outputs[0] = getDstMemoryAtPort(0);
    if (m_hasScore) {
        outputs[1] = getDstMemoryAtPort(1);
    }

    m_executor->execute(inputs, outputs);
}

bool PagedAttention::isSupportedOperation(const std::shared_ptr<const ov::Node>& op,
                                          std::string& errorMessage) noexcept {
    try {
        auto vCachePrecision = op->get_input_element_type(PagedAttentionExecutor::ID_VCACHE);
        auto kCachePrecision = op->get_input_element_type(PagedAttentionExecutor::ID_KCACHE);
        if (any_of(vCachePrecision,
                   ov::element::u4,
                   ov::element::u8,
                   ov::element::f32,
                   ov::element::f16,
                   ov::element::bf16)) {
            if (none_of(kCachePrecision,
                        ov::element::u4,
                        ov::element::i8,
                        ov::element::u8,
                        ov::element::f16,
                        ov::element::f32,
                        ov::element::bf16)) {
                errorMessage = "PageAttn key value cache compression doesn't support key cache prec " +
                               kCachePrecision.to_string() + " value cache prec " + vCachePrecision.to_string();
                return false;
            }
        }
        auto orgInput = static_cast<int>(op->get_input_size());
        if (op->get_type_name() == std::string("PagedAttentionExtension") &&
            orgInput == PagedAttentionExecutor::ID_SLIDING_WINDOW + 1) {
            return true;
        }
    } catch (...) {
        return false;
    }
    return true;
}

ov::element::Type PagedAttention::getRuntimePrecision() const {
    auto rtPrecision = getOriginalInputPrecisionAtPort(0);
#if defined(OPENVINO_ARCH_ARM64)
    if (rtPrecision == ov::element::f16) {
        rtPrecision = ov::element::f16;
    } else {
        rtPrecision = ov::element::f32;
    }
    return rtPrecision;
#endif
    // bf16 should be enabled only when platform supports
    if (rtPrecision == ov::element::bf16 && ov::with_cpu_x86_bfloat16()) {
        rtPrecision = ov::element::bf16;
    } else if (rtPrecision == ov::element::f16 && ov::with_cpu_x86_avx512_core_fp16()) {
        rtPrecision = ov::element::f16;
    } else {
        rtPrecision = ov::element::f32;
    }
    return rtPrecision;
}

}  // namespace ov::intel_cpu::node
