// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "compiled_model.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <ostream>
#include <utility>
#include <vector>

#include "async_infer_request.h"
#include "config.h"
#include "graph.h"
#include "graph_context.h"
#include "infer_request.h"
#include "internal_properties.hpp"
#include "low_precision/low_precision.hpp"
#include "openvino/core/any.hpp"
#include "openvino/core/except.hpp"
#include "openvino/core/model.hpp"
#include "openvino/runtime/iasync_infer_request.hpp"
#include "openvino/runtime/icompiled_model.hpp"
#include "openvino/runtime/intel_cpu/properties.hpp"
#include "openvino/runtime/iplugin.hpp"
#include "openvino/runtime/isync_infer_request.hpp"
#include "openvino/runtime/properties.hpp"
#include "openvino/runtime/threading/cpu_message.hpp"
#include "openvino/runtime/threading/cpu_streams_info.hpp"
#include "openvino/runtime/threading/istreams_executor.hpp"
#include "openvino/runtime/threading/itask_executor.hpp"
#include "sub_memory_manager.hpp"
#include "utils/debug_capabilities.h"
#include "utils/general_utils.h"
#include "utils/memory_stats_dump.hpp"
#include "utils/serialize.hpp"

#if defined(OV_CPU_WITH_ACL)
#    include <arm_compute/runtime/IScheduler.h>
#    include <arm_compute/runtime/Scheduler.h>

#    include "nodes/executors/acl/acl_ie_scheduler.hpp"
#endif

using namespace ov::threading;

namespace ov::intel_cpu {

struct ImmediateSerialExecutor : public ov::threading::ITaskExecutor {
    void run(ov::threading::Task task) override {
        std::lock_guard<std::mutex> l{_mutex};
        task();
    }
    std::mutex _mutex;
};

CompiledModel::~CompiledModel() {
    if (m_has_sub_compiled_models) {
        m_sub_compiled_models.clear();
        m_sub_memory_manager->_memorys_table.clear();
    }
    auto streamsExecutor = std::dynamic_pointer_cast<ov::threading::IStreamsExecutor>(m_task_executor);
    if (streamsExecutor) {
        streamsExecutor->cpu_reset();
    }
    CPU_DEBUG_CAP_ENABLE(dumpMemoryStats(m_cfg.debugCaps, m_name, m_graphs, m_socketWeights));
}

CompiledModel::CompiledModel(const std::shared_ptr<ov::Model>& model,
                             const std::shared_ptr<const ov::IPlugin>& plugin,
                             Config cfg,
                             const bool loaded_from_cache,
                             std::shared_ptr<SubMemoryManager> sub_memory_manager)
    : ov::ICompiledModel::ICompiledModel(model, plugin),
      m_model(model),
      m_plugin(plugin),
      m_cfg{std::move(cfg)},
      m_name{model->get_name()},
      m_loaded_from_cache(loaded_from_cache),
      m_sub_memory_manager(std::move(sub_memory_manager)) {
    m_mutex = std::make_shared<std::mutex>();
    const auto& core = m_plugin->get_core();
    OPENVINO_ASSERT(core, "Unable to get API version. Core is unavailable");

    IStreamsExecutor::Config executor_config;
    if (m_cfg.exclusiveAsyncRequests) {
        // special case when all InferRequests are muxed into a single queue
        m_task_executor = m_plugin->get_executor_manager()->get_executor("CPU");
    } else {
        executor_config = m_cfg.numSubStreams > 0 ? IStreamsExecutor::Config{"CPUMainStreamExecutor",
                                                                             1,
                                                                             1,
                                                                             ov::hint::SchedulingCoreType::ANY_CORE,
                                                                             false,
                                                                             true}
                                                  : m_cfg.streamExecutorConfig;
        m_task_executor = m_plugin->get_executor_manager()->get_idle_cpu_streams_executor(executor_config);
    }
    if (0 != m_cfg.streamExecutorConfig.get_streams()) {
        m_callback_executor = m_plugin->get_executor_manager()->get_idle_cpu_streams_executor(
            IStreamsExecutor::Config{"CPUCallbackExecutor", 1, 0});
    } else {
        m_callback_executor = m_task_executor;
    }

    if (m_task_executor) {
        set_task_executor(m_task_executor);
    }
    if (m_callback_executor) {
        set_callback_executor(m_callback_executor);
    }

    m_optimized_single_stream = all_of(1, executor_config.get_streams(), executor_config.get_threads());

    int streams = std::max(1, executor_config.get_streams());
    std::vector<Task> tasks;
    tasks.resize(streams);
    m_graphs.resize(streams);
    if (executor_config.get_streams() != 0) {
        auto all_graphs_ready = [&] {
            return std::all_of(m_graphs.begin(), m_graphs.end(), [&](Graph& graph) {
                return graph.IsReady();
            });
        };
        do {
            for (auto&& task : tasks) {
                task = [this] {
#if defined(OV_CPU_WITH_ACL)
                    static std::once_flag flag_once;
                    std::call_once(flag_once, [&]() {
                        std::shared_ptr<arm_compute::IScheduler> acl_scheduler = std::make_shared<ACLScheduler>();
                        arm_compute::Scheduler::set(std::static_pointer_cast<arm_compute::IScheduler>(acl_scheduler));
                    });
#endif
                    CompiledModel::get_graph();
                };
            }
            m_task_executor->run_and_wait(tasks);
        } while (!all_graphs_ready());
    } else {
        CompiledModel::get_graph();
    }
    if (m_cfg.numSubStreams > 0) {
        m_has_sub_compiled_models = true;
        auto sub_cfg = m_cfg;
        sub_cfg.numSubStreams = 0;
        sub_cfg.enableNodeSplit = true;
        auto streams_info_table = m_cfg.streamExecutorConfig.get_streams_info_table();
        auto message = message_manager();
        m_sub_memory_manager = std::make_shared<SubMemoryManager>(m_cfg.numSubStreams);
        message->set_num_sub_streams(m_cfg.numSubStreams);
        for (int i = 0; i < m_cfg.numSubStreams; i++) {
            std::vector<std::vector<int>> sub_streams_table;
            sub_streams_table.push_back(streams_info_table[i + 1]);
            sub_streams_table[0][NUMBER_OF_STREAMS] = 1;
            sub_cfg.streamExecutorConfig = IStreamsExecutor::Config{"CPUStreamsExecutor",
                                                                    1,
                                                                    1,
                                                                    ov::hint::SchedulingCoreType::ANY_CORE,
                                                                    false,
                                                                    true,
                                                                    true,
                                                                    std::move(sub_streams_table),
                                                                    sub_cfg.streamsRankTable[i]};
            m_sub_compiled_models.push_back(
                std::make_shared<CompiledModel>(model, plugin, sub_cfg, loaded_from_cache, m_sub_memory_manager));
        }
    }
}

CompiledModel::GraphGuard::Lock CompiledModel::get_graph() const {
    int streamId = 0;
    int socketId = 0;

    size_t graph_idx = 0;
    if (m_graphs.size() > 1) {
        auto streamsExecutor = std::dynamic_pointer_cast<IStreamsExecutor>(m_task_executor);
        if (nullptr != streamsExecutor) {
            streamId = streamsExecutor->get_stream_id();
            socketId = std::max(0, streamsExecutor->get_socket_id());
        }
        graph_idx = streamId % m_graphs.size();
    }

    auto graphLock = GraphGuard::Lock(m_graphs[graph_idx]);

    if (!graphLock._graph.IsReady()) {
        std::exception_ptr exception;
        auto streamsExecutor = std::dynamic_pointer_cast<IStreamsExecutor>(m_task_executor);
        auto makeGraph = [&] {
            try {
                GraphContext::Ptr ctx;
                {
                    std::lock_guard<std::mutex> lock{*m_mutex};
                    auto isQuantizedFlag = (m_cfg.lpTransformsMode == Config::On) &&
                                           ov::pass::low_precision::LowPrecision::isFunctionQuantized(m_model);
                    ctx = std::make_shared<GraphContext>(m_cfg,
                                                         m_socketWeights[socketId],
                                                         isQuantizedFlag,
                                                         streamsExecutor,
                                                         m_sub_memory_manager);
                }

                const std::shared_ptr<const ov::Model> model = m_model;
                graphLock._graph.Init(model, ctx);
                graphLock._graph.Activate();
            } catch (...) {
                exception = std::current_exception();
            }
        };
        if (nullptr != streamsExecutor) {
            streamsExecutor->execute(makeGraph);
        } else {
            makeGraph();
        }
        if (exception) {
            std::rethrow_exception(exception);
        }
    }
    return graphLock;
}

std::shared_ptr<ov::ISyncInferRequest> CompiledModel::create_sync_infer_request() const {
    return std::make_shared<SyncInferRequest>(
        CompiledModelHolder(std::static_pointer_cast<const CompiledModel>(shared_from_this())));
}

std::shared_ptr<ov::IAsyncInferRequest> CompiledModel::create_infer_request() const {
    auto internal_request = create_sync_infer_request();
    auto async_infer_request =
        std::make_shared<AsyncInferRequest>(std::static_pointer_cast<SyncInferRequest>(internal_request),
                                            get_task_executor(),
                                            get_callback_executor(),
                                            m_optimized_single_stream);
    if (m_has_sub_compiled_models) {
        std::vector<std::shared_ptr<IAsyncInferRequest>> requests;
        requests.reserve(m_sub_compiled_models.size());
        for (const auto& model : m_sub_compiled_models) {
            requests.push_back(model->create_infer_request());
        }
        async_infer_request->setSubInferRequest(requests);
        async_infer_request->setSubInfer(true);
    }
    return async_infer_request;
}

std::shared_ptr<const ov::Model> CompiledModel::get_runtime_model() const {
    OPENVINO_ASSERT(!m_graphs.empty(), "No graph was found");

    return get_graph()._graph.dump();
}

ov::Any CompiledModel::get_property(const std::string& name) const {
    OPENVINO_ASSERT(!m_graphs.empty(), "No graph was found");

    if (name == ov::loaded_from_cache) {
        return m_loaded_from_cache;
    }

    Config engConfig = get_graph()._graph.getConfig();
    auto option = engConfig._config.find(name);
    if (option != engConfig._config.end()) {
        return option->second;
    }

    // @todo Can't we just use local copy (_cfg) instead?
    auto graphLock = get_graph();
    const auto& graph = graphLock._graph;
    const auto& config = graph.getConfig();

    auto RO_property = [](const std::string& propertyName) {
        return ov::PropertyName(propertyName, ov::PropertyMutability::RO);
    };

    if (name == ov::supported_properties) {
        std::vector<ov::PropertyName> ro_properties{
            RO_property(ov::supported_properties.name()),
            RO_property(ov::model_name.name()),
            RO_property(ov::optimal_number_of_infer_requests.name()),
            RO_property(ov::num_streams.name()),
            RO_property(ov::inference_num_threads.name()),
            RO_property(ov::enable_profiling.name()),
            RO_property(ov::hint::inference_precision.name()),
            RO_property(ov::hint::performance_mode.name()),
            RO_property(ov::hint::execution_mode.name()),
            RO_property(ov::hint::num_requests.name()),
            RO_property(ov::hint::enable_cpu_pinning.name()),
            RO_property(ov::hint::enable_cpu_reservation.name()),
            RO_property(ov::hint::scheduling_core_type.name()),
            RO_property(ov::hint::model_distribution_policy.name()),
            RO_property(ov::hint::enable_hyper_threading.name()),
            RO_property(ov::execution_devices.name()),
            RO_property(ov::intel_cpu::denormals_optimization.name()),
            RO_property(ov::log::level.name()),
            RO_property(ov::intel_cpu::sparse_weights_decompression_rate.name()),
            RO_property(ov::intel_cpu::enable_tensor_parallel.name()),
            RO_property(ov::hint::dynamic_quantization_group_size.name()),
            RO_property(ov::hint::kv_cache_precision.name()),
            RO_property(ov::key_cache_precision.name()),
            RO_property(ov::value_cache_precision.name()),
            RO_property(ov::key_cache_group_size.name()),
            RO_property(ov::value_cache_group_size.name()),
        };

        return ro_properties;
    }

    if (name == ov::model_name) {
        std::string modelName = graph.GetName();
        return decltype(ov::model_name)::value_type(modelName);
    }
    if (name == ov::optimal_number_of_infer_requests) {
        const auto streams = config.streamExecutorConfig.get_streams();
        return static_cast<decltype(ov::optimal_number_of_infer_requests)::value_type>(
            streams > 0 ? streams : 1);  // ov::optimal_number_of_infer_requests has no negative values
    }
    if (name == ov::num_streams) {
        const auto streams = config.streamExecutorConfig.get_streams();
        return decltype(ov::num_streams)::value_type(
            streams);  // ov::num_streams has special negative values (AUTO = -1, NUMA = -2)
    }
    if (name == ov::inference_num_threads) {
        const auto num_threads = config.streamExecutorConfig.get_threads();
        return static_cast<decltype(ov::inference_num_threads)::value_type>(num_threads);
    }
    if (name == ov::enable_profiling.name()) {
        const bool perfCount = config.collectPerfCounters;
        return static_cast<decltype(ov::enable_profiling)::value_type>(perfCount);
    }
    if (name == ov::hint::inference_precision) {
        return decltype(ov::hint::inference_precision)::value_type(config.inferencePrecision);
    }
    if (name == ov::hint::performance_mode) {
        return static_cast<decltype(ov::hint::performance_mode)::value_type>(config.hintPerfMode);
    }
    if (name == ov::log::level) {
        return static_cast<decltype(ov::log::level)::value_type>(config.logLevel);
    }
    if (name == ov::hint::enable_cpu_pinning.name()) {
        const bool use_pin = config.enableCpuPinning;
        return static_cast<decltype(ov::hint::enable_cpu_pinning)::value_type>(use_pin);
    }
    if (name == ov::hint::enable_cpu_reservation.name()) {
        const bool use_reserve = config.enableCpuReservation;
        return static_cast<decltype(ov::hint::enable_cpu_reservation)::value_type>(use_reserve);
    }
    if (name == ov::hint::scheduling_core_type) {
        const auto stream_mode = config.schedulingCoreType;
        return stream_mode;
    }
    if (name == ov::hint::model_distribution_policy) {
        const auto& distribution_policy = config.modelDistributionPolicy;
        return distribution_policy;
    }
    if (name == ov::hint::enable_hyper_threading.name()) {
        const bool use_ht = config.enableHyperThreading;
        return static_cast<decltype(ov::hint::enable_hyper_threading)::value_type>(use_ht);
    }
    if (name == ov::hint::execution_mode) {
        return config.executionMode;
    }
    if (name == ov::hint::num_requests) {
        return static_cast<decltype(ov::hint::num_requests)::value_type>(config.hintNumRequests);
    }
    if (name == ov::execution_devices) {
        return decltype(ov::execution_devices)::value_type{m_plugin->get_device_name()};
    }
    if (name == ov::intel_cpu::denormals_optimization) {
        return static_cast<decltype(ov::intel_cpu::denormals_optimization)::value_type>(
            config.denormalsOptMode == Config::DenormalsOptMode::DO_On);
    }
    if (name == ov::intel_cpu::sparse_weights_decompression_rate) {
        return static_cast<decltype(ov::intel_cpu::sparse_weights_decompression_rate)::value_type>(
            config.fcSparseWeiDecompressionRate);
    }
    if (name == ov::intel_cpu::enable_tensor_parallel) {
        const auto& enable_tensor_parallel = config.enableTensorParallel;
        return enable_tensor_parallel;
    }
    if (name == ov::hint::dynamic_quantization_group_size) {
        return static_cast<decltype(ov::hint::dynamic_quantization_group_size)::value_type>(
            config.fcDynamicQuantizationGroupSize);
    }
    if (name == ov::hint::kv_cache_precision) {
        return decltype(ov::hint::kv_cache_precision)::value_type(config.kvCachePrecision);
    }
    if (name == ov::key_cache_precision) {
        return decltype(ov::key_cache_precision)::value_type(config.keyCachePrecision);
    }
    if (name == ov::value_cache_precision) {
        return decltype(ov::value_cache_precision)::value_type(config.valueCachePrecision);
    }
    if (name == ov::key_cache_group_size) {
        return static_cast<decltype(ov::key_cache_group_size)::value_type>(config.keyCacheGroupSize);
    }
    if (name == ov::value_cache_group_size) {
        return static_cast<decltype(ov::value_cache_group_size)::value_type>(config.valueCacheGroupSize);
    }
    OPENVINO_THROW("Unsupported property: ", name);
}

void CompiledModel::export_model(std::ostream& modelStream) const {
    ModelSerializer serializer(modelStream, m_cfg.cacheEncrypt);
    serializer << m_model;
}

void CompiledModel::release_memory() {
    for (auto&& graph : m_graphs) {
        // try to lock mutex, since it may be already locked (e.g by an infer request)
        std::unique_lock<std::mutex> lock(graph._mutex, std::try_to_lock);
        OPENVINO_ASSERT(lock.owns_lock(),
                        "Attempt to call release_memory() on a compiled model in a busy state. Please ensure that all "
                        "infer requests are completed before releasing memory.");
        auto ctx = graph.getGraphContext();
        ctx->releaseMemory();
    }
}

}  // namespace ov::intel_cpu
