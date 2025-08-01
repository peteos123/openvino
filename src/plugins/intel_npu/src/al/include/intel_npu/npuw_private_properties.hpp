// Copyright (C) 2024 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "openvino/runtime/intel_npu/properties.hpp"

namespace ov {
namespace intel_npu {
/**
 * @brief
 * Type: bool
 * Set this option to true to utilize NPUW extension
 * Default value: false
 */
static constexpr ov::Property<bool> use_npuw{"NPU_USE_NPUW"};

namespace npuw {
/**
 * @brief
 * Type: std::string
 * Device list to try in order.
 * Possible values: Comma-separated list of devices. E.g., "NPU,GPU,CPU".
 * Default value: "NPU,CPU"
 */
static constexpr ov::Property<std::string> devices{"NPUW_DEVICES"};

/**
 * @brief
 * Type: std::string.
 * Force the specific subgraph to specific device. The device must be present in the NPUW_DEVICES list.
 * Possible values: Comma-separated "Subgraph index:OpenVINO device name" pairs, "last" keyword can be
 * used for last subgraph, e.g. "0:CPU,1:NPU,last:CPU".
 * Default value: empty.
 */
static constexpr ov::Property<std::string> submodel_device{"NPUW_SUBMODEL_DEVICE"};

/**
 * @brief
 * Type: std::string.
 * Specify bank name to utilize for a particular model.
 * Possible values: any std::string as a name.
 * Default value: empty.
 */
static constexpr ov::Property<std::string> weights_bank{"NPUW_WEIGHTS_BANK"};

/**
 * @brief
 * Type: std::string.
 * Specify device name for weights bank which is used to allocate memory.
 * Default value: "".
 */
static constexpr ov::Property<std::string> weights_bank_alloc{"NPUW_WEIGHTS_BANK_ALLOC"};

/**
 * @brief
 * Type: std::string.
 * Specify a directory where to store cached submodels.
 * Default value: empty.
 */
static constexpr ov::Property<std::string> cache_dir{"NPUW_CACHE_DIR"};

namespace partitioning {
namespace online {
/**
 * @brief
 * Type: std::string.
 * Specify which partitioning pipeline to run.
 * Possible values: "NONE", "INIT", "JUST", "REP", "REG", "COMPUTE", "SPATIAL".
 * Default value: "REG".
 */
static constexpr ov::Property<std::string> pipeline{"NPUW_ONLINE_PIPELINE"};

/**
 * @brief
 * Type: std::string.
 * Forbids operation(s) and/or predefined pattern(s) to compile and run on a
 * specified device. Only compatible with online partitioning.
 * Possible values: comma-separated list of operations slash device, e.g.
 *                  "Op:Select/NPU,P:RMSNorm/NPU".
 * Default value: empty.
 */
static constexpr ov::Property<std::string> avoid{"NPUW_ONLINE_AVOID"};

/**
 * @brief
 * Type: std::string.
 * Isolates predefined pattern(s) to compile and run separately from other isolated tags and no tags.
 * Only compatible with online partitioning.
 * Possible values: comma-separated list of layer or pattern name slash tag, e.g.
 *                  "Op:Select/compute2,P:DQMatMulGQ/compute,P:DQMatMulCW/compute,P:RMSNorm/compute".
 * Default value: empty.
 */
static constexpr ov::Property<std::string> isolate{"NPUW_ONLINE_ISOLATE"};

/**
 * @brief
 * Type: std::string.
 * Make a specific tag introduced via NPUW_ONLINE_ISOLATE a non-foldable one.
 * Only compatible with online partitioning.
 * Possible values: comma-separated list of tags, e.g.
 *                  "compute,compute2".
 * Default value: empty.
 */
static constexpr ov::Property<std::string> nofold{"NPUW_ONLINE_NO_FOLD"};

/**
 * @brief
 * Type: std::size_t.
 * Lower boundary of partition graph size the plugin can generate.
 * Used to control fusion term criteria in online partitioning.
 * Only compatible with online partitioning.
 * Possible values: Integer >= 10.
 * Default value: 10.
 */
static constexpr ov::Property<std::size_t> min_size{"NPUW_ONLINE_MIN_SIZE"};

/**
 * @brief
 * Type: std::size_t.
 * Sets the minimum number of repeating groups of the same pattern the plugin will keep in the partitioning.
 * Used to control fusion term criteria in online partitioning.
 * Only compatible with online partitioning.
 * Possible values: Integer > 0.
 * Default value: 5.
 */
static constexpr ov::Property<std::size_t> keep_blocks{"NPUW_ONLINE_KEEP_BLOCKS"};

/**
 * @brief
 * Type: std::size_t.
 * Sets the minimum group size (in layers) within the same pattern the plugin will keep in the partitioning.
 * Used to control fusion term criteria in online partitioning.
 * Only compatible with online partitioning.
 * Possible values: Integer > 0.
 * Default value: 10.
 */
static constexpr ov::Property<std::size_t> keep_block_size{"NPUW_ONLINE_KEEP_BLOCK_SIZE"};

/**
 * @brief
 * Type: std::string.
 * Dump online partitioning to the specified file.
 * This partitioning can be reused via NPUW_PLAN property later.
 * Possible values: Path to .xml file.
 * Default value: empty.
 */
static constexpr ov::Property<std::string> dump_plan{"NPUW_ONLINE_DUMP_PLAN"};
}  // namespace online

/**
 * @brief
 * Type: std::string.
 * Set plan file to use by offline partitioning.
 * Possible values: Path to .xml file.
 * Default value: empty.
 */
static constexpr ov::Property<std::string> plan{"NPUW_PLAN"};

/**
 * @brief
 * Type: bool.
 * Perform function call folding if there are repeating blocks in the graph.
 * Default value: false.
 */
static constexpr ov::Property<bool> fold{"NPUW_FOLD"};

/**
 * @brief
 * Type: bool.
 * Cut-off weights from repeating blocks, but don't do folding.
 * Decompression cut-off may still happen. Conflicts with NPUW_FOLD.
 * Default value: false.
 */
static constexpr ov::Property<bool> cwai{"NPUW_CWAI"};

/**
 * @brief
 * Type: bool.
 * Apply dynamic quantization transformations at the plugin side.
 * Default value: false.
 */
static constexpr ov::Property<bool> dyn_quant{"NPUW_DQ"};

/**
 * @brief
 * Type: bool.
 * Apply the full DQ transformation pipeline in the plugin.
 * Default value: true.
 */
static constexpr ov::Property<bool> dyn_quant_full{"NPUW_DQ_FULL"};

/**
 * @brief
 * Type: string.
 * Identify and merge parallel MatMuls over dimension(s) specified.
 * When set to YES, applies transformation for all dimensions.
 * Works with FOLD enabled only.
 * Set to NO or pass empty value to disable the option.
 * Default value: 2.
 */
static constexpr ov::Property<std::string> par_matmul_merge_dims{"NPUW_PMM"};

/**
 * @brief
 * Type: bool.
 * Add Slice before the last MatMul reducing output's dimention.
 * Default value: false.
 */
static constexpr ov::Property<bool> slice_out{"NPUW_SLICE_OUT"};

/**
 * @brief
 * Type: boolean.
 * Enable spatial execution for selected subgraphs. Requires COMPUTE isolation.
 * Default value: false
 */
static constexpr ov::Property<bool> spatial{"NPUW_SPATIAL"};

/**
 * @brief
 * Type: std::size_t.
 * Submission size for the spatial execution.
 * Default value: 128
 */
static constexpr ov::Property<std::size_t> spatial_nway{"NPUW_SPATIAL_NWAY"};

/**
 * @brief
 * Type: boolean.
 * Enable dynamic submission for spatial subgraphs. Requires SPATIAL pipeline to be selected.
 * Default value: true
 */
static constexpr ov::Property<bool> spatial_dyn{"NPUW_SPATIAL_DYN"};

/**
 * @brief
 * Type: boolean
 * Force subgraph interconnect tensors to f16 precision if those are in f32
 * Default value: false
 */
static constexpr ov::Property<bool> f16_interconnect{"NPUW_F16IC"};

/**
 * @brief
 * Type: boolean
 * When applicable, do embedding gather on host.
 * Default value: true.
 */
static constexpr ov::Property<bool> host_gather{"NPUW_HOST_GATHER"};

/**
 * @brief
 * Type: boolean
 * When applicable, do embedding gather on host but leave it quantized.
 * Default value: false.
 */
static constexpr ov::Property<bool> gather_quant{"NPUW_HOST_GATHER_QUANT"};

/**
 * @brief
 * Type: std::string.
 * Promotional data type for weights decompression. Works only with function "NPUW_FOLD"ing.
 * Possible values: "i8", "f16"
 * Default value: empty.
 */
static constexpr ov::Property<std::string> dcoff_type{"NPUW_DCOFF_TYPE"};

/**
 * @brief
 * Type: bool.
 * Include weights scaling into the decompression procedure (and exclude it from function bodies).
 * Works only with function "NPUW_FOLD"ing.
 * Default value: false.
 */
static constexpr ov::Property<bool> dcoff_with_scale{"NPUW_DCOFF_SCALE"};

/**
 * @brief
 * Type: bool.
 * Every subgraph will be turned into a function.
 * Warning: May cause performance issues!
 * Default value: false.
 */
static constexpr ov::Property<bool> funcall_for_all{"NPUW_FUNCALL_FOR_ALL"};
}  // namespace partitioning

/**
 * @brief
 * Type: bool.
 * Employ parallel subgraph compilation. Disabled by default due to instabilities.
 * Default value: false.
 */
static constexpr ov::Property<bool> parallel_compilation{"NPUW_PARALLEL_COMPILE"};

/**
 * @brief
 * Type: bool.
 * Pipeline execution of functions (repeating blocks) and their prologues
 * (e.g., where weights decompression may happen).
 * Default value: false.
 */
static constexpr ov::Property<bool> funcall_async{"NPUW_FUNCALL_ASYNC"};

/**
 * @brief
 * Type: boolean
 * Create individual infer requests for partitiongs, even repeating.
 * Default value: false.
 */
static constexpr ov::Property<bool> unfold_ireqs{"NPUW_UNFOLD_IREQS"};

namespace accuracy {
/**
 * @brief
 * Type: bool.
 * Enable accuracy check for inference to make infer requests
 * tolerant to accuracy fails
 * Default value: false.
 */
static constexpr ov::Property<bool> check{"NPUW_ACC_CHECK"};

/**
 * @brief
 * Type: double.
 * Threshold for accuracy validators, to indicate that metric
 * returns successfull comparison.
 * Possible values: Double floating-point value from 0.0 to 1.0.
 * Default value: 0.1.
 */
static constexpr ov::Property<double> threshold{"NPUW_ACC_THRESH"};

/**
 * @brief
 * Type: std::string.
 * Reference device, giving accurate results for given model(s).
 * Possible values: device name, e.g. "CPU".
 * Default value: empty.
 */
static constexpr ov::Property<std::string> reference_device{"NPUW_ACC_DEVICE"};
}  // namespace accuracy

namespace dump {
/**
 * @brief
 * Type: std::string.
 * Dump the whole model in its original form (as plugin gets it, before any partitioning is done).
 * Default value: false.
 */
static constexpr ov::Property<bool> full{"NPUW_DUMP_FULL"};

/**
 * @brief
 * Type: std::string.
 * Dump the specified subgraph(s) in OpenVINO IR form in the current directory.
 * Possible values: Comma-separated list of subgraph indices ("last" can be used
 * for dumping last subgraph without specifying it by specific index), "YES" for
 * all subgraphs, "MIN" for representative subgraph subset (all non-repeated and
 * one instance of repeated block), "NO" or just empty value to turn option off.
 * E.g. "0,1" or "0,1,last" or "YES".
 * Default value: empty.
 */
static constexpr ov::Property<std::string> subgraphs{"NPUW_DUMP_SUBS"};

/**
 * @brief
 * Type: std::string.
 * Dump subgraph on disk if a compilation failure happens.
 * Possible values: Comma-separated list of subgraph indices ("last" can be used
 * for dumping last subgraph) or "YES" for all subgraphs, "MIN" for representative
 * subgraph subset, "NO" or just empty value to turn option off. E.g. "0,1" or
 * "0,1,last" or "YES".
 * Default value: empty.
 */
static constexpr ov::Property<std::string> subgraphs_on_fail{"NPUW_DUMP_SUBS_ON_FAIL"};

/**
 * @brief
 * Type: std::string.
 * Dump input & output tensors for subgraph(s).
 * Possible values: Comma-separated list of subgraph indices ("last" can be used for
 * last subgraph) or "YES" for all subgraphs, "MIN" for representative subgraph subset,
 * "NO" or just empty value to turn option off. E.g. "0,1" or "0,1,last" or "YES".
 * Default value: empty.
 */
static constexpr ov::Property<std::string> inputs_outputs{"NPUW_DUMP_IO"};

/**
 * @brief
 * Type: std::string.
 * Dump input & output tensors for subgraph(s) for every iteration.
 * WARNING: may exhaust the disk space quickly.
 * Default value: false.
 */
static constexpr ov::Property<std::string> io_iters{"NPUW_DUMP_IO_ITERS"};
}  // namespace dump

namespace llm {
/**
 * @brief
 * Type: bool.
 * Tell NPUW that you want to pass dynamic stateful LLM model.
 * Default value: false.
 */
static constexpr ov::Property<bool> enabled{"NPUW_LLM"};

/**
 * @brief
 * FIXME: Should be removed.
 * Type: uint32_t.
 * Dimension of the batch in input tensor shape.
 * Default value: 0.
 */
static constexpr ov::Property<uint32_t> batch_dim{"NPUW_LLM_BATCH_DIM"};

/**
 * @brief
 * FIXME: Should be removed.
 * Type: uint32_t.
 * Dimension of KV-Cache size in input tensor shape.
 * Default value: 2.
 */
static constexpr ov::Property<uint32_t> seq_len_dim{"NPUW_LLM_SEQ_LEN_DIM"};

/**
 * @brief
 * Type: uint32_t.
 * Desirable max prompt length.
 * Default value: 1024.
 */
static constexpr ov::Property<uint32_t> max_prompt_len{"NPUW_LLM_MAX_PROMPT_LEN"};

/**
 * @brief
 * Type: uint32_t.
 * Desirable min response length.
 * Default value: 128.
 */
static constexpr ov::Property<uint32_t> min_response_len{"NPUW_LLM_MIN_RESPONSE_LEN"};

/**
 * @brief
 * FIXME: Should be removed.
 * Type: bool.
 * Tell NPUW to apply values transpose optimization for the model.
 * Default value: false.
 */
static constexpr ov::Property<bool> optimize_v_tensors{"NPUW_LLM_OPTIMIZE_V_TENSORS"};

/**
 * @brief
 * Type: uint64_t.
 * Prompt chunk size for chunk prefill.
 * The chunk size should be a power of two.
 * Chunk prefill feature is disabled in case the value is 0.
 * Default value: 0.
 */
static constexpr ov::Property<uint64_t> prefill_chunk_size{"NPUW_LLM_PREFILL_CHUNK_SIZE"};

/**
 * @brief
 * Type: std::string.
 * Hint for prefill stage. NPUW will use optimal configuration based on the passed preference via hint.
 * Passing this hint with "NPUW_LLM_PREFILL_CONFIG" will generate a error.
 * Possible values: "DYNAMIC", "STATIC".
 * Default value: "STATIC".
 */
static constexpr ov::Property<std::string> prefill_hint{"NPUW_LLM_PREFILL_HINT"};

/**
 * @brief
 * Type: ov::AnyMap.
 * Configuration for compilation/execution of prefill model. If specified, it will override default
 * config, prepared by NPUW specifically for this model.
 *
 * NOTE: !! Write-only !!
 */
static constexpr ov::Property<ov::AnyMap> prefill_config{"NPUW_LLM_PREFILL_CONFIG"};

/**
 * @brief
 * Type: ov::AnyMap.
 * Additional configuration for compilation/execution of prefill model. If specified, it
 * will be appended to the default configuration, prepared by NPUW.
 * For duplicated options, preference will be given to values from given map.
 *
 * NOTE: !! Write-only !!
 */
static constexpr ov::Property<ov::AnyMap> additional_prefill_config{"++NPUW_LLM_PREFILL_CONFIG"};

/**
 * @brief
 * Type: std::string.
 * Hint for generation stage. NPUW will use optimal configuration based on the passed preference via hint.
 * Passing this hint with "NPUW_LLM_GENERATE_CONFIG" will generate a error.
 * Possible values: "FAST_COMPILE", "BEST_PERF".
 * Default value: "FAST_COMPILE".
 */
static constexpr ov::Property<std::string> generate_hint{"NPUW_LLM_GENERATE_HINT"};

/**
 * @brief
 * Type: ov::AnyMap.
 * Configuration for compilation/execution of generate model. If specified, it will override default
 * config, prepared by NPUW specifically for this model.
 *
 * NOTE: !! Write-only !!
 */
static constexpr ov::Property<ov::AnyMap> generate_config{"NPUW_LLM_GENERATE_CONFIG"};

/**
 * @brief
 * Type: ov::AnyMap.
 * Configuration for compilation/execution of generate model. If specified, it
 * will be appended to the default configuration, prepared by NPUW.
 * For duplicated options, preference will be given to values from given map.
 *
 * NOTE: !! Write-only !!
 */
static constexpr ov::Property<ov::AnyMap> additional_generate_config{"++NPUW_LLM_GENERATE_CONFIG"};

/**
 * @brief
 * Type: bool.
 * Tell NPUW to separate LM head into the 3rd model, that will be shared between
 * prefill and generate.
 * Default value: true.
 */
static constexpr ov::Property<bool> shared_lm_head{"NPUW_LLM_SHARED_HEAD"};

/**
 * @brief
 * Type: ov::AnyMap.
 * Configuration for compilation/execution of shared LM head model. If specified, it will override
 * default config, prepared by NPUW specifically for this model.
 *
 * NOTE: !! Write-only !!
 */
static constexpr ov::Property<ov::AnyMap> shared_lm_head_config{"NPUW_LLM_SHARED_HEAD_CONFIG"};

/**
 * @brief
 * Type: ov::AnyMap.
 * Configuration for compilation/execution of shared LM head model. If specified, it
 * will be appended to the default configuration, prepared by NPUW.
 * For duplicated options, preference will be given to values from given map.
 *
 * NOTE: !! Write-only !!
 */
static constexpr ov::Property<ov::AnyMap> additional_shared_lm_head_config{"++NPUW_LLM_SHARED_HEAD_CONFIG"};
}  // namespace llm

}  // namespace npuw
}  // namespace intel_npu
}  // namespace ov
