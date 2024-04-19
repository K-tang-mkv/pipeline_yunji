#include "ax_model_multi_level_model.hpp"

#include "../../utilities/json.hpp"
#include "../../utilities/sample_log.h"

#include "opencv2/opencv.hpp"

#include "fstream"
// #include "ax_sys_api.h"
#include "ax_common_api.h"

#include "base/pose.hpp"

void draw_pose_result(cv::Mat &img, axdl_object_t *pObj, std::vector<pose::skeleton> &pairs, int joints_num, int offset_x, int offset_y)
{
    for (int i = 0; i < joints_num; i++)
    {
        cv::circle(img, cv::Point(pObj->landmark[i].x * img.cols + offset_x, pObj->landmark[i].y * img.rows + offset_y), 4, cv::Scalar(0, 255, 0), cv::FILLED);
    }

    cv::Scalar color;
    cv::Point pt1;
    cv::Point pt2;

    for (auto &element : pairs)
    {
        switch (element.left_right_neutral)
        {
        case 0:
            color = cv::Scalar(255, 255, 0, 0);
            break;
        case 1:
            color = cv::Scalar(255, 0, 0, 255);
            break;
        case 2:
            color = cv::Scalar(255, 0, 255, 0);
            break;
        case 3:
            color = cv::Scalar(255, 255, 0, 255);
            break;
        default:
            color = cv::Scalar(255, 255, 255, 255);
        }

        int x1 = (int)(pObj->landmark[element.connection[0]].x * img.cols) + offset_x;
        int y1 = (int)(pObj->landmark[element.connection[0]].y * img.rows) + offset_y;
        int x2 = (int)(pObj->landmark[element.connection[1]].x * img.cols) + offset_x;
        int y2 = (int)(pObj->landmark[element.connection[1]].y * img.rows) + offset_y;

        x1 = std::max(std::min(x1, (img.cols - 1)), 0);
        y1 = std::max(std::min(y1, (img.rows - 1)), 0);
        x2 = std::max(std::min(x2, (img.cols - 1)), 0);
        y2 = std::max(std::min(y2, (img.rows - 1)), 0);

        pt1 = cv::Point(x1, y1);
        pt2 = cv::Point(x2, y2);
        cv::line(img, pt1, pt2, color, 2);
    }
}

void ax_model_human_pose_axppl::draw_custom(cv::Mat &image, axdl_results_t *results, float fontscale, int thickness, int offset_x, int offset_y)
{
    draw_bbox(image, results, fontscale, thickness, offset_x, offset_y);
    for (int i = 0; i < results->nObjSize; i++)
    {
        static std::vector<pose::skeleton> pairs = {{15, 13, 0},
                                                    {13, 11, 1},
                                                    {16, 14, 2},
                                                    {14, 12, 3},
                                                    {11, 12, 0},
                                                    {5, 11, 1},
                                                    {6, 12, 2},
                                                    {5, 6, 3},
                                                    {5, 7, 0},
                                                    {6, 8, 1},
                                                    {7, 9, 2},
                                                    {8, 10, 3},
                                                    {1, 2, 0},
                                                    {0, 1, 1},
                                                    {0, 2, 2},
                                                    {1, 3, 3},
                                                    {2, 4, 0},
                                                    {0, 5, 1},
                                                    {0, 6, 2}};
        if (results->mObjects[i].nLandmark == SAMPLE_BODY_LMK_SIZE)
        {
            draw_pose_result(image, &results->mObjects[i], pairs, SAMPLE_BODY_LMK_SIZE, offset_x, offset_y);
        }
    }
}
void ax_model_human_pose_axppl::draw_custom(int chn, axdl_results_t *results, float fontscale, int thickness)
{
    draw_bbox(chn, results, fontscale, thickness);
    static std::vector<int> head{4, 2, 0, 1, 3};
    static std::vector<int> hand_arm{10, 8, 6, 5, 7, 9};
    static std::vector<int> leg{16, 14, 12, 6, 12, 11, 5, 11, 13, 15};
    std::vector<axdl_point_t> pts(leg.size());
    for (int d = 0; d < results->nObjSize; d++)
    {
        if (results->mObjects[d].nLandmark == SAMPLE_BODY_LMK_SIZE)
        {
            for (size_t k = 0; k < head.size(); k++)
            {
                pts[k].x = results->mObjects[d].landmark[head[k]].x;
                pts[k].y = results->mObjects[d].landmark[head[k]].y;
            }
            m_drawers[chn].add_line(pts.data(), head.size(), {255, 0, 255, 0}, thickness * 2);
            for (size_t k = 0; k < hand_arm.size(); k++)
            {
                pts[k].x = results->mObjects[d].landmark[hand_arm[k]].x;
                pts[k].y = results->mObjects[d].landmark[hand_arm[k]].y;
            }
            m_drawers[chn].add_line(pts.data(), hand_arm.size(), {255, 0, 0, 255}, thickness * 2);
            for (size_t k = 0; k < leg.size(); k++)
            {
                pts[k].x = results->mObjects[d].landmark[leg[k]].x;
                pts[k].y = results->mObjects[d].landmark[leg[k]].y;
            }
            m_drawers[chn].add_line(pts.data(), leg.size(), {255, 255, 0, 0}, thickness * 2);
        }
    }
}

int ax_model_human_pose_axppl::inference(axdl_image_t *pstFrame, axdl_bbox_t *crop_resize_box, axdl_results_t *results)
{
    int ret = model_0->inference(pstFrame, crop_resize_box, results);
    if (ret)
        return ret;
    std::vector<int> idxs;
    for (int i = 0; i < results->nObjSize; i++)
    {
        auto it = std::find(CLASS_IDS.begin(), CLASS_IDS.end(), results->mObjects[i].label);
        if (it != CLASS_IDS.end())
        {
            idxs.push_back(i);
        }
    }
    int count = MIN((int)idxs.size(), MAX_SUB_INFER_COUNT);
    results->nObjSize = count;
    for (int i = 0; i < count; i++)
    {
        int idx = idxs[i];
        model_1->set_current_index(idx);
        ret = model_1->inference(pstFrame, &results->mObjects[idx].bbox, results);
        if (ret)
            return ret;
        if (idx != 0)
        {
            memcpy(&results->mObjects[i], &results->mObjects[idx], sizeof(axdl_object_t));
        }
    }
    return 0;
}

void ax_model_animal_pose_hrnet::draw_custom(cv::Mat &image, axdl_results_t *results, float fontscale, int thickness, int offset_x, int offset_y)
{
    draw_bbox(image, results, fontscale, thickness, offset_x, offset_y);
    for (int i = 0; i < results->nObjSize; i++)
    {
        static std::vector<pose::skeleton> pairs = {{19, 15, 0},
                                                    {18, 14, 1},
                                                    {17, 13, 2},
                                                    {16, 12, 3},
                                                    {15, 11, 0},
                                                    {14, 10, 1},
                                                    {13, 9, 2},
                                                    {12, 8, 3},
                                                    {11, 6, 0},
                                                    {10, 6, 1},
                                                    {9, 7, 2},
                                                    {8, 7, 3},
                                                    {6, 7, 0},
                                                    {7, 5, 1},
                                                    {5, 4, 2},
                                                    {0, 2, 3},
                                                    {1, 3, 0},
                                                    {0, 1, 1},
                                                    {0, 4, 2},
                                                    {1, 4, 3}};
        if (results->mObjects[i].nLandmark == SAMPLE_ANIMAL_LMK_SIZE)
        {
            draw_pose_result(image, &results->mObjects[i], pairs, SAMPLE_ANIMAL_LMK_SIZE, offset_x, offset_y);
        }
    }
}

void ax_model_animal_pose_hrnet::draw_custom(int chn, axdl_results_t *results, float fontscale, int thickness)
{
    draw_bbox(chn, results, fontscale, thickness);
    static std::vector<std::vector<int>> skeletons{{19, 15, 11, 6, 7, 5, 4, 1, 3},
                                                   {18, 14, 10, 6, 7},
                                                   {17, 13, 9, 7},
                                                   {16, 12, 8, 7, 5, 4, 0, 1, 0, 2}};
    static std::vector<ax_osd_drawer::ax_abgr_t> colors{{255, 255, 255, 255}, {255, 255, 0, 0}, {255, 0, 255, 0}, {255, 0, 0, 255}, {255, 0, 0, 0}};
    std::vector<axdl_point_t> pts;
    for (int d = 0; d < results->nObjSize; d++)
    {
        if (results->mObjects[d].nLandmark == SAMPLE_ANIMAL_LMK_SIZE)
        {
            for (size_t s = 0; s < skeletons.size(); s++)
            {
                pts.resize(skeletons[s].size());
                for (size_t k = 0; k < pts.size(); k++)
                {
                    pts[k].x = results->mObjects[d].landmark[skeletons[s][k]].x;
                    pts[k].y = results->mObjects[d].landmark[skeletons[s][k]].y;
                }
                m_drawers[chn].add_line(pts.data(), pts.size(), colors[s], thickness * 2);
            }
        }
    }
}

void ax_model_hand_pose::draw_custom(cv::Mat &image, axdl_results_t *results, float fontscale, int thickness, int offset_x, int offset_y)
{
    draw_bbox(image, results, fontscale, thickness, offset_x, offset_y);
    for (int i = 0; i < results->nObjSize; i++)
    {
        static std::vector<pose::skeleton> hand_pairs = {{0, 1, 0},
                                                         {1, 2, 0},
                                                         {2, 3, 0},
                                                         {3, 4, 0},
                                                         {0, 5, 1},
                                                         {5, 6, 1},
                                                         {6, 7, 1},
                                                         {7, 8, 1},
                                                         {0, 9, 2},
                                                         {9, 10, 2},
                                                         {10, 11, 2},
                                                         {11, 12, 2},
                                                         {0, 13, 3},
                                                         {13, 14, 3},
                                                         {14, 15, 3},
                                                         {15, 16, 3},
                                                         {0, 17, 4},
                                                         {17, 18, 4},
                                                         {18, 19, 4},
                                                         {19, 20, 4}};
        if (results->mObjects[i].nLandmark == SAMPLE_HAND_LMK_SIZE)
        {
            draw_pose_result(image, &results->mObjects[i], hand_pairs, SAMPLE_HAND_LMK_SIZE, offset_x, offset_y);
        }
    }
}

void ax_model_hand_pose::draw_custom(int chn, axdl_results_t *results, float fontscale, int thickness)
{
    static std::vector<std::vector<int>> skeletons{{0, 1, 2, 3, 4}, {0, 5, 6, 7, 8}, {0, 9, 10, 11, 12}, {0, 13, 14, 15, 16}, {0, 17, 18, 19, 20}};
    static std::vector<ax_osd_drawer::ax_abgr_t> colors{{255, 255, 255, 255}, {255, 255, 0, 0}, {255, 0, 255, 0}, {255, 0, 0, 255}, {255, 0, 0, 0}};
    std::vector<axdl_point_t> pts(5);
    for (int d = 0; d < results->nObjSize; d++)
    {
        if (results->mObjects[d].nLandmark == SAMPLE_HAND_LMK_SIZE)
        {
            for (size_t s = 0; s < skeletons.size(); s++)
            {
                for (size_t k = 0; k < pts.size(); k++)
                {
                    pts[k].x = results->mObjects[d].landmark[skeletons[s][k]].x;
                    pts[k].y = results->mObjects[d].landmark[skeletons[s][k]].y;
                }
                m_drawers[chn].add_line(pts.data(), pts.size(), colors[s], thickness * 2);
            }
        }
    }
    draw_bbox(chn, results, fontscale, thickness);
}

void ax_model_hand_pose::deinit()
{
    model_1->deinit();
    model_0->deinit();
    ax_sys_memfree(pstFrame_RGB.pPhy, pstFrame_RGB.pVir);
}

int ax_model_hand_pose::inference(axdl_image_t *pstFrame, axdl_bbox_t *crop_resize_box, axdl_results_t *results)
{
    if (!pstFrame_RGB.pVir)
    {
        memcpy(&pstFrame_RGB, pstFrame, sizeof(axdl_image_t));
        pstFrame_RGB.eDtype = axdl_color_space_rgb;
        ax_sys_memalloc(&pstFrame_RGB.pPhy, (void **)&pstFrame_RGB.pVir, pstFrame_RGB.nSize, 0x100, NULL);
    }
    pstFrame_RGB.eDtype = axdl_color_space_bgr;
    ax_imgproc_csc(pstFrame, &pstFrame_RGB);
    pstFrame_RGB.eDtype = axdl_color_space_rgb;

    int ret = model_0->inference(&pstFrame_RGB, crop_resize_box, results);
    if (ret)
        return ret;

    int count = MIN(results->nObjSize, MAX_SUB_INFER_COUNT);
    results->nObjSize = count;
    for (int i = 0; i < results->nObjSize; i++)
    {
        model_1->set_current_index(i);
        ret = model_1->inference(pstFrame, crop_resize_box, results);
        if (ret)
            return ret;
    }
    return 0;
}

int ax_model_face_recognition::inference(axdl_image_t *pstFrame, axdl_bbox_t *crop_resize_box, axdl_results_t *results)
{
    if (!b_face_database_init)
    {
        for (size_t i = 0; i < face_register_ids.size(); i++)
        {
            auto &faceid = face_register_ids[i];
            cv::Mat image = cv::imread(faceid.path);
            if (image.empty())
            {
                ALOGE("image %s cannot open,name %s register failed", faceid.path.c_str(), faceid.name.c_str());
                continue;
            }
#if defined(AXERA_TARGET_CHIP_AX650) || defined(AXERA_TARGET_CHIP_AX620E)
            // width align 128
            image = image(cv::Rect(0, 0, image.cols - image.cols % 128, image.rows)).clone();
            ALOGI("image size %d %d", image.cols, image.rows);
#endif

            cv::cvtColor(image, image, cv::COLOR_BGR2RGB);
            axdl_image_t npu_image = {0};
            npu_image.eDtype = axdl_color_space_rgb;
            npu_image.nHeight = image.rows;
            npu_image.nWidth = image.cols;
            npu_image.tStride_W = npu_image.nWidth;
            npu_image.nSize = npu_image.nWidth * npu_image.nHeight * 3;
            ax_sys_memalloc(&npu_image.pPhy, (void **)&npu_image.pVir, npu_image.nSize, 0x100, "SAMPLE-CV");
            memcpy(npu_image.pVir, image.data, npu_image.nSize);

            axdl_results_t Results = {0};
            int width, height;
            model_0->get_det_restore_resolution(width, height);
            model_0->set_det_restore_resolution(npu_image.nWidth, npu_image.nHeight);
            int ret = model_0->inference(&npu_image, nullptr, &Results);
            model_0->set_det_restore_resolution(width, height);
            if (ret)
            {
                ax_sys_memfree(npu_image.pPhy, npu_image.pVir);
                continue;
            }
            if (Results.nObjSize)
            {
                model_1->set_current_index(0);
                ret = model_1->inference(&npu_image, nullptr, &Results);
                if (ret)
                {
                    ax_sys_memfree(npu_image.pPhy, npu_image.pVir);
                    continue;
                }
                faceid.feat.resize(FACE_FEAT_LEN);
                memcpy(faceid.feat.data(), Results.mObjects[0].mFaceFeat.data, FACE_FEAT_LEN * sizeof(float));
                ALOGI("register name=%s", faceid.name.c_str());
            }
            ax_sys_memfree(npu_image.pPhy, npu_image.pVir);
        }
        b_face_database_init = true;
    }
    int ret = model_0->inference(pstFrame, crop_resize_box, results);
    if (ret)
        return ret;
    int count = MIN(results->nObjSize, MAX_SUB_INFER_COUNT);
    results->nObjSize = count;
    for (int i = 0; i < count; i++)
    {
        model_1->set_current_index(i);
        ret = model_1->inference(pstFrame, crop_resize_box, results);
        if (ret)
        {
            ALOGE("sub model inference failed");
            return ret;
        }

        int maxidx = -1;
        float max_score = 0;
        for (size_t j = 0; j < face_register_ids.size(); j++)
        {
            if (int(face_register_ids[j].feat.size()) != FACE_FEAT_LEN)
            {
                continue;
            }
            float sim = _calcSimilar((float *)results->mObjects[i].mFaceFeat.data, face_register_ids[j].feat.data(), FACE_FEAT_LEN);
            if (sim > max_score && sim > FACE_RECOGNITION_THRESHOLD)
            {
                maxidx = j;
                max_score = sim;
            }
        }

        if (maxidx >= 0)
        {
            ALOGI("%s %f", face_register_ids[maxidx].name.data(), max_score);
            if (max_score >= FACE_RECOGNITION_THRESHOLD)
            {
                sprintf(results->mObjects[i].objname, "%s %0.2f", face_register_ids[maxidx].name.c_str(), max_score);
            }
            else
            {
                sprintf(results->mObjects[i].objname, "%d %0.2f", maxidx, max_score);
            }
        }
        else
        {
            sprintf(results->mObjects[i].objname, "%d %0.2f", maxidx, max_score);
            // sprintf(results->mObjects[i].objname, "unknown");
        }
    }

    return 0;
}

int ax_model_vehicle_license_recognition::inference(axdl_image_t *pstFrame, axdl_bbox_t *crop_resize_box, axdl_results_t *results)
{
    int ret = model_0->inference(pstFrame, crop_resize_box, results);
    if (ret)
        return ret;

    int count = MIN(results->nObjSize, MAX_SUB_INFER_COUNT);
    results->nObjSize = count;
    for (int i = 0; i < count; i++)
    {
        model_1->set_current_index(i);
        ret = model_1->inference(pstFrame, crop_resize_box, results);
        if (ret)
            return ret;
    }
    return 0;
}
