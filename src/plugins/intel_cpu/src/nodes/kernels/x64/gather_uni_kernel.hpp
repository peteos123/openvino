// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

// Gather kernel implements two approaches for indices calculation: "Short" and "Long".
// 1. Short approach is applicable for cases when the number of elements less or equal to vector register length.
// It just uses permutation of current indices vector to retrieve the next.
// 2. Long approach is applicable for cases when the number of elements is greater than vector register length.
// It increases indices in vector on vector length and normalizes upper bound of indices.
//
//                    SUPPORTED CASES
//--------------------------------------------------------------
//  After axis |         AVX512        |         AVX2          |
// (block) size| 32bit | 16bit |  8bit | 32bit | 16bit |  8bit |
//                      STATIC SHAPES
//      1      |   X   |   X   |   X   |   X   |   X   |   X   |
// >1 & <=vlen |   X   |   X   |   X   |   X   |       |       |
//                      DYNAMIC SHAPES
//      1      |   X   |   X   |   X   |   X   |   X   |   X   |
//--------------------------------------------------------------

#pragma once

#include <xbyak/xbyak.h>

#include <cassert>
#include <common/utils.hpp>
#include <cpu/x64/cpu_isa_traits.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "cpu/x64/jit_generator.hpp"
#include "emitters/plugin/x64/jit_conversion_emitters.hpp"
#include "openvino/core/type/element_type.hpp"

namespace ov::intel_cpu {

struct jGatherConfParams {
    uint64_t dataTypeSize = 1LU;
    ov::element::Type in_prec = ov::element::f32;
    ov::element::Type out_prec = ov::element::f32;
    bool reverseIndexing = true;
    bool dynamicShapes = false;
    uint64_t batchDims = 0LU;
    uint64_t beforeAxisSize = 0LU;
    uint64_t specIdxSize = 0LU;
    uint64_t afterAxisSize = 0LU;
};

struct gatherJitExecArgs {
    const void* src = nullptr;
    const void* indices = nullptr;
    void* dst = nullptr;
    const int* axisDim = nullptr;
    const uint64_t* start = nullptr;
    const uint64_t* specIndicesSize = nullptr;
    const uint64_t* betweenBatchAndAxisSize = nullptr;
    const uint64_t* axisAndAfterAxisSizeB = nullptr;
    const uint64_t* srcAfterBatchSizeB = nullptr;
    const int* permIdxMask = nullptr;
    const int* beforeAxisDiff = nullptr;

    const int* beforeAxisPermMask = nullptr;
    const int* afterAxIdxB = nullptr;
    const int* afterAxisPermMask = nullptr;
    const uint64_t* afterAxisSize = nullptr;
    const int* specIdxDiff = nullptr;

    uint64_t workAmount = 0LU;
    uint64_t afterAxSize = 1LU;
    // Blocked short.
    uint64_t specIdxAndAfterAxIterB = 0UL;
    uint64_t specIdxAndAfterAxSizeB = 0UL;
    // Only static
    const int* specIdxB = nullptr;
    const int* idxBatchSumB = nullptr;
    const int* dataBeforeAxisSumB = nullptr;
    uint64_t betweenBatchAndAxisIter = 0UL;
};

struct jitGatherKernelBase {
    void (*ker_)(const gatherJitExecArgs*) = nullptr;
    void operator()(const gatherJitExecArgs* args) const {
        assert(ker_);
        ker_(args);
    }
    explicit jitGatherKernelBase(const jGatherConfParams& jcp, uint64_t vlen, uint64_t indicesTypeSize)
        : jcp(jcp),
          vlen(vlen),
          dataElPerVec(vlen / jcp.dataTypeSize),
          idxElPerVec(vlen / indicesTypeSize),
          is_real16_to_f32((jcp.in_prec == element::f16 || jcp.in_prec == element::bf16) &&
                           jcp.out_prec == element::f32) {}
    virtual ~jitGatherKernelBase() = default;

    virtual void create_ker() = 0;
    [[nodiscard]] uint64_t getVecLen() const {
        return vlen;
    }
    [[nodiscard]] uint64_t getDataElPerVec() const {
        return dataElPerVec;
    }
    [[nodiscard]] uint64_t getIdxElPerVec() const {
        return idxElPerVec;
    }
    virtual bool isSupportedConfiguration(uint64_t afterAxisSize) = 0;

protected:
    jGatherConfParams jcp;
    uint64_t vlen = 0LU;
    uint64_t dataElPerVec = 0LU;
    uint64_t idxElPerVec = 0LU;
    static const unsigned shufMask8bitUni[16];
    static const unsigned permMask8bitA2[8];
    static const unsigned permMask8bitA5[16];
    static const unsigned shufMask16bitUni[16];
    static const unsigned permMask16bitA2[8];
    static const unsigned permMask16bitA5[16];
    static const unsigned incVec[16];

    int shortPermIdx[16]{};
    int shortBeforeAxisDiff[16]{};
    const bool is_real16_to_f32 = false;
};

template <dnnl::impl::cpu::x64::cpu_isa_t isa>
struct jitUniGatherKernel : public jitGatherKernelBase, public dnnl::impl::cpu::x64::jit_generator_t {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jitUniGatherKernel)

    explicit jitUniGatherKernel(const jGatherConfParams& jcp);

    void create_ker() override;
    void generate() override;

    bool isSupportedConfiguration(uint64_t afterAxisSize) override;

protected:
    using Vmm =
        typename dnnl::impl::utils::conditional<isa == dnnl::impl::cpu::x64::avx2, Xbyak::Ymm, Xbyak::Zmm>::type;
    using Vmask =
        typename dnnl::impl::utils::conditional<isa == dnnl::impl::cpu::x64::avx2, Xbyak::Ymm, Xbyak::Opmask>::type;
    static const uint32_t vlenXmm = dnnl::impl::cpu::x64::cpu_isa_traits_t<dnnl::impl::cpu::x64::sse41>::vlen;
    static const uint32_t indicesTypeSize = sizeof(uint32_t);
    static const uint8_t idxTypeShift = 2;
    uint8_t dataTypeShift = 0;

    // Suffix B means "In Bytes".
    // 64b registers.
    const Xbyak::Reg64& regSrc = r8;
    const Xbyak::Reg64& regDst = r9;
    const Xbyak::Reg64& regIndices = r10;
    const Xbyak::Reg64& regIdxIter = r11;
    const Xbyak::Reg64& regWorkAmount = r12;
    const Xbyak::Reg64& regSpecIdxSizeB = r13;
    const Xbyak::Reg64& regAux1 = r14;
    const Xbyak::Reg64& regAux2 = rsi;
    const Xbyak::Reg64& regBetweenBatchAndAxisIter = r15;
    const Xbyak::Reg64& regBetweenBatchAndAxisSize = rbx;
    const Xbyak::Reg64& rSpecIdxAndAfterAxIterB = regIdxIter;
    const Xbyak::Reg64& rSpecIdxAndAfterAxSizeB = regSpecIdxSizeB;

    const Xbyak::Reg64 regParams = Xbyak::Reg64(dnnl::impl::cpu::x64::abi_param_regs[0]);

    // 32b registers.
    Xbyak::Reg32 reg32IdxIter = Xbyak::Reg32(regIdxIter.getIdx());
    Xbyak::Reg32 reg32SpecIdxSizeB = Xbyak::Reg32(regSpecIdxSizeB.getIdx());
    Xbyak::Reg32 reg32BetweenBatchAndAxisSize = Xbyak::Reg32(regBetweenBatchAndAxisSize.getIdx());
    Xbyak::Reg32 reg32BetweenBatchAndAxisIter = Xbyak::Reg32(regBetweenBatchAndAxisIter.getIdx());
    Xbyak::Reg32 reg32Aux1 = Xbyak::Reg32(regAux1.getIdx());
    Xbyak::Reg32 reg32Aux2 = Xbyak::Reg32(regAux2.getIdx());

    // Masks pool. Do not use k0 with gather instruction!
    Vmask masksContainer[8] = {Vmask(0), Vmask(1), Vmask(2), Vmask(3), Vmask(4), Vmask(5), Vmask(6), Vmask(7)};
    // Auxiliary pool.
    Vmm vmmAuxContainer[12] =
        {Vmm(0), Vmm(1), Vmm(2), Vmm(3), Vmm(4), Vmm(5), Vmm(6), /*AVX5*/ Vmm(16), Vmm(17), Vmm(18), Vmm(19), Vmm(20)};
    // Common.
    Vmm vmmZeros = Vmm(7);
    Vmm vmmSrcBeforeAxisSumB = Vmm(8);
    Vmm vmmSpecIdxB = Vmm(9);
    Vmm vmmSpecIdxSizeB = Vmm(10);
    Vmm vmmAxisDim = Vmm(11);
    Vmm vmmAxisAndAfterAxisSizeB = Vmm(12);

    // Only short.
    Vmm vmmSrcAfterBatchSizeB = Vmm(13);
    Vmm vmmPermIdxMask = Vmm(14);
    Vmm& vmmBeforeAxDiffB = vmmAxisAndAfterAxisSizeB;
    // Blocked short.
    Vmm& vmmSpecIdxDiff = vmmAuxContainer[4];
    Vmm& vmmAfterAxisSize = vmmAuxContainer[5];
    Vmm vmmAfterAxisIdxB = Vmm(15);
    Vmm& vmmAfterAxisPermMask = vmmPermIdxMask;
    Vmm& vmmBeforeAxPermMask = vmmAuxContainer[6];
    // Only long.
    Vmm vmmVecLenB = Vmm(13);
    Vmm vmmIdxBatchSumB = Vmm(14);

    // XMM
    Xbyak::Xmm xmmAuxContainer[6] =
        {Xbyak::Xmm(0), Xbyak::Xmm(1), Xbyak::Xmm(2), Xbyak::Xmm(3), Xbyak::Xmm(4), Xbyak::Xmm(16)};
    Xbyak::Xmm xmmZeros = Xbyak::Xmm(vmmZeros.getIdx());
    Xbyak::Xmm xmmSrcBeforeAxisSum = Xbyak::Xmm(vmmSrcBeforeAxisSumB.getIdx());
    Xbyak::Xmm xmmSpecIdxSizeB = Xbyak::Xmm(vmmSpecIdxSizeB.getIdx());
    Xbyak::Xmm xmmSpecIdxB = Xbyak::Xmm(vmmSpecIdxB.getIdx());

    void calcSrcShiftLong(Vmm* vAuxPool, bool shiftFirst = true);
    void calcSrcShiftLongBlock(Vmm* vAuxPool, bool shiftFirst = true);
    void calcSrcShiftShort(Vmm* vAuxPool, bool shiftFirst = true);
    void calcSrcShiftShortBlock(Vmm* vAuxPool, bool shiftFirst);
    void process(bool isShortIdx, bool blocked);
    void process32b(bool isShortIdx, bool blocked);
    void process16b(bool isShortIdx, bool blocked);
    void process8b(bool isShortIdx, bool blocked);
    void shiftIdxAndGather(Vmm* vAuxPool, bool isShortIdx, bool shiftFirst, bool blocked);
    void tail(bool isShortIdx, bool shiftFirst = true, bool blocked = false);
    // Aux functions.
    void normalizeRawIndices(Vmm& rawIndices, Vmask& dstMask, Vmask& aux);
    void normWithUpperBound(Vmm& vTarget, Vmm& vMax, Vmask& kAuxMask);
    void fillRestWorkMask(Vmask& kMask,
                          Vmm& vAux,
                          const Xbyak::Reg64& rWorkRest,
                          const Xbyak::Reg64& rAux0,
                          const Xbyak::Reg64& rAux1);
    void storeVectorPart(const Xbyak::Reg64& rDst, const Xbyak::Reg64& rToStoreCounter, Vmm& vmmSrc, Vmm& vAux);
    void uniVpGatherDd(Vmm& vDst, const Xbyak::Address& srcAddr, Vmask& vMask);
    void fillVlenVector();
    void store(const Xbyak::Reg64& reg_dst, Vmm& vmmSrc);

    const unsigned* permMask8bitUni;
    const unsigned* permMask16bitUni;
    size_t dstStep = 0;
    std::unique_ptr<jit_convert_saturation_emitter> convert_emitter = nullptr;
};

}  // namespace ov::intel_cpu
