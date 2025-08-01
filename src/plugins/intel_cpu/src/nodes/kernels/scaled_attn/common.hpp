// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "openvino/core/type/element_type.hpp"
#include "openvino/core/type/float16.hpp"
#include "utils/general_utils.h"

#if defined(HAVE_AVX2) || defined(HAVE_AVX512F)
#    include "openvino/core/type/bfloat16.hpp"
#endif

#if defined(HAVE_SSE) || defined(HAVE_AVX2) || defined(HAVE_AVX512F)
#    include <immintrin.h>
#endif

#if defined(OPENVINO_ARCH_ARM64)
#    if defined(HAVE_SVE)
#        include "arm_sve.h"
#    endif
#    include "arm_neon.h"
#endif

namespace ov::Extensions::Cpu::XARCH {

// avx512/avx2 register length in byte
static constexpr size_t vec_len_avx512 = 64lu;
static constexpr size_t vec_len_avx2 = 32lu;
static constexpr size_t vec_len_neon = 16lu;
// avx512/avx2 register length in float
static constexpr size_t vec_len_f32_avx512 = vec_len_avx512 / sizeof(float);
static constexpr size_t vec_len_f32_avx2 = vec_len_avx2 / sizeof(float);
static constexpr size_t vec_len_f32_neon = vec_len_neon / sizeof(float);
static constexpr size_t vec_len_f16_neon = vec_len_neon / sizeof(ov::float16);
static constexpr size_t vec_len_epi8_avx2 = vec_len_avx2 / sizeof(int8_t);

#if defined(HAVE_SVE)
inline size_t vec_len_f32_sve() {
    static size_t len = svcntw();
    return len;
}
inline size_t vec_len_f16_sve() {
    static size_t len = svcnth();
    return len;
}
#endif

constexpr size_t get_sub_byte_multiplier(ov::element::Type type) {
    return ov::intel_cpu::any_of(type, ov::element::i4, ov::element::u4) ? 2 : 1;
}

uint8_t inline insert_half_byte(uint8_t dst, uint8_t val, bool high_half) {
    uint8_t shift = high_half ? 0 : 4;
    return dst | static_cast<uint8_t>(val << shift);
}

uint8_t inline extract_half_byte(uint8_t val, bool high_half) {
    uint8_t shift = high_half ? 0 : 4;

    return static_cast<uint8_t>((val >> shift) & 0x000F);
};

#ifdef HAVE_AVX512F
inline __m512 cvt_bf16_to_fp32(const __m256i src) {
    __m512i y = _mm512_cvtepu16_epi32(src);
    return _mm512_castsi512_ps(_mm512_slli_epi32(y, 16));
}

// load addr to __m512 reg
inline __m512 mm512_uni_loadu_ps(const float* a) {
    return _mm512_loadu_ps(a);
}

inline __m512 mm512_uni_loadu_ps(const ov::bfloat16* a) {
    auto vec_bf16 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a));
    return cvt_bf16_to_fp32(vec_bf16);
}

inline __m512 mm512_uni_loadu_ps(const ov::float16* a) {
    auto vec_f16 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a));
    return _mm512_cvtph_ps(vec_f16);
}

// load addr to __m512 reg
inline __m512 mm512_uni_loadu_tail_ps(const float* a, size_t count) {
    __mmask16 mask = (1 << count) - 1;
    return _mm512_maskz_loadu_ps(mask, a);
}

inline __m512 mm512_uni_loadu_tail_ps(const ov::bfloat16* a, size_t count) {
    auto mask = (1 << count) - 1;
    auto bf16_vec = _mm256_maskz_loadu_epi16(mask, a);
    return cvt_bf16_to_fp32(bf16_vec);
}

inline __m512 mm512_uni_loadu_tail_ps(const ov::float16* a, size_t count) {
    auto mask = (1 << count) - 1;
    auto f16_vec = _mm256_maskz_loadu_epi16(mask, a);
    return _mm512_cvtph_ps(f16_vec);
}

// store __m512 reg to addr
inline void mm512_uni_storeu_ps(float* a, __m512 v) {
    _mm512_storeu_ps(a, v);
}
inline void mm512_uni_storeu_ps(ov::bfloat16* addr, __m512 xps) {
    __m512i xpi32 = _mm512_castps_si512(xps);
    __m512i nan = _mm512_set1_epi32(0xffff);
    auto mask = _mm512_cmp_ps_mask(xps, xps, _CMP_ORD_Q);
    __m512i ones = _mm512_set1_epi32(0x1);
    __m512i vec_bias = _mm512_set1_epi32(0x7fff);
    auto x = _mm512_and_si512(_mm512_srli_epi32(xpi32, 16), ones);  // LSB = x[16]
    x = _mm512_add_epi32(x, vec_bias);                              // rounding_bias = 0x7fff + LSB
    x = _mm512_srli_epi32(_mm512_add_epi32(x, xpi32), 16);          // x = (x + rounding_bias) >> 16;
    x = _mm512_mask_blend_epi32(mask, nan, x);                      // Check NaN before converting back to bf16
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(addr), _mm512_cvtepi32_epi16(x));
}

inline void mm512_uni_storeu_ps(ov::float16* addr, __m512 v) {
    __m256i vec_f16 = _mm512_cvtps_ph(v, 0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(addr), vec_f16);
}

// store __m512 reg to addr
inline void mm512_uni_mask_storeu_ps(ov::bfloat16* addr, __mmask16 mask_addr, __m512 xps) {
    __m512i xpi32 = _mm512_castps_si512(xps);
    __m512i nan = _mm512_set1_epi32(0xffff);
    auto mask = _mm512_cmp_ps_mask(xps, xps, _CMP_ORD_Q);
    __m512i ones = _mm512_set1_epi32(0x1);
    __m512i vec_bias = _mm512_set1_epi32(0x7fff);
    auto x = _mm512_and_si512(_mm512_srli_epi32(xpi32, 16), ones);  // LSB = x[16]
    x = _mm512_add_epi32(x, vec_bias);                              // rounding_bias = 0x7fff + LSB
    x = _mm512_srli_epi32(_mm512_add_epi32(x, xpi32), 16);          // x = (x + rounding_bias) >> 16;
    x = _mm512_mask_blend_epi32(mask, nan, x);                      // Check NaN before converting back to bf16
    _mm512_mask_cvtepi32_storeu_epi16(addr, mask_addr, x);
}

inline void mm512_uni_storeu_tail_ps(float* addr, __m512 v, size_t count) {
    __mmask16 mask_addr = (1 << count) - 1;
    _mm512_mask_storeu_ps(addr, mask_addr, v);
}

inline void mm512_uni_storeu_tail_ps(ov::bfloat16* addr, __m512 v, size_t count) {
    __mmask16 mask_addr = (1 << count) - 1;
    __m512i xpi32 = _mm512_castps_si512(v);
    __m512i nan = _mm512_set1_epi32(0xffff);
    auto mask = _mm512_cmp_ps_mask(v, v, _CMP_ORD_Q);
    __m512i ones = _mm512_set1_epi32(0x1);
    __m512i vec_bias = _mm512_set1_epi32(0x7fff);
    auto x = _mm512_and_si512(_mm512_srli_epi32(xpi32, 16), ones);  // LSB = x[16]
    x = _mm512_add_epi32(x, vec_bias);                              // rounding_bias = 0x7fff + LSB
    x = _mm512_srli_epi32(_mm512_add_epi32(x, xpi32), 16);          // x = (x + rounding_bias) >> 16;
    x = _mm512_mask_blend_epi32(mask, nan, x);                      // Check NaN before converting back to bf16
    _mm512_mask_cvtepi32_storeu_epi16(addr, mask_addr, x);
}

inline void mm512_uni_storeu_tail_ps(ov::float16* addr, __m512 v, size_t count) {
    __mmask16 mask_addr = (1 << count) - 1;
    __m256i vec_f16 = _mm512_cvtps_ph(v, 0);
    _mm256_mask_storeu_epi16(reinterpret_cast<__m256i*>(addr), mask_addr, vec_f16);
}

inline void mm512_loadu_u4_to_f32(uint8_t* src_data, __m512& first_half, __m512& second_half) {
    auto data = _mm_loadu_si128(reinterpret_cast<__m128i*>(src_data));
    auto v_i32 = _mm512_cvtepu8_epi32(data);

    auto v_512_low_half = _mm512_srli_epi32(v_i32, 4);
    auto v_f32_low_half = _mm512_cvtepi32_ps(v_512_low_half);

    auto mask = _mm512_set1_epi32(0x0F);
    auto v_512_high_half = _mm512_and_si512(v_i32, mask);
    auto v_f32_high_half = _mm512_cvtepi32_ps(v_512_high_half);
    __m512i idx1 = _mm512_set_epi32(23, 7, 22, 6, 21, 5, 20, 4, 19, 3, 18, 2, 17, 1, 16, 0);
    __m512i idx2 = _mm512_set_epi32(31, 15, 30, 14, 29, 13, 28, 12, 27, 11, 26, 10, 25, 9, 24, 8);
    first_half = _mm512_permutex2var_ps(v_f32_low_half, idx1, v_f32_high_half);
    second_half = _mm512_permutex2var_ps(v_f32_low_half, idx2, v_f32_high_half);
}

inline void mm512_storeu_u4(uint8_t* dst_data, __m512i& v0, __m512i& v1) {
    __m512i idx1 = _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0);
    __m512i idx2 = _mm512_set_epi32(31, 29, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9, 7, 5, 3, 1);
    auto first_half = _mm512_permutex2var_epi32(v0, idx1, v1);
    auto second_half = _mm512_permutex2var_epi32(v0, idx2, v1);
    first_half = _mm512_slli_epi32(first_half, 4);
    auto mask = _mm512_set1_epi32(0x0F);
    second_half = _mm512_and_epi32(second_half, mask);
    auto combined = _mm512_or_epi32(first_half, second_half);
    _mm512_mask_cvtepi32_storeu_epi8(dst_data, 0xffff, combined);
}

#endif

#ifdef HAVE_AVX2
inline __m128i get_8bit_tail_mask_for_16bit_elts(size_t num_16bit_tail_elts) {
    // num_tail_elts may take from 0 to 8
    static int8_t masks[9][16] = {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                  {-1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                  {-1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                  {-1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                  {-1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0},
                                  {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0},
                                  {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0},
                                  {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 0},
                                  {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
    return _mm_loadu_si128(reinterpret_cast<__m128i*>(masks[num_16bit_tail_elts]));
}
inline __m256i get_mask(int N7) {
    static int32_t masks[9][8] = {{0, 0, 0, 0, 0, 0, 0, 0},
                                  {-1, 0, 0, 0, 0, 0, 0, 0},
                                  {-1, -1, 0, 0, 0, 0, 0, 0},
                                  {-1, -1, -1, 0, 0, 0, 0, 0},
                                  {-1, -1, -1, -1, 0, 0, 0, 0},
                                  {-1, -1, -1, -1, -1, 0, 0, 0},
                                  {-1, -1, -1, -1, -1, -1, 0, 0},
                                  {-1, -1, -1, -1, -1, -1, -1, 0},
                                  {-1, -1, -1, -1, -1, -1, -1, -1}};
    return _mm256_loadu_si256(reinterpret_cast<__m256i*>(masks[N7]));
}

// load addr to __m256 reg
inline __m256 mm256_uni_loadu_ps(const float* a) {
    return _mm256_loadu_ps(a);
}

inline __m256 mm256_uni_loadu_ps(const ov::bfloat16* a) {
    auto vec_bf16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
    auto o = _mm256_castsi256_ps(_mm256_slli_epi32(_mm256_cvtepu16_epi32(vec_bf16), 16));
    return o;
}

inline __m256 mm256_uni_loadu_ps(const ov::float16* a) {
    auto vec_f16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
    auto o = _mm256_cvtph_ps(vec_f16);
    return o;
}

// load addr tail to __m256 reg
inline __m256 mm256_uni_loadu_tail_ps(const float* a, const size_t count) {
    auto mask = get_mask(count);
    return _mm256_maskload_ps(a, mask);
}

inline __m256 mm256_uni_loadu_tail_ps(const ov::bfloat16* a, const size_t count) {
    assert("AVX2 version of bfloat16 tail load is just for compilation pass");
    ov::bfloat16 tmp_values[8] = {0};
    std::memcpy(tmp_values, a, count * sizeof(ov::bfloat16));
    return mm256_uni_loadu_ps(tmp_values);
}

inline __m256 mm256_uni_loadu_tail_ps(const ov::float16* a, const size_t count) {
    ov::float16 tmp_values[8] = {0};
    std::memcpy(tmp_values, a, count * sizeof(ov::float16));
    return mm256_uni_loadu_ps(tmp_values);
}

// store __m256 reg to addr
inline void mm256_uni_storeu_ps(float* a, __m256 v) {
    _mm256_storeu_ps(a, v);
}

inline __m128i __convert_avx2_packed_float_to_packed_ov_bfloat16(__m256 xps) {
    __m256i xpi32 = _mm256_castps_si256(xps);
    __m256i nan = _mm256_set1_epi32(0xffff);
    __m256i mask = _mm256_castps_si256(_mm256_cmp_ps(xps, xps, _CMP_ORD_Q));
    __m256i ones = _mm256_set1_epi32(0x1);
    __m256i vec_bias = _mm256_set1_epi32(0x7fff);
    auto x = _mm256_and_si256(_mm256_srli_epi32(xpi32, 16), ones);  // LSB = x[16]
    x = _mm256_add_epi32(x, vec_bias);                              // rounding_bias = 0x7fff + LSB
    x = _mm256_srli_epi32(_mm256_add_epi32(x, xpi32), 16);          // x = (x + rounding_bias) >> 16;
    x = _mm256_blendv_epi8(nan, x, mask);                           // Check NaN before converting back to bf16
    x = _mm256_packus_epi32(x, x);
    x = _mm256_permute4x64_epi64(x, 0xd8);
    __m128i bf16_o = _mm256_extractf128_si256(x, 0);
    return bf16_o;
}

inline void mm256_uni_storeu_ps(ov::bfloat16* addr, __m256 xps) {
    __m128i bf16_o = __convert_avx2_packed_float_to_packed_ov_bfloat16(xps);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(addr), bf16_o);
}

inline void mm256_uni_storeu_ps(ov::float16* a, __m256 v) {
    __m128i vec_f16 = _mm256_cvtps_ph(v, 0);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(a), vec_f16);
}

// store __m256 to addr
inline void mm256_uni_storeu_tail_ps(float* addr, __m256 v, size_t count) {
    auto mask = get_mask(count);
    return _mm256_maskstore_ps(addr, mask, v);
}

inline void mm256_uni_storeu_tail_ps(ov::float16* addr, __m256 v, size_t count) {
    auto mask = get_8bit_tail_mask_for_16bit_elts(count);
    __m128i vec_f16 = _mm256_cvtps_ph(v, 0);
    return _mm_maskmoveu_si128(vec_f16, mask, reinterpret_cast<char*>(addr));
}

inline void mm256_uni_storeu_tail_ps(ov::bfloat16* addr, __m256 v, size_t count) {
    auto mask = get_8bit_tail_mask_for_16bit_elts(count);
    __m128i bf16_o = __convert_avx2_packed_float_to_packed_ov_bfloat16(v);
    return _mm_maskmoveu_si128(bf16_o, mask, reinterpret_cast<char*>(addr));
}

inline void mm256_loadu_u4_to_f32(uint8_t* src, __m256& first_half, __m256& second_half) {
    auto data = _mm_loadl_epi64(reinterpret_cast<__m128i*>(src));

    auto v_i32 = _mm256_cvtepu8_epi32(data);
    auto v_256_low_half = _mm256_srli_epi32(v_i32, 4);
    auto v_f32_low_half = _mm256_cvtepi32_ps(v_256_low_half);

    auto mask = _mm256_set1_epi32(0x0F);
    auto v_256_high_half = _mm256_and_si256(v_i32, mask);
    auto v_f32_high_half = _mm256_cvtepi32_ps(v_256_high_half);

    // 0,2,4,6,8,10,12,14 | 1,3,5,7,9,11,13,15
    //         _mm256_permute2f128_ps
    // 0,2,4,6,1,3,5,7    | 8,10,12,14,9,11,13,15
    //         _mm256_permutevar8x32_ps
    // 0,1,2,3,4,5,6,7    | 8,9,10,11,12,13,14,15
    first_half = _mm256_permute2f128_ps(v_f32_low_half, v_f32_high_half, 0x20);
    auto idx1 = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);
    first_half = _mm256_permutevar8x32_ps(first_half, idx1);
    second_half = _mm256_permute2f128_ps(v_f32_low_half, v_f32_high_half, 0x31);
    second_half = _mm256_permutevar8x32_ps(second_half, idx1);
}

inline void mm256_storeu_u4(uint8_t* dst_data, __m256i& v0_i32, __m256i& v1_i32) {
    auto idx1 = _mm256_set_epi32(7, 5, 3, 1, 6, 4, 2, 0);
    v0_i32 = _mm256_permutevar8x32_epi32(v0_i32, idx1);
    v1_i32 = _mm256_permutevar8x32_epi32(v1_i32, idx1);
    //    0,1,2,3,4,5,6,7 | 8,9,10,11,12,13,14,15
    //       _mm256_permutevar8x32_epi32
    //    0,2,4,6,1,3,5,7 | 8,10,12,14,9,11,13,15
    //       _mm256_permute2x128_si256
    // 0,2,4,6,8,10,12,14 | 1,3,5,7,9,11,13,15
    //          shift + mask + or
    //     [0,1],[2,3], ..., [12,13], [14,15]
    auto first_half = _mm256_permute2x128_si256(v0_i32, v1_i32, 0x20);
    auto second_half = _mm256_permute2x128_si256(v0_i32, v1_i32, 0x31);
    first_half = _mm256_slli_epi32(first_half, 4);
    auto mask = _mm256_set1_epi32(0x0F);
    second_half = _mm256_and_si256(second_half, mask);
    auto combined = _mm256_or_si256(first_half, second_half);

    auto high4 = _mm256_extractf128_si256(combined, 1);
    auto low4 = _mm256_castsi256_si128(combined);
    // ignore sign bit for u4 case
    auto packed = _mm_packus_epi32(low4, high4);
    packed = _mm_packus_epi16(packed, packed);
    _mm_storel_epi64(reinterpret_cast<__m128i*>(dst_data), packed);
}

inline void hsum(__m256& x) {
    __m256 y;                             // x:  0 1 2 3   4 5 6 7
    y = _mm256_permute_ps(x, 0x39);       // y:  1 2 3 0   5 6 7 4
    x = _mm256_add_ps(x, y);              // X:  01 12 23 30  45 56 67 74
    y = _mm256_permute_ps(x, 0x4e);       // y:  23 30 01 12  67 74 45 56
    x = _mm256_add_ps(x, y);              // x: 0123 x x x   4567 x x x
    y = _mm256_permute2f128_ps(x, x, 1);  // y: 4567 x x x  0123 x x x
    x = _mm256_add_ps(x, y);              // x: 01234567 x x x x x x x
}
inline void hmax(__m256& x) {
    __m256 y;                             // x:  0 1 2 3   4 5 6 7
    y = _mm256_permute_ps(x, 0x39);       // y:  1 2 3 0   5 6 7 4
    x = _mm256_max_ps(x, y);              // X:  01 12 23 30  45 56 67 74
    y = _mm256_permute_ps(x, 0x4e);       // y:  23 30 01 12  67 74 45 56
    x = _mm256_max_ps(x, y);              // x: 0123 x x x   4567 x x x
    y = _mm256_permute2f128_ps(x, x, 1);  // y: 4567 x x x  0123 x x x
    x = _mm256_max_ps(x, y);              // x: 01234567 x x x x x x x
}
inline void hmin(__m256& x) {
    __m256 y;                             // x:  0 1 2 3   4 5 6 7
    y = _mm256_permute_ps(x, 0x39);       // y:  1 2 3 0   5 6 7 4
    x = _mm256_min_ps(x, y);              // X:  01 12 23 30  45 56 67 74
    y = _mm256_permute_ps(x, 0x4e);       // y:  23 30 01 12  67 74 45 56
    x = _mm256_min_ps(x, y);              // x: 0123 x x x   4567 x x x
    y = _mm256_permute2f128_ps(x, x, 1);  // y: 4567 x x x  0123 x x x
    x = _mm256_min_ps(x, y);              // x: 01234567 x x x x x x x
}
#endif

#ifdef OPENVINO_ARCH_ARM64
#    if defined(HAVE_SVE)
inline svfloat32_t exp_ps_sve(svbool_t& pg, svfloat32_t& src) {
    // Constants
    const auto log2_e = svdup_n_f32(1.4426950409f);
    const auto ln2 = svdup_n_f32(0.6931473921f);
    const auto half_ln2_sq = svdup_n_f32(0.2413862043f);
    const auto not_mask17 = svdup_n_u32(~((1U << 17) - 1));
    const auto one = svdup_n_f32(1.0f);

    // Algorithm starts here
    svfloat32_t t0 = svmul_f32_z(pg, src, log2_e);  // y = x * log2(e)
    svfloat32_t t1 = svrintm_f32_z(pg, t0);         // rount to int (float)
    svint32_t t2 = svcvt_s32_f32_z(pg, t1);         // n

    t1 = svsub_f32_z(pg, t0, t1);   // a = y - floor(y)
    t1 = svadd_f32_z(pg, t1, one);  // b = a + 1

    svuint32_t t3 = svlsr_n_u32_z(pg, svreinterpret_u32_f32(t1), 17);  // v = b >> 17 (u32)
    svfloat32_t t4 = svexpa_f32(t3);                                   // c = fexpa(v)
    t4 = svscale_f32_z(pg, t4, t2);                                    // fexpa(v) * 2^(n)

    // and_(t2.d, t1.d, not_mask17.d)
    svfloat32_t t5 = svreinterpret_f32_u32(svand_u32_z(pg, svreinterpret_u32_f32(t1), not_mask17));
    t5 = svsub_f32_z(pg, t1, t5);                // z
    t0 = svmla_f32_z(pg, ln2, t5, half_ln2_sq);  // ln2 + half_ln2_sq * z
    t0 = svmla_f32_z(pg, one, t5, t0);           // 1 + (ln2 * z) + (half_ln2_sq * z * z)
    t0 = svmul_f32_z(pg, t0, t4);                // Final result

    return t0;
}
inline svfloat32_t exp_ps_sve_legacy(svbool_t& pg, svfloat32_t& src) {
    const auto c1 = svreinterpret_f32_u32(svdup_n_u32(0x3f7ffff6));
    const auto c2 = svreinterpret_f32_u32(svdup_n_u32(0x3efffedb));
    const auto c3 = svreinterpret_f32_u32(svdup_n_u32(0x3e2aaf33));
    const auto c4 = svreinterpret_f32_u32(svdup_n_u32(0x3d2b9f17));
    const auto c5 = svreinterpret_f32_u32(svdup_n_u32(0x3c072010));

    const auto shift = svreinterpret_f32_u32(svdup_n_u32(0x4b00007f));  // 2^23 + 127 = 0x1.0000fep23f
    const auto one = svdup_n_f32(1.0f);                                 // 1
    const auto two = svdup_n_f32(2.0f);                                 // 2
    const auto inv_ln2 = svreinterpret_f32_u32(svdup_n_u32(0x3fb8aa3b));
    const auto neg_ln2_hi = svreinterpret_f32_u32(svdup_n_u32(0xbf317200));
    const auto neg_ln2_lo = svreinterpret_f32_u32(svdup_n_u32(0xb5bfbe8e));

    const auto inf = svdup_n_f32(std::numeric_limits<float>::infinity());
    const auto max_input = svdup_n_f32(88.37f);  // Approximately ln(2^127.5)
    const auto zero = svdup_n_f32(0.F);
    const auto min_input = svdup_n_f32(-86.64f);  // Approximately ln(2^-125)

    const auto z = svmla_f32_z(pg, shift, src, inv_ln2);
    auto n = svsub_f32_z(pg, z, shift);
    n = svsub_f32_z(pg, n, one);
    const auto scale = svreinterpret_f32_u32(svlsl_n_u32_z(pg, svreinterpret_u32_f32(z), 23));  // 2^n

    const auto r_hi = svmla_f32_z(pg, src, n, neg_ln2_hi);
    const auto r = svmla_f32_z(pg, r_hi, n, neg_ln2_lo);
    const auto r2 = svmul_f32_z(pg, r, r);

    const auto p1 = svmul_f32_z(pg, c1, r);
    const auto p23 = svmla_f32_z(pg, c2, c3, r);
    const auto p45 = svmla_f32_z(pg, c4, c5, r);
    const auto p2345 = svmla_f32_z(pg, p23, p45, r2);
    const auto p12345 = svmla_f32_z(pg, p1, p2345, r2);

    auto poly = svmla_f32_z(pg, scale, p12345, scale);
    poly = svmul_f32_z(pg, poly, two);

    poly = svsel_f32(svcmplt_f32(pg, src, min_input), zero, poly);
    poly = svsel_f32(svcmpgt_f32(pg, src, max_input), inf, poly);

    return poly;
}
#    endif
inline float32x4_t exp_ps_neon_f32(const float32x4_t& src) {
    const auto c1 = vreinterpretq_f32_u32(vdupq_n_u32(0x3f7ffff6));
    const auto c2 = vreinterpretq_f32_u32(vdupq_n_u32(0x3efffedb));
    const auto c3 = vreinterpretq_f32_u32(vdupq_n_u32(0x3e2aaf33));
    const auto c4 = vreinterpretq_f32_u32(vdupq_n_u32(0x3d2b9f17));
    const auto c5 = vreinterpretq_f32_u32(vdupq_n_u32(0x3c072010));

    const auto shift = vreinterpretq_f32_u32(vdupq_n_u32(0x4b00007f));  // 2^23 + 127 = 0x1.0000fep23f
    const auto one = vdupq_n_f32(1.0f);                                 // 1
    const auto two = vdupq_n_f32(2.0f);                                 // 2
    const auto inv_ln2 = vreinterpretq_f32_u32(vdupq_n_u32(0x3fb8aa3b));
    const auto neg_ln2_hi = vreinterpretq_f32_u32(vdupq_n_u32(0xbf317200));
    const auto neg_ln2_lo = vreinterpretq_f32_u32(vdupq_n_u32(0xb5bfbe8e));

    const auto inf = vdupq_n_f32(std::numeric_limits<float>::infinity());
    const auto max_input = vdupq_n_f32(88.37f);  // Approximately ln(2^127.5)
    const auto zero = vdupq_n_f32(0.F);
    const auto min_input = vdupq_n_f32(-86.64f);  // Approximately ln(2^-125)

    const auto z = vmlaq_f32(shift, src, inv_ln2);
    auto n = z - shift;
    n = vsubq_f32(n, one);
    const auto scale = vreinterpretq_f32_u32(vreinterpretq_u32_f32(z) << 23);  // 2^n

    const auto r_hi = vfmaq_f32(src, n, neg_ln2_hi);
    const auto r = vfmaq_f32(r_hi, n, neg_ln2_lo);

    const auto r2 = r * r;

    const auto p1 = c1 * r;
    const auto p23 = vfmaq_f32(c2, c3, r);
    const auto p45 = vfmaq_f32(c4, c5, r);
    const auto p2345 = vfmaq_f32(p23, p45, r2);
    const auto p12345 = vfmaq_f32(p1, p2345, r2);

    auto poly = vfmaq_f32(scale, p12345, scale);
    poly = vmulq_f32(poly, two);

    poly = vbslq_f32(vcltq_f32(src, min_input), zero, poly);
    poly = vbslq_f32(vcgtq_f32(src, max_input), inf, poly);

    return poly;
}
inline float32x4_t __vld1q_f32(const ov::bfloat16* a) {
    uint16x4_t vec_bf16 = vld1_u16(reinterpret_cast<const uint16_t*>(a));

    float32x4_t vec_f32 = vcvtq_f32_u32(vmovl_u16(vec_bf16));
    return vec_f32;
}
inline float32x4_t __vld1q_f32(const float* a) {
    return vld1q_f32(a);
}
inline float32x4_t __vld1q_f32(const ov::float16* a) {
    auto _a = reinterpret_cast<const float16_t*>(a);
    return vcvt_f32_f16(vld1_f16(_a));
}
inline void __vst1q_f32(float* a, float32x4_t b) {
    vst1q_f32(a, b);
}
inline void __vst1q_f32(ov::float16* a, float32x4_t b) {
    float16x4_t v_f16 = vcvt_f16_f32(b);
    vst1_f16(reinterpret_cast<float16_t*>(a), v_f16);
}
inline void __vst1q_f32(ov::bfloat16* a, float32x4_t b) {
    uint32x4_t v_int32 = vreinterpretq_u32_f32(b);
    uint16x4_t v_bf16 = vshrn_n_u32(v_int32, 16);

    vst1_u16(reinterpret_cast<uint16_t*>(a), v_bf16);
}

#endif

#if defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC)
#    if defined(HAVE_SVE)
inline svfloat16_t exp_ps_sve_f16(svbool_t& pg, svfloat16_t& src) {
    svbool_t pg_f32 = svtrn1_b16(pg, svpfalse());

    // Extract lower and upper halves of src into two separate vecs and convert
    svfloat16_t zero = svdup_n_f16(0.0);
    svfloat16_t low_f16 = svtrn1_f16(src, zero);
    svfloat16_t high_f16 = svtrn2_f16(src, zero);
    svfloat32_t low_f32 = svcvt_f32_f16_z(pg, low_f16);
    svfloat32_t high_f32 = svcvt_f32_f16_z(pg, high_f16);

    // Perform exp and convert back to f16
    svfloat32_t low_exp_f32 = exp_ps_sve(pg_f32, low_f32);
    svfloat32_t high_exp_f32 = exp_ps_sve(pg_f32, high_f32);
    svfloat16_t low_exp_f16 = svcvt_f16_f32_z(pg_f32, low_exp_f32);
    svfloat16_t high_exp_f16 = svcvt_f16_f32_z(pg_f32, high_exp_f32);

    // Interleave both to get final result
    svfloat16_t res = svtrn1_f16(low_exp_f16, high_exp_f16);
    return res;
}
#    else
inline float16x8_t exp_ps_neon_f16(float16x8_t x) {
    const float32x4_t x_high = vcvt_f32_f16(vget_high_f16(x));
    const float32x4_t x_low = vcvt_f32_f16(vget_low_f16(x));

    // We use f32 to maintain accuracy
    const float16x8_t res = vcombine_f16(vcvt_f16_f32(exp_ps_neon_f32(x_low)), vcvt_f16_f32(exp_ps_neon_f32(x_high)));
    return res;
}
#    endif
inline float16_t hsum(float16x8_t vec) {
    float16x4_t sum1 = vpadd_f16(vget_low_f16(vec), vget_high_f16(vec));
    float16x4_t sum2 = vpadd_f16(sum1, sum1);
    float16x4_t sum3 = vpadd_f16(sum2, sum2);
    return vget_lane_f16(sum3, 0);
}
#endif

template <typename TA, typename TB>
void cvt_copy(TA* a, TB* b, size_t m, size_t n, size_t src_stride, size_t dst_stride) {
    for (size_t j = 0; j < m; j++) {
        size_t i = 0;
#if defined(HAVE_AVX512F)
        for (; i + vec_len_f32_avx512 <= n; i += vec_len_f32_avx512) {
            auto vb = mm512_uni_loadu_ps(b + i + j * src_stride);
            mm512_uni_storeu_ps(a + i + j * dst_stride, vb);
        }
#elif defined(HAVE_AVX2)
        for (; i + vec_len_f32_avx2 <= n; i += vec_len_f32_avx2) {
            auto vb = mm256_uni_loadu_ps(b + i + j * src_stride);
            mm256_uni_storeu_ps(a + i + j * dst_stride, vb);
        }
#endif
        for (; i < n; i++) {
            a[i + j * dst_stride] = b[i + j * src_stride];
        }
    }
}

template <typename TDST, typename TA, typename TB>
void cvt_add(TDST* dst, TA* a, TB* b, size_t m, size_t n, size_t a_stride, size_t b_stride, size_t dst_stride) {
    for (size_t j = 0; j < m; j++) {
        size_t i = 0;
#if defined(HAVE_AVX512F)
        for (; i + vec_len_f32_avx512 <= n; i += vec_len_f32_avx512) {
            auto va = mm512_uni_loadu_ps(a + i + j * a_stride);
            auto vb = mm512_uni_loadu_ps(b + i + j * b_stride);
            auto vd = _mm512_add_ps(va, vb);
            mm512_uni_storeu_ps(dst + i + j * dst_stride, vd);
        }
#elif defined(HAVE_AVX2)
        for (; i + vec_len_f32_avx2 <= n; i += vec_len_f32_avx2) {
            auto va = mm256_uni_loadu_ps(a + i + j * a_stride);
            auto vb = mm256_uni_loadu_ps(b + i + j * b_stride);
            auto vd = _mm256_add_ps(va, vb);
            mm256_uni_storeu_ps(dst + i + j * dst_stride, vd);
        }
#endif
        for (; i < n; i++) {
            dst[i + j * dst_stride] = a[i + j * a_stride] + b[i + j * b_stride];
        }
    }
}

}  // namespace ov::Extensions::Cpu::XARCH
