/*
 * AXERA is pleased to support the open source community by making ax-samples available.
 *
 * Copyright (c) 2022, AXERA Semiconductor (Shanghai) Co., Ltd. All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 *
 * https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 */

/*
 * Author: ZHEQIUSHUI
 */
#ifdef AXERA_TARGET_CHIP_AX620
#include "ax_interpreter_external_api.h"
#include "ax_sys_api.h"
#include "joint.h"
#include "joint_adv.h"

#include "utilities/file.hpp"
#include "middleware/io.hpp"
#include "sample_run_joint.h"
#include "../include/ax_common_api.h"
#include "sample_log.h"
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>

#ifndef MIN
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#endif

#ifndef MAX
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#endif

typedef struct
{
    AX_JOINT_HANDLE joint_handle;
    AX_JOINT_SDK_ATTR_T joint_attr;

    // AX_JOINT_IO_INFO_T* io_info = nullptr;

    AX_JOINT_EXECUTION_CONTEXT joint_ctx;
    AX_JOINT_EXECUTION_CONTEXT_SETTING_T joint_ctx_settings;

    AX_JOINT_IO_T joint_io_arr;
    AX_JOINT_IO_SETTING_T joint_io_setting;

    ax_imgproc_t imgproc;

    AX_JOINT_COLOR_SPACE_T SAMPLE_ALOG_FORMAT;
    int SAMPLE_ALGO_WIDTH = 0;
    int SAMPLE_ALGO_HEIGHT = 0;
} handle_t;

AX_S32 alloc_joint_buffer(const AX_JOINT_IOMETA_T *pMeta, AX_JOINT_IO_BUFFER_T *pBuf)
{
    // AX_JOINT_IOMETA_T meta = *pMeta;
    auto ret = AX_JOINT_AllocBuffer(pMeta, pBuf, AX_JOINT_ABST_DEFAULT);
    if (AX_ERR_NPU_JOINT_SUCCESS != ret)
    {
        fprintf(stderr, "[ERR]: Cannot allocate memory.\n");
        return -1;
    }
    return AX_ERR_NPU_JOINT_SUCCESS;
}

AX_S32 prepare_io(axdl_image_t *algo_input, AX_JOINT_IO_T &io, const AX_JOINT_IO_INFO_T *io_info, const uint32_t &batch)
{
    memset(&io, 0, sizeof(io));

    io.nInputSize = io_info->nInputSize;
    if (1 != io.nInputSize)
    {
        fprintf(stderr, "[ERR]: Only single input was accepted(got %u).\n", io.nInputSize);
        return -1;
    }
    io.pInputs = new AX_JOINT_IO_BUFFER_T[io.nInputSize];

    // fill input
    {
        const AX_JOINT_IOMETA_T *pMeta = io_info->pInputs;
        AX_JOINT_IO_BUFFER_T *pBuf = io.pInputs;

        if (pMeta->nShapeSize <= 0)
        {
            fprintf(stderr, "[ERR]: Dimension(%u) of shape is not allowed.\n", (uint32_t)pMeta->nShapeSize);
            return -1;
        }

        auto actual_data_size = pMeta->nSize / pMeta->pShape[0] * batch;
        if (algo_input->nSize != actual_data_size)
        {
            fprintf(stderr,
                    "[ERR]: The buffer size is not equal to model input(%s) size(%u vs %u).\n",
                    io_info->pInputs[0].pName,
                    (uint32_t)algo_input->nSize,
                    actual_data_size);
            return -1;
        }
        pBuf->phyAddr = (AX_ADDR)algo_input->pPhy;
        pBuf->pVirAddr = (AX_VOID *)algo_input->pVir;
        pBuf->nSize = (AX_U32)algo_input->nSize;
    }

    // deal with output
    {
        io.nOutputSize = io_info->nOutputSize;
        io.pOutputs = new AX_JOINT_IO_BUFFER_T[io.nOutputSize];
        for (size_t i = 0; i < io.nOutputSize; ++i)
        {
            const AX_JOINT_IOMETA_T *pMeta = io_info->pOutputs + i;
            AX_JOINT_IO_BUFFER_T *pBuf = io.pOutputs + i;
            alloc_joint_buffer(pMeta, pBuf);
        }
    }
    return AX_ERR_NPU_JOINT_SUCCESS;
}

int sample_run_joint_init(char *model_file, void **yhandle, sample_run_joint_attr *attr)
{
    if (!model_file)
    {
        ALOGE("invalid param:model_file is null");
        return -1;
    }

    if (!attr)
    {
        ALOGE("invalid param:attr is null");
        return -1;
    }

    handle_t *handle = new handle_t;

    // 1. create a runtime handle and load the model
    // AX_JOINT_HANDLE joint_handle;
    memset(&handle->joint_handle, 0, sizeof(handle->joint_handle));

    memset(&handle->joint_attr, 0, sizeof(handle->joint_attr));

    // 1.1 read model file to buffer
    // std::vector<char> model_buffer;
    // if (!utilities::read_file(model_file, model_buffer))
    // {
    //     fprintf(stderr, "Read Run-Joint model(%s) file failed.\n", model_file);
    //     return -1;
    // }

    auto *file_fp = fopen(model_file, "r");
    if (!file_fp)
    {
        ALOGE("Read Run-Joint model(%s) file failed.\n", model_file);
        return -1;
    }

    fseek(file_fp, 0, SEEK_END);
    int model_size = ftell(file_fp);
    fclose(file_fp);

    int fd = open(model_file, O_RDWR, 0644);
    void *mmap_add = mmap(NULL, model_size, PROT_WRITE, MAP_SHARED, fd, 0);

    auto ret = middleware::parse_npu_mode_from_joint((const char *)mmap_add, model_size, &handle->joint_attr.eNpuMode);
    if (AX_ERR_NPU_JOINT_SUCCESS != ret)
    {
        fprintf(stderr, "Load Run-Joint model(%s) failed.\n", model_file);
        munmap(mmap_add, model_size);
        return -1;
    }

    // 1.3 init model
    ret = AX_JOINT_Adv_Init(&handle->joint_attr);
    if (AX_ERR_NPU_JOINT_SUCCESS != ret)
    {
        fprintf(stderr, "Init Run-Joint model(%s) failed.\n", model_file);
        munmap(mmap_add, model_size);
        return -1;
    }

    auto deinit_joint = [&]()
    {
        AX_JOINT_DestroyHandle(handle->joint_handle);
        AX_JOINT_Adv_Deinit();
        munmap(mmap_add, model_size);
        return -1;
    };

    // 1.4 the real init processing

    ret = AX_JOINT_CreateHandle(&handle->joint_handle, (const char *)mmap_add, model_size);
    if (AX_ERR_NPU_JOINT_SUCCESS != ret)
    {
        fprintf(stderr, "Create Run-Joint handler from file(%s) failed.\n", model_file);
        return deinit_joint();
    }

    // 1.5 get the version of toolkit (optional)
    const AX_CHAR *version = AX_JOINT_GetModelToolsVersion(handle->joint_handle);
    fprintf(stdout, "Tools version: %s\n", version);

    // 1.6 drop the model buffer
    munmap(mmap_add, model_size);
    // std::vector<char>().swap(model_buffer);

    // 1.7 create context
    memset(&handle->joint_ctx, 0, sizeof(handle->joint_ctx));
    memset(&handle->joint_ctx_settings, 0, sizeof(handle->joint_ctx_settings));
    ret = AX_JOINT_CreateExecutionContextV2(handle->joint_handle, &handle->joint_ctx, &handle->joint_ctx_settings);
    if (AX_ERR_NPU_JOINT_SUCCESS != ret)
    {
        fprintf(stderr, "Create Run-Joint context failed.\n");
        return deinit_joint();
    }

    memset(&handle->joint_io_arr, 0, sizeof(handle->joint_io_arr));
    memset(&handle->joint_io_setting, 0, sizeof(handle->joint_io_setting));

    auto io_info = AX_JOINT_GetIOInfo(handle->joint_handle);
    handle->SAMPLE_ALGO_WIDTH = io_info->pInputs->pShape[2];
    handle->SAMPLE_ALOG_FORMAT = io_info->pInputs->pExtraMeta->eColorSpace;

    switch (handle->SAMPLE_ALOG_FORMAT)
    {
    case AX_JOINT_CS_NV12:
        attr->algo_colorformat = (int)AX_YUV420_SEMIPLANAR;
        handle->SAMPLE_ALGO_HEIGHT = io_info->pInputs->pShape[1] / 1.5;
        ALOGI("NV12 MODEL (%s)\n", model_file);
        break;
    case AX_JOINT_CS_RGB:
        attr->algo_colorformat = (int)AX_FORMAT_RGB888;
        handle->SAMPLE_ALGO_HEIGHT = io_info->pInputs->pShape[1];
        ALOGI("RGB MODEL (%s)\n", model_file);
        break;
    case AX_JOINT_CS_BGR:
        attr->algo_colorformat = (int)AX_FORMAT_BGR888;
        handle->SAMPLE_ALGO_HEIGHT = io_info->pInputs->pShape[1];
        ALOGI("BGR MODEL (%s)\n", model_file);
        break;
    default:
        ALOGE("now ax-pipeline just only support NV12/RGB/BGR input format,you can modify by yourself");
        return deinit_joint();
    }

    switch (handle->SAMPLE_ALOG_FORMAT)
    {
    case AX_JOINT_CS_NV12:
        handle->imgproc.init(handle->SAMPLE_ALGO_WIDTH, handle->SAMPLE_ALGO_HEIGHT, true, axdl_color_space_nv12);
        ret = prepare_io(handle->imgproc.get(axdl_color_space_nv12), handle->joint_io_arr, io_info, 1);
        break;
    case AX_JOINT_CS_RGB:
        handle->imgproc.init(handle->SAMPLE_ALGO_WIDTH, handle->SAMPLE_ALGO_HEIGHT, true, axdl_color_space_rgb);
        ret = prepare_io(handle->imgproc.get(axdl_color_space_rgb), handle->joint_io_arr, io_info, 1);
        break;
    case AX_JOINT_CS_BGR:
        handle->imgproc.init(handle->SAMPLE_ALGO_WIDTH, handle->SAMPLE_ALGO_HEIGHT, true, axdl_color_space_bgr);
        ret = prepare_io(handle->imgproc.get(axdl_color_space_bgr), handle->joint_io_arr, io_info, 1);
        break;
    default:
        ALOGE("now ax-pipeline just only support NV12/RGB/BGR input format,you can modify by yourself");
        return deinit_joint();
    }

    if (AX_ERR_NPU_JOINT_SUCCESS != ret)
    {
        fprintf(stderr, "Fill input failed.\n");
        AX_JOINT_DestroyExecutionContext(handle->joint_ctx);
        return deinit_joint();
    }

    handle->joint_io_arr.pIoSetting = &handle->joint_io_setting;

    attr->algo_width = handle->SAMPLE_ALGO_WIDTH;
    attr->algo_height = handle->SAMPLE_ALGO_HEIGHT;
    attr->nOutputSize = io_info->nOutputSize;
    attr->pOutputsInfo = io_info->pOutputs;
    attr->pOutputs = handle->joint_io_arr.pOutputs;

    *yhandle = handle;
    return 0;
}

int sample_run_joint_release(void *yhandle)
{
    handle_t *handle = (handle_t *)yhandle;
    if (handle)
    {
        auto DestroyJoint = [&]()
        {
            if (handle->joint_io_arr.pInputs)
            {
                delete[] handle->joint_io_arr.pInputs;
            }

            if (handle->joint_io_arr.pOutputs)
            {
                for (size_t i = 0; i < handle->joint_io_arr.nOutputSize; ++i)
                {
                    AX_JOINT_IO_BUFFER_T *pBuf = handle->joint_io_arr.pOutputs + i;
                    AX_JOINT_FreeBuffer(pBuf);
                }

                delete[] handle->joint_io_arr.pOutputs;
            }

            AX_JOINT_DestroyExecutionContext(handle->joint_ctx);
            AX_JOINT_DestroyHandle(handle->joint_handle);
            AX_JOINT_Adv_Deinit();
        };
        DestroyJoint();
        handle->imgproc.deinit();

        delete handle;
    }
    return 0;
}

int sample_run_joint_inference(void *yhandle, const void *_pstFrame, const void *crop_resize_box)
{
    handle_t *handle = (handle_t *)yhandle;

    if (!handle)
    {
        ALOGE("invalid param:yhandle is null");
        return -1;
    }

    if (handle->imgproc.process((axdl_image_t *)_pstFrame, (axdl_bbox_t *)crop_resize_box) != 0)
    {
        ALOGE("image process failed");
        return -1;
    }

    auto ret = AX_JOINT_RunSync(handle->joint_handle, handle->joint_ctx, &handle->joint_io_arr);
    if (ret != AX_ERR_NPU_JOINT_SUCCESS)
    {
        return -1;
    }

    return 0;
}
#endif