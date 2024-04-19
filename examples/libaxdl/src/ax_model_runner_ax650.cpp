#if defined(AXERA_TARGET_CHIP_AX650) || defined(AXERA_TARGET_CHIP_AX620E)
#include "ax_model_runner_ax650.hpp"
#include "string.h"
#include <fcntl.h>
#include <sys/mman.h>
#include "utilities/file.hpp"
#include <ax_sys_api.h>
#include <ax_engine_api.h>

#include "../../utilities/sample_log.h"

#define AX_CMM_ALIGN_SIZE 128

const char *AX_CMM_SESSION_NAME = "ax-pipeline-npu";

typedef enum
{
    AX_ENGINE_ABST_DEFAULT = 0,
    AX_ENGINE_ABST_CACHED = 1,
} AX_ENGINE_ALLOC_BUFFER_STRATEGY_T;

typedef std::pair<AX_ENGINE_ALLOC_BUFFER_STRATEGY_T, AX_ENGINE_ALLOC_BUFFER_STRATEGY_T> INPUT_OUTPUT_ALLOC_STRATEGY;

void free_io_index(AX_ENGINE_IO_BUFFER_T *io_buf, int index)
{
    for (int i = 0; i < index; ++i)
    {
        AX_ENGINE_IO_BUFFER_T *pBuf = io_buf + i;
        AX_SYS_MemFree(pBuf->phyAddr, pBuf->pVirAddr);
    }
}

void free_io(AX_ENGINE_IO_T *io)
{
    for (size_t j = 0; j < io->nInputSize; ++j)
    {
        AX_ENGINE_IO_BUFFER_T *pBuf = io->pInputs + j;
        AX_SYS_MemFree(pBuf->phyAddr, pBuf->pVirAddr);
    }
    for (size_t j = 0; j < io->nOutputSize; ++j)
    {
        AX_ENGINE_IO_BUFFER_T *pBuf = io->pOutputs + j;
        AX_SYS_MemFree(pBuf->phyAddr, pBuf->pVirAddr);
    }
    delete[] io->pInputs;
    delete[] io->pOutputs;
}

static inline int prepare_io(AX_ENGINE_IO_INFO_T *info, AX_ENGINE_IO_T *io_data, ax_imgproc_t *pimgproc, INPUT_OUTPUT_ALLOC_STRATEGY strategy)
{
    memset(io_data, 0, sizeof(*io_data));
    io_data->pInputs = new AX_ENGINE_IO_BUFFER_T[info->nInputSize];
    io_data->nInputSize = info->nInputSize;

    auto ret = 0;
    if (info->nInputSize == 1)
    {
        auto buffer = &io_data->pInputs[0];
        buffer->pVirAddr = pimgproc->get()->pVir;
        buffer->phyAddr = pimgproc->get()->pPhy;
    }
    else
    {
        ALOGE("Only single input was accepted(got %u).\n", info->nInputSize);
        // for (uint i = 0; i < info->nInputSize; ++i)
        // {
        //     auto meta = info->pInputs[i];
        //     auto buffer = &io_data->pInputs[i];
        //     if (strategy.first == AX_ENGINE_ABST_CACHED)
        //     {
        //         ret = AX_SYS_MemAllocCached((AX_U64 *)(&buffer->phyAddr), &buffer->pVirAddr, meta.nSize, AX_CMM_ALIGN_SIZE, (const AX_S8 *)(AX_CMM_SESSION_NAME));
        //     }
        //     else
        //     {
        //         ret = AX_SYS_MemAlloc((AX_U64 *)(&buffer->phyAddr), &buffer->pVirAddr, meta.nSize, AX_CMM_ALIGN_SIZE, (const AX_S8 *)(AX_CMM_SESSION_NAME));
        //     }

        //     if (ret != 0)
        //     {
        //         free_io_index(io_data->pInputs, i);
        //         fprintf(stderr, "Allocate input{%d} { phy: %p, vir: %p, size: %lu Bytes }. fail \n", i, (void *)buffer->phyAddr, buffer->pVirAddr, (long)meta.nSize);
        //         return ret;
        //     }
        //     // fprintf(stderr, "Allocate input{%d} { phy: %p, vir: %p, size: %lu Bytes }. \n", i, (void*)buffer->phyAddr, buffer->pVirAddr, (long)meta.nSize);
        // }
    }

    io_data->pOutputs = new AX_ENGINE_IO_BUFFER_T[info->nOutputSize];
    io_data->nOutputSize = info->nOutputSize;
    for (uint i = 0; i < info->nOutputSize; ++i)
    {
        auto meta = info->pOutputs[i];
        auto buffer = &io_data->pOutputs[i];
        buffer->nSize = meta.nSize;

        ret = AX_SYS_MemAllocCached((AX_U64 *)(&buffer->phyAddr), &buffer->pVirAddr, meta.nSize, AX_CMM_ALIGN_SIZE, (const AX_S8 *)(AX_CMM_SESSION_NAME));

        if (ret != 0)
        {
            fprintf(stderr, "Allocate output{%d} { phy: %p, vir: %p, size: %lu Bytes }. fail \n", i, (void *)buffer->phyAddr, buffer->pVirAddr, (long)meta.nSize);
            free_io_index(io_data->pInputs, io_data->nInputSize);
            free_io_index(io_data->pOutputs, i);
            return ret;
        }
        // fprintf(stderr, "Allocate output{%d} { phy: %p, vir: %p, size: %lu Bytes }.\n", i, (void*)buffer->phyAddr, buffer->pVirAddr, (long)meta.nSize);
    }

    return 0;
}

struct ax_joint_runner_ax650_handle_t
{
    AX_ENGINE_HANDLE handle;
    AX_ENGINE_IO_INFO_T *io_info;
    AX_ENGINE_IO_T io_data;

    unsigned int algo_width, algo_height;
    int algo_colorformat;
};

int ax_runner_ax650::init(const char *model_file)
{
    if (m_handle)
    {
        return -1;
    }
    m_handle = new ax_joint_runner_ax650_handle_t;

    // 1. init engine
    int ret = 0;

    // 2. load model
    auto *file_fp = fopen(model_file, "r");
    if (!file_fp)
    {
        ALOGE("Read model(%s) file failed.\n", model_file);
        return -1;
    }
    fseek(file_fp, 0, SEEK_END);
    int model_size = ftell(file_fp);
    fclose(file_fp);
    int fd = open(model_file, O_RDWR, 0644);
    void *mmap_add = mmap(NULL, model_size, PROT_WRITE, MAP_SHARED, fd, 0);

    // 3. create handle

    ret = AX_ENGINE_CreateHandle(&m_handle->handle, mmap_add, model_size);
    if (0 != ret)
    {
        ALOGE("AX_ENGINE_CreateHandle 0x%x", ret);
        return ret;
    }
    fprintf(stdout, "Engine creating handle is done.\n");
    munmap(mmap_add, model_size);

    // 4. create context
    ret = AX_ENGINE_CreateContext(m_handle->handle);
    if (0 != ret)
    {
        ALOGE("AX_ENGINE_CreateContext 0x%x", ret);
        return ret;
    }
    fprintf(stdout, "Engine creating context is done.\n");

    // {
    //     static int affinity = 0;
    //     AX_ENGINE_SetAffinity(m_handle->handle, affinity % 3);
    //     affinity++;
    // }

    // 5. set io

    ret = AX_ENGINE_GetIOInfo(m_handle->handle, &m_handle->io_info);
    if (0 != ret)
    {
        return ret;
    }
    fprintf(stdout, "Engine get io info is done. \n");

    m_handle->algo_width = m_handle->io_info->pInputs[0].pShape[2];

    switch (m_handle->io_info->pInputs[0].pExtraMeta->eColorSpace)
    {
    case AX_ENGINE_CS_NV12:
        m_handle->algo_colorformat = (int)AX_FORMAT_YUV420_SEMIPLANAR;
        m_handle->algo_height = m_handle->io_info->pInputs[0].pShape[1] / 1.5;
        imgproc.init(m_handle->algo_width, m_handle->algo_height, true, axdl_color_space_nv12);
        ALOGI("NV12 MODEL");
        break;
    case AX_ENGINE_CS_RGB:
        m_handle->algo_colorformat = (int)AX_FORMAT_RGB888;
        m_handle->algo_height = m_handle->io_info->pInputs[0].pShape[1];
        imgproc.init(m_handle->algo_width, m_handle->algo_height, true, axdl_color_space_rgb);
        ALOGI("RGB MODEL");
        break;
    case AX_ENGINE_CS_BGR:
        m_handle->algo_colorformat = (int)AX_FORMAT_BGR888;
        m_handle->algo_height = m_handle->io_info->pInputs[0].pShape[1];
        imgproc.init(m_handle->algo_width, m_handle->algo_height, true, axdl_color_space_bgr);
        ALOGI("BGR MODEL");
        break;
    default:
        ALOGE("now ax-pipeline just only support NV12/RGB/BGR input format,you can modify by yourself");
        // return deinit_joint();
        return -1;
    }

    // 6. alloc io
    ret = prepare_io(m_handle->io_info, &m_handle->io_data, &imgproc, std::make_pair(AX_ENGINE_ABST_DEFAULT, AX_ENGINE_ABST_DEFAULT));
    if (0 != ret)
    {
        return ret;
    }
    fprintf(stdout, "Engine alloc io is done. \n");

    for (size_t i = 0; i < m_handle->io_info->nOutputSize; i++)
    {
        ax_runner_tensor_t tensor;
        tensor.nIdx = i;
        tensor.sName = std::string(m_handle->io_info->pOutputs[i].pName);
        tensor.nSize = m_handle->io_info->pOutputs[i].nSize;
        for (size_t j = 0; j < m_handle->io_info->pOutputs[i].nShapeSize; j++)
        {
            tensor.vShape.push_back(m_handle->io_info->pOutputs[i].pShape[j]);
        }
        tensor.phyAddr = m_handle->io_data.pOutputs[i].phyAddr;
        tensor.pVirAddr = m_handle->io_data.pOutputs[i].pVirAddr;
        mtensors.push_back(tensor);
    }

    for (size_t i = 0; i < m_handle->io_info->nInputSize; i++)
    {
        ax_runner_tensor_t tensor;
        tensor.nIdx = i;
        tensor.sName = std::string(m_handle->io_info->pInputs[i].pName);
        tensor.nSize = m_handle->io_info->pInputs[i].nSize;
        for (size_t j = 0; j < m_handle->io_info->pInputs[i].nShapeSize; j++)
        {
            tensor.vShape.push_back(m_handle->io_info->pInputs[i].pShape[j]);
        }
        tensor.phyAddr = m_handle->io_data.pInputs[i].phyAddr;
        tensor.pVirAddr = m_handle->io_data.pInputs[i].pVirAddr;
        minput_tensors.push_back(tensor);
    }

    return ret;
}

void ax_runner_ax650::deinit()
{
    if (m_handle && m_handle->handle)
    {
        free_io(&m_handle->io_data);
        AX_ENGINE_DestroyHandle(m_handle->handle);
    }
    delete m_handle;
    m_handle = nullptr;
    AX_ENGINE_Deinit();
}

int ax_runner_ax650::get_algo_width() { return m_handle->algo_width; }
int ax_runner_ax650::get_algo_height() { return m_handle->algo_height; }
axdl_color_space_e ax_runner_ax650::get_color_space()
{
    switch (m_handle->algo_colorformat)
    {
    case AX_FORMAT_RGB888:
        return axdl_color_space_e::axdl_color_space_rgb;
    case AX_FORMAT_BGR888:
        return axdl_color_space_e::axdl_color_space_bgr;
    case AX_FORMAT_YUV420_SEMIPLANAR:
        return axdl_color_space_e::axdl_color_space_nv12;
    default:
        return axdl_color_space_unknown;
    }
}

int ax_runner_ax650::inference(axdl_image_t *pstFrame, const axdl_bbox_t *crop_resize_box)
{
    // if (imgproc.process(pstFrame, (axdl_bbox_t *)crop_resize_box) != 0)
    // {
    //     ALOGE("image process failed");
    //     return -1;
    // }
    static std::map<AX_IMG_FORMAT_E, axdl_color_space_e> color_space_map =
        {{AX_FORMAT_RGB888, axdl_color_space_rgb},
         {AX_FORMAT_BGR888, axdl_color_space_bgr},
         {AX_FORMAT_YUV420_SEMIPLANAR, axdl_color_space_nv12},
         {AX_FORMAT_YUV420_SEMIPLANAR_VU, axdl_color_space_nv21}};

    if (color_space_map[(AX_IMG_FORMAT_E)m_handle->algo_colorformat] == pstFrame->eDtype &&
        m_handle->algo_width == pstFrame->nWidth &&
        m_handle->algo_height == pstFrame->nHeight)
    {
        memcpy(minput_tensors[0].pVirAddr, pstFrame->pVir, minput_tensors[0].nSize);
    }
    else
    {
        if (imgproc.process(pstFrame, (axdl_bbox_t *)crop_resize_box) != 0)
        {
            ALOGE("image process failed");
            return -1;
        }
    }

    // switch (m_handle->algo_colorformat)
    // {
    // case AX_FORMAT_RGB888:
    // case AX_FORMAT_BGR888:
    // {
    //     unsigned char *dst = (unsigned char *)minput_tensors[0].pVirAddr;
    //     unsigned char *src = (unsigned char *)imgproc.get()->pVir;
    //     memcpy(dst, src, minput_tensors[0].nSize);
    // }
    // break;
    // case AX_FORMAT_YUV420_SEMIPLANAR:
    // case AX_FORMAT_YUV420_SEMIPLANAR_VU:
    //     for (size_t i = 0; i < pstFrame->nHeight * 1.5; i++)
    //     {
    //         memcpy(dst + i * pstFrame->nWidth, src + i * pstFrame->tStride_W, pstFrame->nWidth);
    //     }
    //     break;
    // default:
    //     break;
    // }

    // memcpy(minput_tensors[0].pVirAddr, pstFrame->pVir, minput_tensors[0].nSize);
    return AX_ENGINE_RunSync(m_handle->handle, &m_handle->io_data);
}
#endif