// Copyright (C) 2024 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <oneapi/dnnl/dnnl_common_types.h>
#include <xbyak/xbyak.h>

#include <common/c_types_map.hpp>
#include <cpu/x64/cpu_isa_traits.hpp>
#include <cpu/x64/jit_generator.hpp>
#include <cpu/x64/matmul/brgemm_matmul_copy_utils.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>

#include "cache/multi_cache.h"
#include "emitters/snippets/cpu_kernel_executor_table.hpp"
#include "emitters/snippets/input_repacker.hpp"
#include "openvino/core/type/element_type.hpp"
#include "snippets/emitter.hpp"
#include "snippets/kernel_executor_table.hpp"
#include "snippets/lowered/expression.hpp"
#include "snippets/lowered/linear_ir.hpp"
#include "transformations/snippets/x64/op/brgemm_utils.hpp"

namespace ov::intel_cpu {

struct BrgemmCopyBKernelConfig : public snippets::KernelExecutorBase::GenericConfig {
public:
    BrgemmCopyBKernelConfig() = default;
    explicit BrgemmCopyBKernelConfig(const brgemm_utils::BrgemmConfig& brgemm_config);

    bool operator==(const BrgemmCopyBKernelConfig& rhs) const;
    bool operator!=(const BrgemmCopyBKernelConfig& rhs) const {
        return !(*this == rhs);
    }

    [[nodiscard]] std::unique_ptr<GenericConfig> get_clone_ptr() const override {
        return std::make_unique<BrgemmCopyBKernelConfig>(*this);
    }

    [[nodiscard]] bool is_empty() const;
    [[nodiscard]] bool is_completed() const override;

    void update(dnnl_dim_t N,
                dnnl_dim_t N_blk,
                dnnl_dim_t K,
                dnnl_dim_t K_blk,
                dnnl_dim_t copy_B_wei_stride,
                dnnl_dim_t LDB);

    [[nodiscard]] size_t hash() const override {
        return m_hash;
    }

    [[nodiscard]] dnnl_data_type_t get_src_dt() const {
        return m_static_params->src_dt;
    }
    [[nodiscard]] dnnl_data_type_t get_wei_dt() const {
        return m_static_params->wei_dt;
    }
    [[nodiscard]] dnnl_data_type_t get_original_wei_dt() const {
        return m_static_params->original_wei_dt;
    }

    [[nodiscard]] dnnl::impl::cpu::x64::cpu_isa_t get_isa() const {
        return m_static_params->isa;
    }
    [[nodiscard]] bool is_with_comp() const {
        return m_static_params->is_with_comp;
    }
    [[nodiscard]] bool is_transposed_B() const {
        return m_static_params->is_transposed_B;
    }
    [[nodiscard]] bool are_wei_blocked() const {
        return m_static_params->are_wei_blocked;
    }

    [[nodiscard]] dnnl_dim_t get_N() const {
        return m_N;
    }
    [[nodiscard]] dnnl_dim_t get_N_blk() const {
        return m_N_blk;
    }
    [[nodiscard]] dnnl_dim_t get_N_tail() const {
        return m_N % m_N_blk;
    }
    [[nodiscard]] dnnl_dim_t get_wei_N_blk() const {
        return m_static_params->wei_N_blk;
    }
    [[nodiscard]] dnnl_dim_t get_wei_N_tail() const {
        return m_N_blk % m_static_params->wei_N_blk;
    }
    [[nodiscard]] dnnl_dim_t get_wei_K_blk() const {
        return m_static_params->wei_K_blk;
    }
    [[nodiscard]] dnnl_dim_t get_K() const {
        return m_K;
    }
    [[nodiscard]] dnnl_dim_t get_K_blk() const {
        return m_K_blk;
    }
    [[nodiscard]] dnnl_dim_t get_copy_B_wei_stride() const {
        return m_copy_B_wei_stride;
    }
    [[nodiscard]] dnnl_dim_t get_LDB() const {
        return m_LDB;
    }

#ifdef SNIPPETS_DEBUG_CAPS
    [[nodiscard]] std::string to_string() const override;
#endif

private:
    struct StaticParams {
        StaticParams(const element::Type& src_type,
                     const element::Type& wei_type,
                     const element::Type& original_wei_type,
                     dnnl::impl::cpu::x64::cpu_isa_t isa,
                     bool is_with_comp,
                     bool is_transposed_B,
                     bool are_wei_blocked,
                     dnnl_dim_t wei_N_blk,
                     dnnl_dim_t wei_K_blk);

        const dnnl_data_type_t src_dt{dnnl_data_type_undef}, wei_dt{dnnl_data_type_undef};
        const dnnl_data_type_t original_wei_dt{dnnl_data_type_undef};
        const dnnl::impl::cpu::x64::cpu_isa_t isa{dnnl::impl::cpu::x64::isa_undef};
        const bool is_with_comp{false};
        const bool is_transposed_B{false};
        const bool are_wei_blocked{false};
        const dnnl_dim_t wei_N_blk{0}, wei_K_blk{0};
        const size_t hash{0};

        bool operator==(const StaticParams& rhs) const;
        bool operator!=(const StaticParams& rhs) const {
            return !(*this == rhs);
        }

#ifdef SNIPPETS_DEBUG_CAPS
        [[nodiscard]] std::string to_string() const;
#endif

    private:
        static size_t init_hash(const dnnl_data_type_t& src_dt,
                                const dnnl_data_type_t& wei_dt,
                                const dnnl_data_type_t& original_wei_dt,
                                dnnl::impl::cpu::x64::cpu_isa_t primitive_isa,
                                bool is_with_comp,
                                bool is_transposed_B,
                                bool are_wei_blocked,
                                dnnl_dim_t wei_N_blk,
                                dnnl_dim_t wei_K_blk);
    };

    [[nodiscard]] size_t compute_hash() const;

    std::shared_ptr<StaticParams> m_static_params;
    dnnl_dim_t m_N{0}, m_N_blk{0};
    dnnl_dim_t m_K{0}, m_K_blk{0};
    dnnl_dim_t m_copy_B_wei_stride{0}, m_LDB{0};
    size_t m_hash{SIZE_MAX};
};

struct BrgemmCopyBKernel : public InputRepackerKernel, public dnnl::impl::cpu::x64::jit_generator_t {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(BrgemmCopyBKernel)
    struct call_args {
        const void* src = nullptr;
        void* tr_src = nullptr;
        void* compensation_ptr = nullptr;
    };

    BrgemmCopyBKernel();
    explicit BrgemmCopyBKernel(const BrgemmCopyBKernelConfig& conf);

    dnnl::impl::status_t create_kernel() override;

    void operator()(const void* args) const override;

private:
    void generate() override;

    void emit_brgemm_copy_b_kernel_call(size_t N, size_t K, size_t offset_in, size_t offset_out, size_t offset_comp);

    static void execute(dnnl::impl::cpu::x64::matmul::jit_brgemm_matmul_copy_b_t* kernel,
                        const void* src,
                        const void* dst,
                        const void* comp,
                        size_t N,
                        size_t K);

    static void init_brgemm_copy_b_kernel(
        std::unique_ptr<dnnl::impl::cpu::x64::matmul::jit_brgemm_matmul_copy_b_t>& kernel,
        const BrgemmCopyBKernelConfig& conf);

    std::set<snippets::Reg> get_live_regs() const;

    static constexpr auto abi_param_regs = dnnl::impl::cpu::x64::abi_param_regs;
    const Xbyak::Reg64 src_reg = abi_param2;
    const Xbyak::Reg64 tr_src_reg = abi_param3;
    const Xbyak::Reg64 comp_reg = abi_param4;

    const bool is_with_comp = false;
    const bool is_transpose = false;
    const size_t K = 0;
    const size_t N_blk = 0;
    const size_t wei_N_blk = 0;
    const size_t wei_N_tail = 0;
    size_t stride_in = 0;
    size_t stride_out = 0;
    size_t stride_comp = 0;

    // JIT kernel code of the current BrgemmCopyBKernel
    void (*ker_)(const call_args*);

    // JIT kernel dnnl Brgemm copy b which is called in the current snippets BrgemmCopyBKernel
    std::unique_ptr<dnnl::impl::cpu::x64::matmul::jit_brgemm_matmul_copy_b_t> dnnl_brgemm_copy_b_kernel = nullptr;
};

class BrgemmCopyBKernelExecutor : public CPUKernelExecutor<BrgemmCopyBKernelConfig, BrgemmCopyBKernel> {
public:
    BrgemmCopyBKernelExecutor(ov::intel_cpu::MultiCacheWeakPtr kernel_cache, BrgemmCopyBKernelConfig config);

    static void execute(const BrgemmCopyBKernelExecutor* executor, BrgemmCopyBKernel::call_args* args);

protected:
    [[nodiscard]] std::shared_ptr<BrgemmCopyBKernel> compile_kernel(const BrgemmCopyBKernelConfig& c) const override;

    void update_config(const ov::snippets::lowered::ExpressionPtr& expr,
                       const ov::snippets::lowered::LinearIRCPtr& linear_ir,
                       BrgemmCopyBKernelConfig& config) const override;
};
#define GET_OFF_BRGEMM_COPY_B_ARGS(field) offsetof(BrgemmCopyBKernel::call_args, field)

}  // namespace ov::intel_cpu
