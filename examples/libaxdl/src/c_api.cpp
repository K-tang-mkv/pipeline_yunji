#include "../include/c_api.h"

#include "../../utilities/sample_log.h"
#include "../include/ax_model_base.hpp"
#include "../../utilities/json.hpp"
#include "utilities/object_register.hpp"

#include "fstream"

extern "C"
{
    // 给sipeed的python包用的
    typedef int (*result_callback_for_sipeed_py)(void *, axdl_results_t *);
    result_callback_for_sipeed_py g_cb_results_sipeed_py = NULL;
    int register_result_callback(result_callback_for_sipeed_py cb)
    {
        g_cb_results_sipeed_py = cb;
        return 0;
    }

    // 给sipeed的python包用的
    typedef int (*display_callback_for_sipeed_py)(int, int, int, char **);
    display_callback_for_sipeed_py g_cb_display_sipeed_py = NULL;
    int register_display_callback(display_callback_for_sipeed_py cb)
    {
        g_cb_display_sipeed_py = cb;
        return 0;
    }
}

struct ax_model_handle_t
{
    std::shared_ptr<ax_model_base> model = nullptr;
    std::mutex locker;

    int fcnt = 0;
    int fps = -1;
    struct timespec ts1
    {
        0
    }, ts2{0};
};

int axdl_parse_param_init(char *json_file_path, void **pModels)
{
    std::ifstream f(json_file_path);
    if (f.fail())
    {
        ALOGE("json file [%s] is not exist.", json_file_path);
        return -1;
    }
    auto jsondata = nlohmann::json::parse(f);
    f.close();

    std::string strModelType;
    int mt = ax_model_base::get_model_type(&jsondata, strModelType); // get config model type, det, or cls, or pose
    if (mt == MT_UNKNOWN)
    {
        ALOGE("unknown model type: %s", strModelType.c_str());
        return -1;
    }
    *pModels = new ax_model_handle_t;
    ax_model_base *model = (ax_model_base *)OBJFactory::getInstance().getObjectByID(mt); // here is MT_DET_YOLOV5
    if (model == nullptr)
    {
        ALOGE("create model failed mt=%d", mt);
        return -1;
    }

    ((ax_model_handle_t *)(*pModels))->model.reset(model); // make pModels be model
    int ret = ((ax_model_handle_t *)(*pModels))->model->init(&jsondata);
    bool track_enable = ax_model_base::get_track_enable(&jsondata);
    if (ret == 0 && track_enable)
    {
        ALOGI("====================================");
        ALOGI("========== enable tracker ==========");
        ALOGI("====================================");
        ((ax_model_handle_t *)(*pModels))->model->enable_track();
    }
    return ret;
}

void axdl_deinit(void **pModels)
{
    if (pModels && (ax_model_handle_t *)(*pModels) && ((ax_model_handle_t *)(*pModels))->model.get())
    {
        ((ax_model_handle_t *)(*pModels))->model->deinit();
        delete (ax_model_handle_t *)(*pModels);
        *pModels = nullptr;
    }
}

int axdl_get_ivps_width_height(void *pModels, char *json_file_path, int *width_ivps, int *height_ivps)
{
    if (!(ax_model_handle_t *)(pModels) || !((ax_model_handle_t *)(pModels))->model.get())
    {
        return -1;
    }
    std::ifstream f(json_file_path);
    if (f.fail())
    {
        return -1;
    }
    auto jsondata = nlohmann::json::parse(f);
    f.close();

    if (jsondata.contains("SAMPLE_IVPS_ALGO_WIDTH") && jsondata.contains("SAMPLE_IVPS_ALGO_HEIGHT"))
    {
        *width_ivps = jsondata["SAMPLE_IVPS_ALGO_WIDTH"];
        *height_ivps = jsondata["SAMPLE_IVPS_ALGO_HEIGHT"];
        ((ax_model_handle_t *)pModels)->model->set_det_restore_resolution(*width_ivps, *height_ivps);
    }
    else
    {
        switch (((ax_model_handle_t *)pModels)->model->get_model_type())
        {
        case MT_MLM_HUMAN_POSE_AXPPL:
        case MT_MLM_HUMAN_POSE_HRNET:
        case MT_MLM_ANIMAL_POSE_HRNET:
        case MT_MLM_HAND_POSE:
        case MT_MLM_FACE_RECOGNITION:
        case MT_MLM_VEHICLE_LICENSE_RECOGNITION:
            *width_ivps = 960;
            *height_ivps = 540;
            ((ax_model_handle_t *)pModels)->model->set_det_restore_resolution(*width_ivps, *height_ivps);
            break;
        default:
            *width_ivps = ((ax_model_handle_t *)pModels)->model->get_algo_width();
            *height_ivps = ((ax_model_handle_t *)pModels)->model->get_algo_height();
            break;
        }
    }
    return 0;
}

int axdl_set_ivps_width_height(void *pModels, int width_ivps, int height_ivps)
{
    if (!(ax_model_handle_t *)(pModels) || !((ax_model_handle_t *)(pModels))->model.get())
    {
        return -1;
    }
    ((ax_model_handle_t *)pModels)->model->set_det_restore_resolution(width_ivps, height_ivps);
    return 0;
}

axdl_color_space_e axdl_get_color_space(void *pModels)
{
    if (!(ax_model_handle_t *)(pModels) || !((ax_model_handle_t *)(pModels))->model.get())
    {
        return axdl_color_space_e::axdl_color_space_unknown;
    }
    return ((ax_model_handle_t *)pModels)->model->get_color_space();
}

int axdl_get_model_type(void *pModels)
{
    if (!(ax_model_handle_t *)(pModels) || !((ax_model_handle_t *)(pModels))->model.get())
    {
        return -1;
    }
    return ((ax_model_handle_t *)pModels)->model->get_model_type();
}

int axdl_get_letterbox_enable(void *pModels)
{
    if (!(ax_model_handle_t *)(pModels) || !((ax_model_handle_t *)(pModels))->model.get())
    {
        return -1;
    }
    return ((ax_model_handle_t *)pModels)->model->get_letter_box_enable() ? 1 : 0;
}

// execute inference with model and frame and return results
int axdl_inference(void *pModels, axdl_image_t *pstFrame, axdl_results_t *pResults)
{
    if (!(ax_model_handle_t *)(pModels) || !((ax_model_handle_t *)(pModels))->model.get())
    {
        return -1;
    }
    std::lock_guard<std::mutex> locker(((ax_model_handle_t *)pModels)->locker);
    pResults->mModelType = ((ax_model_handle_t *)pModels)->model->get_model_type();
    // inference here
    int ret = ((ax_model_handle_t *)pModels)->model->inference(pstFrame, nullptr, pResults);
    if (ret)
    {
        return -1;
    }
    int width, height;
    ((ax_model_handle_t *)pModels)->model->get_det_restore_resolution(width, height);
    // post process
    for (int i = 0; i < pResults->nObjSize; i++)
    {
        pResults->mObjects[i].bbox.x /= width;
        pResults->mObjects[i].bbox.y /= height;
        pResults->mObjects[i].bbox.w /= width;
        pResults->mObjects[i].bbox.h /= height;

        for (int j = 0; j < pResults->mObjects[i].nLandmark; j++)
        {
            pResults->mObjects[i].landmark[j].x /= width;
            pResults->mObjects[i].landmark[j].y /= height;
        }

        if (pResults->mObjects[i].bHasBoxVertices)
        {
            for (size_t j = 0; j < 4; j++)
            {
                pResults->mObjects[i].bbox_vertices[j].x /= width;
                pResults->mObjects[i].bbox_vertices[j].y /= height;
            }
        }
    }

    for (int i = 0; i < pResults->nCrowdCount; i++)
    {
        pResults->mCrowdCountPts[i].x /= width;
        pResults->mCrowdCountPts[i].y /= height;
    }

    if (g_cb_results_sipeed_py)
    {
        ret = g_cb_results_sipeed_py((void *)pstFrame, pResults);
    }

    {
        int &fcnt = ((ax_model_handle_t *)pModels)->fcnt;
        int &fps = ((ax_model_handle_t *)pModels)->fps;
        struct timespec &ts1 = ((ax_model_handle_t *)pModels)->ts1,
                        &ts2 = ((ax_model_handle_t *)pModels)->ts2;
        fcnt++;
        clock_gettime(CLOCK_MONOTONIC, &ts2);
        if ((ts2.tv_sec * 1000 + ts2.tv_nsec / 1000000) - (ts1.tv_sec * 1000 + ts1.tv_nsec / 1000000) >= 1000)
        {
            fps = fcnt;
            ts1 = ts2;
            fcnt = 0;
        }
        pResults->niFps = fps;
    }

    return 0;
}

int axdl_draw_results(void *pModels, axdl_canvas_t *canvas, axdl_results_t *pResults, float fontscale, int thickness, int offset_x, int offset_y)
{
    if (!(ax_model_handle_t *)(pModels) || !((ax_model_handle_t *)(pModels))->model.get())
    {
        return -1;
    }
    if (g_cb_display_sipeed_py)
    {
        int ret = g_cb_display_sipeed_py(canvas->height, canvas->width, CV_8UC4, (char **)&canvas->data);
        for (uint32_t *rgba2abgr = (uint32_t *)canvas->data, i = 0, s = canvas->width * canvas->height; i != s; i++)
            rgba2abgr[i] = __builtin_bswap32(rgba2abgr[i]);
        if (ret != 0)
            return 0; // python will disable show
    }

    cv::Mat image(canvas->height, canvas->width, CV_8UC4, canvas->data);

    ((ax_model_handle_t *)pModels)->model->draw_results(image, pResults, fontscale, thickness, offset_x, offset_y);
    return 0;
}

void axdl_native_osd_init(void *pModels, int chn, int chn_width, int chn_height, int max_num_rgn)
{
    if (!(ax_model_handle_t *)(pModels) || !((ax_model_handle_t *)(pModels))->model.get())
    {
        return;
    }
    ((ax_model_handle_t *)pModels)->model->draw_init(chn, chn_width, chn_height, max_num_rgn);
}

void *axdl_native_osd_get_handle(void *pModels, int chn)
{
    if (!(ax_model_handle_t *)(pModels) || !((ax_model_handle_t *)(pModels))->model.get())
    {
        return nullptr;
    }
    return ((ax_model_handle_t *)pModels)->model->get_drawer(chn).get().data();
}

int axdl_native_osd_draw_results(void *pModels, int chn, axdl_results_t *pResults, float fontscale, int thickness)
{
    if (!(ax_model_handle_t *)(pModels) || !((ax_model_handle_t *)(pModels))->model.get())
    {
        return -1;
    }
    ((ax_model_handle_t *)pModels)->model->draw_results(chn, pResults, fontscale, thickness);
    return 0;
}