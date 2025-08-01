// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cstddef>
#include <memory>
#include <oneapi/dnnl/dnnl_common.hpp>
#include <string>

#include "cpu_shape.h"
#include "cpu_types.h"
#include "graph_context.h"
#include "kernels/x64/jit_kernel_base.hpp"
#include "kernels/x64/non_max_suppression.hpp"
#include "node.h"
#include "nodes/kernels/x64/jit_kernel_base.hpp"
#include "openvino/core/node.hpp"
#include "openvino/core/shape.hpp"

namespace ov::intel_cpu::node {

enum NMSCandidateStatus : uint8_t { SUPPRESSED = 0, SELECTED = 1, UPDATED = 2 };

class NonMaxSuppression : public Node {
public:
    NonMaxSuppression(const std::shared_ptr<ov::Node>& op, const GraphContext::CPtr& context);

    void getSupportedDescriptors() override {};

    void initSupportedPrimitiveDescriptors() override;

    void execute(const dnnl::stream& strm) override;

    void executeDynamicImpl(const dnnl::stream& strm) override;

    [[nodiscard]] bool created() const override;

    static bool isSupportedOperation(const std::shared_ptr<const ov::Node>& op, std::string& errorMessage) noexcept;

    struct FilteredBox {
        float score;
        int batch_index;
        int class_index;
        int box_index;
        FilteredBox() = default;
        FilteredBox(float _score, int _batch_index, int _class_index, int _box_index)
            : score(_score),
              batch_index(_batch_index),
              class_index(_class_index),
              box_index(_box_index) {}
    };

    struct boxInfo {
        float score;
        int idx;
        int suppress_begin_index;
    };

    [[nodiscard]] bool neverExecute() const override;
    [[nodiscard]] bool isExecutable() const override;

    [[nodiscard]] bool needShapeInfer() const override {
        return false;
    }

    void prepareParams() override;

    struct Point2D {
        float x, y;
        explicit Point2D(const float px = 0.F, const float py = 0.F) : x(px), y(py) {}
        Point2D operator+(const Point2D& p) const {
            return Point2D(x + p.x, y + p.y);
        }
        Point2D& operator+=(const Point2D& p) {
            x += p.x;
            y += p.y;
            return *this;
        }
        Point2D operator-(const Point2D& p) const {
            return Point2D(x - p.x, y - p.y);
        }
        Point2D operator*(const float coeff) const {
            return Point2D(x * coeff, y * coeff);
        }
    };

private:
    // input
    enum : uint8_t {
        NMS_BOXES,
        NMS_SCORES,
        NMS_MAX_OUTPUT_BOXES_PER_CLASS,
        NMS_IOU_THRESHOLD,
        NMS_SCORE_THRESHOLD,
        NMS_SOFT_NMS_SIGMA,
    };

    // output
    enum : uint8_t { NMS_SELECTED_INDICES, NMS_SELECTED_SCORES, NMS_VALID_OUTPUTS };

    float intersectionOverUnion(const float* boxesI, const float* boxesJ);

    float rotatedIntersectionOverUnion(const Point2D (&vertices_0)[4], float area_0, const float* box_1) const;

    void nmsWithSoftSigma(const float* boxes,
                          const float* scores,
                          const VectorDims& boxesStrides,
                          const VectorDims& scoresStrides,
                          std::vector<FilteredBox>& filtBoxes);

    void nmsWithoutSoftSigma(const float* boxes,
                             const float* scores,
                             const VectorDims& boxesStrides,
                             const VectorDims& scoresStrides,
                             std::vector<FilteredBox>& filtBoxes);

    void nmsRotated(const float* boxes,
                    const float* scores,
                    const VectorDims& boxes_strides,
                    const VectorDims& scores_strides,
                    std::vector<FilteredBox>& filtered_boxes);

    void check1DInput(const Shape& shape, const std::string& name, size_t port);

    void checkOutput(const Shape& shape, const std::string& name, size_t port);

    void createJitKernel();

    NMSBoxEncodeType boxEncodingType = NMSBoxEncodeType::CORNER;
    bool m_sort_result_descending = true;
    bool m_clockwise = false;
    bool m_rotated_boxes = false;
    size_t m_coord_num = 1LU;

    size_t m_batches_num = 0LU;
    size_t m_boxes_num = 0LU;
    size_t m_classes_num = 0LU;

    size_t m_max_output_boxes_per_class = 0LU;  // Original value of input NMS_MAX_OUTPUT_BOXES_PER_CLASS
    size_t m_output_boxes_per_class = 0LU;      // Actual number of output boxes
    float m_iou_threshold = 0.F;
    float m_score_threshold = 0.F;
    float m_soft_nms_sigma = 0.F;
    float m_scale = 0.F;
    // control placeholder for NMS in new opset.
    bool m_is_soft_suppressed_by_iou = false;

    bool m_out_static_shape = false;

    std::vector<std::vector<size_t>> m_num_filtered_boxes;
    const std::string inType = "input";
    const std::string outType = "output";
    bool m_defined_outputs[NMS_VALID_OUTPUTS + 1] = {false, false, false};
    std::vector<FilteredBox> m_filtered_boxes;

    std::shared_ptr<kernel::JitKernelBase> m_jit_kernel;
};

}  // namespace ov::intel_cpu::node
