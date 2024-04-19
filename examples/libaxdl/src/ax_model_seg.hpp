#pragma once
#include "../include/ax_model_base.hpp"
#include "utilities/object_register.hpp"

#include "../../utilities/ringbuffer.hpp"
#include "opencv2/opencv.hpp"
#include "ax_model_clip.hpp"

class ax_model_pphumseg : public ax_model_single_base_t
{
protected:
    cv::Mat base_canvas;
    SimpleRingBuffer<std::shared_ptr<unsigned char>> mSimpleRingBuffer;
    int post_process(axdl_image_t *pstFrame, axdl_bbox_t *crop_resize_box, axdl_results_t *results) override;
    void draw_custom(cv::Mat &image, axdl_results_t *results, float fontscale, int thickness, int offset_x, int offset_y) override;
    void draw_custom(int chn, axdl_results_t *results, float fontscale, int thickness) override;
};
REGISTER(MT_SEG_PPHUMSEG, ax_model_pphumseg)

class ax_model_dinov2 : public ax_model_pphumseg
{
protected:
    cv::PCA pca;
    int cnt = 0;
    int post_process(axdl_image_t *pstFrame, axdl_bbox_t *crop_resize_box, axdl_results_t *results) override;
    void draw_custom(cv::Mat &image, axdl_results_t *results, float fontscale, int thickness, int offset_x, int offset_y) override;
    void draw_custom(int chn, axdl_results_t *results, float fontscale, int thickness) override;
};
REGISTER(MT_SEG_DINOV2, ax_model_dinov2)

class ax_model_dinov2_depth : public ax_model_pphumseg
{
protected:
    double history_maxVal = -FLT_MAX, history_minVal = FLT_MAX;
    int post_process(axdl_image_t *pstFrame, axdl_bbox_t *crop_resize_box, axdl_results_t *results) override;
    void draw_custom(cv::Mat &image, axdl_results_t *results, float fontscale, int thickness, int offset_x, int offset_y) override;
    void draw_custom(int chn, axdl_results_t *results, float fontscale, int thickness) override;
};
REGISTER(MT_SEG_DINOV2_DEPTH, ax_model_dinov2_depth)

class ax_model_glpdepth : public ax_model_dinov2_depth
{
protected:
    int post_process(axdl_image_t *pstFrame, axdl_bbox_t *crop_resize_box, axdl_results_t *results) override;
};
REGISTER(MT_SEG_GLPDEPTH, ax_model_glpdepth)

class ax_model_depth_anything : public ax_model_dinov2_depth
{
protected:
    // int preprocess(axdl_image_t *srcFrame, axdl_bbox_t *crop_resize_box, axdl_results_t *results) override;
    int post_process(axdl_image_t *pstFrame, axdl_bbox_t *crop_resize_box, axdl_results_t *results) override;
};
REGISTER(MT_SEG_DEPTH_ANYTHING, ax_model_depth_anything)