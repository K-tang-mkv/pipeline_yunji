#include "ax_common_api.h"

#include "c_api.h"
#include "utilities/mat_pixel_affine.h"
#include "../../utilities/sample_log.h"
#include "opencv2/opencv.hpp"

#include "string.h"

#include "ax_sys_api.h"
#include "ax_ivps_api.h"

#ifndef MIN
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#endif

#ifndef MAX
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#endif

int ax_sys_memalloc(unsigned long long int *phyaddr, void **pviraddr, unsigned int size, unsigned int align, const char *token)
{
    return AX_SYS_MemAlloc(phyaddr, pviraddr, size, align, (const AX_S8 *)token);
}

int ax_sys_memfree(unsigned long long int phyaddr, void *pviraddr)
{
    return AX_SYS_MemFree(phyaddr, pviraddr);
}
#ifdef AXERA_TARGET_CHIP_AX620
#include "npu_cv_kit/ax_npu_imgproc.h"
void cvt(axdl_image_t *src, AX_NPU_CV_Image *dst)
{
    memset(dst, 0, sizeof(AX_NPU_CV_Image));
    dst->pPhy = src->pPhy;
    dst->pVir = (unsigned char *)src->pVir;
    dst->nHeight = src->nHeight;
    dst->nWidth = src->nWidth;
    dst->nSize = src->nSize;
    dst->tStride.nW = src->tStride_W;
    switch (src->eDtype)
    {
    case axdl_color_space_nv12:
        dst->eDtype = AX_NPU_CV_FDT_NV12;
        break;
    case axdl_color_space_nv21:
        dst->eDtype = AX_NPU_CV_FDT_NV21;
        break;
    case axdl_color_space_bgr:
        dst->eDtype = AX_NPU_CV_FDT_BGR;
        break;
    case axdl_color_space_rgb:
        dst->eDtype = AX_NPU_CV_FDT_RGB;
        break;
    default:
        dst->eDtype = AX_NPU_CV_FDT_UNKNOWN;
        break;
    }
}

void cvt(AX_NPU_CV_Image *src, axdl_image_t *dst)
{
    memset(dst, 0, sizeof(axdl_image_t));
    dst->pPhy = src->pPhy;
    dst->pVir = (unsigned char *)src->pVir;
    dst->nHeight = src->nHeight;
    dst->nWidth = src->nWidth;
    dst->nSize = src->nSize;
    dst->tStride_W = src->tStride.nW;
    switch (src->eDtype)
    {
    case AX_NPU_CV_FDT_NV12:
        dst->eDtype = axdl_color_space_nv12;
        break;
    case AX_NPU_CV_FDT_NV21:
        dst->eDtype = axdl_color_space_nv21;
        break;
    case AX_NPU_CV_FDT_BGR:
        dst->eDtype = axdl_color_space_bgr;
        break;
    case AX_NPU_CV_FDT_RGB:
        dst->eDtype = axdl_color_space_rgb;
        break;
    default:
        dst->eDtype = axdl_color_space_unknown;
        break;
    }
}

int ax_imgproc_csc(axdl_image_t *src, axdl_image_t *dst)
{
    AX_NPU_CV_Image npu_src, npu_dst;
    cvt(src, &npu_src);
    cvt(dst, &npu_dst);
    return AX_NPU_CV_CSC(AX_NPU_MODEL_TYPE_1_1_1, &npu_src, &npu_dst);
}

int ax_imgproc_warp(axdl_image_t *src, axdl_image_t *dst, const float *pMat33, const int const_val)
{
    AX_NPU_CV_Image npu_src, npu_dst;
    cvt(src, &npu_src);
    cvt(dst, &npu_dst);
    return AX_NPU_CV_Warp(AX_NPU_MODEL_TYPE_1_1_2, &npu_src, &npu_dst, pMat33, AX_NPU_CV_BILINEAR, const_val);
}

int _ax_imgproc_crop_resize(axdl_image_t *src, axdl_image_t *dst, axdl_bbox_t *box, AX_NPU_CV_ImageResizeAlignParam horizontal, AX_NPU_CV_ImageResizeAlignParam vertical)
{
    AX_NPU_CV_Image npu_src, npu_dst;
    cvt(src, &npu_src);
    cvt(dst, &npu_dst);

    AX_NPU_CV_Color color;
    color.nYUVColorValue[0] = 128;
    color.nYUVColorValue[1] = 128;
    AX_NPU_SDK_EX_MODEL_TYPE_T virtual_npu_mode_type = AX_NPU_MODEL_TYPE_1_1_1;

    if (box)
    {
        box->x = MAX((int)box->x, 0);
        box->y = MAX((int)box->y, 0);

        box->w = MIN((int)box->w, (int)src->nWidth - (int)box->x);
        box->h = MIN((int)box->h, (int)src->nHeight - (int)box->y);

        box->w = int(box->w) - int(box->w) % 2;
        box->h = int(box->h) - int(box->h) % 2;
    }

    AX_NPU_CV_Box *ppBox[1];
    ppBox[0] = (AX_NPU_CV_Box *)box;

    AX_NPU_CV_Image *p_npu_dst = &npu_dst;

    return AX_NPU_CV_CropResizeImage(virtual_npu_mode_type, &npu_src, 1, &p_npu_dst, ppBox, horizontal, vertical, color);
}

int ax_imgproc_crop_resize(axdl_image_t *src, axdl_image_t *dst, axdl_bbox_t *box)
{
    return _ax_imgproc_crop_resize(src, dst, box, AX_NPU_CV_IMAGE_FORCE_RESIZE, AX_NPU_CV_IMAGE_FORCE_RESIZE);
}

int ax_imgproc_crop_resize_keep_ratio(axdl_image_t *src, axdl_image_t *dst, axdl_bbox_t *box)
{
    return _ax_imgproc_crop_resize(src, dst, box, AX_NPU_CV_IMAGE_HORIZONTAL_CENTER, AX_NPU_CV_IMAGE_VERTICAL_CENTER);
}

int ax_imgproc_align_face(axdl_object_t *obj, axdl_image_t *src, axdl_image_t *dst)
{
    static float target[10] = {38.2946, 51.6963,
                               73.5318, 51.5014,
                               56.0252, 71.7366,
                               41.5493, 92.3655,
                               70.7299, 92.2041};
    float _tmp[10] = {obj->landmark[0].x, obj->landmark[0].y,
                      obj->landmark[1].x, obj->landmark[1].y,
                      obj->landmark[2].x, obj->landmark[2].y,
                      obj->landmark[3].x, obj->landmark[3].y,
                      obj->landmark[4].x, obj->landmark[4].y};
    float _m[6], _m_inv[6];
    get_affine_transform(_tmp, target, 5, _m);
    invert_affine_transform(_m, _m_inv);
    float mat3x3[3][3] = {
        {_m_inv[0], _m_inv[1], _m_inv[2]},
        {_m_inv[3], _m_inv[4], _m_inv[5]},
        {0, 0, 1}};

    dst->eDtype = src->eDtype;
    if (dst->eDtype == axdl_color_space_rgb || dst->eDtype == axdl_color_space_bgr)
    {
        dst->nSize = 112 * 112 * 3;
    }
    else if (dst->eDtype == axdl_color_space_nv12 || dst->eDtype == axdl_color_space_nv21)
    {
        dst->nSize = 112 * 112 * 1.5;
    }
    else
    {
        ALOGE("just only support BGR/RGB/NV12 format");
    }
    return ax_imgproc_warp(src, dst, &mat3x3[0][0], 128);
}
#elif defined(AXERA_TARGET_CHIP_AX650)
// #include "ax_ive_api.h"

// static struct ive_init_t
// {
//     ive_init_t()
//     {
//         int ret = AX_IVE_Init();
//         if (ret != 0)
//         {
//             ALOGE("Ive init failed, s32Ret=0x%x!\n", ret);
//         }
//     }
//     ~ive_init_t()
//     {
//         AX_IVE_Exit();
//     }
// } tmp_ive_init_t;

void cvt(axdl_image_t *src, AX_VIDEO_FRAME_T *dst)
{
    memset(dst, 0, sizeof(AX_VIDEO_FRAME_T));
    dst->u32Height = src->nHeight;
    dst->u32Width = src->nWidth;
    dst->u32PicStride[0] = src->tStride_W;
    dst->u64PhyAddr[0] = src->pPhy;
    dst->u64VirAddr[0] = (AX_U64)src->pVir;
    switch (src->eDtype)
    {
    case axdl_color_space_nv12:
        dst->enImgFormat = AX_FORMAT_YUV420_SEMIPLANAR;
        dst->u32PicStride[1] = dst->u32PicStride[0];
        dst->u64PhyAddr[1] = dst->u64PhyAddr[0] + dst->u32PicStride[0] * dst->u32Height;
        dst->u64VirAddr[1] = dst->u64VirAddr[0] + dst->u32PicStride[0] * dst->u32Height;
        break;
    case axdl_color_space_nv21:
        dst->enImgFormat = AX_FORMAT_YUV420_SEMIPLANAR_VU;
        dst->u32PicStride[1] = dst->u32PicStride[0];
        dst->u64PhyAddr[1] = dst->u64PhyAddr[0] + dst->u32PicStride[0] * dst->u32Height;
        dst->u64VirAddr[1] = dst->u64VirAddr[0] + dst->u32PicStride[0] * dst->u32Height;
        break;
    case axdl_color_space_bgr:
        dst->enImgFormat = AX_FORMAT_BGR888;
        break;
    case axdl_color_space_rgb:
        dst->enImgFormat = AX_FORMAT_RGB888;
        break;
    default:
        ALOGE("UNKNOW TYPE");
        break;
    }
}

int ax_imgproc_csc(axdl_image_t *src, axdl_image_t *dst)
{
    AX_VIDEO_FRAME_T ive_src, ive_dst;
    cvt(src, &ive_src);
    cvt(dst, &ive_dst);
    return AX_IVPS_CscVpp(&ive_src, &ive_dst);
}
int ax_imgproc_warp(axdl_image_t *src, axdl_image_t *dst, const float *pMat33, const int const_val)
{
    AX_VIDEO_FRAME_T ive_src, ive_dst;
    cvt(src, &ive_src);
    cvt(dst, &ive_dst);
    AX_IVPS_DEWARP_ATTR_T attr;
    memset(&attr, 0, sizeof(attr));
    attr.nDstWidth = ive_dst.u32Width;
    attr.nDstHeight = ive_dst.u32Height;
    attr.nDstStride = ive_dst.u32PicStride[0];
    attr.bPerspective = AX_TRUE;
    attr.eImgFormat = ive_src.enImgFormat;
    for (size_t i = 0; i < 9; i++)
    {
        attr.tPerspectiveAttr.nMatrix[i] = AX_S64(pMat33[i] * 1000000);
    }

    return AX_IVPS_Dewarp(&ive_src, &ive_dst, &attr);
}

int _ax_imgproc_crop_resize(axdl_image_t *src, axdl_image_t *dst, axdl_bbox_t *box, AX_IVPS_ASPECT_RATIO_T *resize_ctrl)
{
    AX_VIDEO_FRAME_T ive_src, ive_dst;
    cvt(src, &ive_src);
    cvt(dst, &ive_dst);

    AX_IVPS_RECT_T Box = {0, 0, (AX_U16)ive_src.u32Width, (AX_U16)ive_src.u32Height};
    AX_IVPS_RECT_T pbox[1] = {0};
    if (box)
    {
        box->x = MAX((int)box->x, 0);
        box->y = MAX((int)box->y, 0);

        box->w = MIN((int)box->w, (int)src->nWidth - (int)box->x);
        box->h = MIN((int)box->h, (int)src->nHeight - (int)box->y);

        box->w = int(box->w) - int(box->w) % 2;
        box->h = int(box->h) - int(box->h) % 2;

        Box.nX = box->x;
        Box.nY = box->y;
        Box.nW = box->w;
        Box.nH = box->h;
        pbox[0] = Box;
    }
    pbox[0] = Box;

    AX_VIDEO_FRAME_T *p_ive_dst = &ive_dst;

    return AX_IVPS_CropResizeV2Vpp(&ive_src, pbox, 1, &p_ive_dst, resize_ctrl);
}

int ax_imgproc_crop_resize(axdl_image_t *src, axdl_image_t *dst, axdl_bbox_t *box)
{
    AX_IVPS_ASPECT_RATIO_T resize_ctrl;
    memset(&resize_ctrl, 0, sizeof(resize_ctrl));
    resize_ctrl.nBgColor = 0xFFFFFF;
    resize_ctrl.eMode = AX_IVPS_ASPECT_RATIO_STRETCH;
    return _ax_imgproc_crop_resize(src, dst, box, &resize_ctrl);
}
int ax_imgproc_crop_resize_keep_ratio(axdl_image_t *src, axdl_image_t *dst, axdl_bbox_t *box)
{
    AX_IVPS_ASPECT_RATIO_T resize_ctrl;
    memset(&resize_ctrl, 0, sizeof(resize_ctrl));
    resize_ctrl.nBgColor = 0xFFFFFF;
    resize_ctrl.eMode = AX_IVPS_ASPECT_RATIO_AUTO;
    resize_ctrl.eAligns[0] = AX_IVPS_ASPECT_RATIO_HORIZONTAL_CENTER;
    resize_ctrl.eAligns[1] = AX_IVPS_ASPECT_RATIO_VERTICAL_CENTER;
    return _ax_imgproc_crop_resize(src, dst, box, &resize_ctrl);
}

int ax_imgproc_align_face(axdl_object_t *obj, axdl_image_t *src, axdl_image_t *dst)
{
    float scale = dst->nWidth / 112;
    float target[10] = {38.2946 * scale, 51.6963 * scale,
                        73.5318 * scale, 51.5014 * scale,
                        56.0252 * scale, 71.7366 * scale,
                        41.5493 * scale, 92.3655 * scale,
                        70.7299 * scale, 92.2041 * scale};
    float _tmp[10] = {obj->landmark[0].x, obj->landmark[0].y,
                      obj->landmark[1].x, obj->landmark[1].y,
                      obj->landmark[2].x, obj->landmark[2].y,
                      obj->landmark[3].x, obj->landmark[3].y,
                      obj->landmark[4].x, obj->landmark[4].y};
    float _m[6], _m_inv[6];
    get_affine_transform(_tmp, target, 5, _m);
    invert_affine_transform(_m, _m_inv);

#if 0
    dst->eDtype = src->eDtype;
    if (dst->eDtype == axdl_color_space_rgb || dst->eDtype == axdl_color_space_bgr)
    {
        dst->nSize = 112 * 112 * 3;

        warpaffine_bilinear_c3((const unsigned char *)src->pVir, src->nWidth, src->nHeight, (unsigned char *)dst->pVir, dst->nWidth, dst->nHeight, _m_inv, -233, 0);
    }
    // else if (dst->eDtype == axdl_color_space_nv12 || dst->eDtype == axdl_color_space_nv21)
    // {
    //     dst->nSize = 112 * 112 * 1.5;

    //      warpaffine_bilinear_yuv420sp((const unsigned char *)src->pVir, src->nWidth, src->nHeight, (const unsigned char *)dst->pVir, dst->nWidth, dst->nHeight, _m, 0, 0);
    // }
    else
    {
        ALOGE("just only support BGR/RGB format");
    }
    return 0;

#else
    float mat3x3[9] = {
        _m_inv[0], _m_inv[1], _m_inv[2],
        _m_inv[3], _m_inv[4], _m_inv[5],
        0, 0, 1};

    dst->eDtype = src->eDtype;
    if (dst->eDtype == axdl_color_space_rgb || dst->eDtype == axdl_color_space_bgr)
    {
        dst->nSize = 112 * 112 * 3;
    }
    else if (dst->eDtype == axdl_color_space_nv12 || dst->eDtype == axdl_color_space_nv21)
    {
        dst->nSize = 112 * 112 * 1.5;
    }
    else
    {
        ALOGE("just only support BGR/RGB/NV12 format");
    }
    return ax_imgproc_warp(src, dst, &mat3x3[0], 128);
#endif
}
#elif defined(AXERA_TARGET_CHIP_AX620E)
void cvt(axdl_image_t *src, AX_VIDEO_FRAME_T *dst)
{
    memset(dst, 0, sizeof(AX_VIDEO_FRAME_T));
    dst->u32Height = src->nHeight;
    dst->u32Width = src->nWidth;
    dst->u32PicStride[0] = src->tStride_W;
    dst->u64PhyAddr[0] = src->pPhy;
    dst->u64VirAddr[0] = (AX_U64)src->pVir;
    switch (src->eDtype)
    {
    case axdl_color_space_nv12:
        dst->enImgFormat = AX_FORMAT_YUV420_SEMIPLANAR;
        dst->u32PicStride[1] = dst->u32PicStride[0];
        dst->u64PhyAddr[1] = dst->u64PhyAddr[0] + dst->u32PicStride[0] * dst->u32Height;
        dst->u64VirAddr[1] = dst->u64VirAddr[0] + dst->u32PicStride[0] * dst->u32Height;
        break;
    case axdl_color_space_nv21:
        dst->enImgFormat = AX_FORMAT_YUV420_SEMIPLANAR_VU;
        dst->u32PicStride[1] = dst->u32PicStride[0];
        dst->u64PhyAddr[1] = dst->u64PhyAddr[0] + dst->u32PicStride[0] * dst->u32Height;
        dst->u64VirAddr[1] = dst->u64VirAddr[0] + dst->u32PicStride[0] * dst->u32Height;
        break;
    case axdl_color_space_bgr:
        dst->enImgFormat = AX_FORMAT_BGR888;
        break;
    case axdl_color_space_rgb:
        dst->enImgFormat = AX_FORMAT_RGB888;
        break;
    default:
        ALOGE("UNKNOW TYPE");
        break;
    }
}

int ax_imgproc_csc(axdl_image_t *src, axdl_image_t *dst)
{
    AX_VIDEO_FRAME_T ive_src, ive_dst;
    cvt(src, &ive_src);
    cvt(dst, &ive_dst);
    return AX_IVPS_CscTdp(&ive_src, &ive_dst);
}
int ax_imgproc_warp(axdl_image_t *src, axdl_image_t *dst, const float *pMat33, const int const_val)
{
    AX_VIDEO_FRAME_T ive_src, ive_dst;
    cvt(src, &ive_src);
    cvt(dst, &ive_dst);
    AX_IVPS_DEWARP_ATTR_T attr;
    memset(&attr, 0, sizeof(attr));
    attr.eDewarpType = AX_IVPS_DEWARP_PERSPECTIVE;
    for (size_t i = 0; i < 9; i++)
    {
        attr.tPerspectiveAttr.nMatrix[i] = AX_S64(pMat33[i] * 1000000);
    }

    return AX_IVPS_Dewarp(&ive_src, &ive_dst, &attr);
}

int _ax_imgproc_crop_resize(axdl_image_t *src, axdl_image_t *dst, axdl_bbox_t *box, AX_IVPS_ASPECT_RATIO_T *resize_ctrl)
{
    AX_VIDEO_FRAME_T ive_src, ive_dst;
    cvt(src, &ive_src);
    cvt(dst, &ive_dst);

    AX_IVPS_RECT_T Box = {0, 0, (AX_U16)ive_src.u32Width, (AX_U16)ive_src.u32Height};
    AX_IVPS_RECT_T pbox[1] = {0};
    if (box)
    {
        box->x = MAX((int)box->x, 0);
        box->y = MAX((int)box->y, 0);

        box->w = MIN((int)box->w, (int)src->nWidth - (int)box->x);
        box->h = MIN((int)box->h, (int)src->nHeight - (int)box->y);

        box->w = int(box->w) - int(box->w) % 2;
        box->h = int(box->h) - int(box->h) % 2;

        Box.nX = box->x;
        Box.nY = box->y;
        Box.nW = box->w;
        Box.nH = box->h;
        pbox[0] = Box;
    }
    pbox[0] = Box;

    // AX_VIDEO_FRAME_T *p_ive_dst = ;

    AX_IVPS_CROP_RESIZE_ATTR_T tAttr;
    memset(&tAttr, 0, sizeof(tAttr));
    memcpy(&tAttr.tAspectRatio, resize_ctrl, sizeof(AX_IVPS_ASPECT_RATIO_T));

    return AX_IVPS_CropResizeV2Vpp(&ive_src, 1, pbox, &ive_dst, &tAttr);
}

int ax_imgproc_crop_resize(axdl_image_t *src, axdl_image_t *dst, axdl_bbox_t *box)
{
    AX_IVPS_ASPECT_RATIO_T resize_ctrl;
    memset(&resize_ctrl, 0, sizeof(resize_ctrl));
    resize_ctrl.nBgColor = 0xFFFFFF;
    resize_ctrl.eMode = AX_IVPS_ASPECT_RATIO_STRETCH;
    return _ax_imgproc_crop_resize(src, dst, box, &resize_ctrl);
}
int ax_imgproc_crop_resize_keep_ratio(axdl_image_t *src, axdl_image_t *dst, axdl_bbox_t *box)
{
    AX_IVPS_ASPECT_RATIO_T resize_ctrl;
    memset(&resize_ctrl, 0, sizeof(resize_ctrl));
    resize_ctrl.nBgColor = 0xFFFFFF;
    resize_ctrl.eMode = AX_IVPS_ASPECT_RATIO_AUTO;
    resize_ctrl.eAligns[0] = AX_IVPS_ASPECT_RATIO_HORIZONTAL_CENTER;
    resize_ctrl.eAligns[1] = AX_IVPS_ASPECT_RATIO_VERTICAL_CENTER;
    return _ax_imgproc_crop_resize(src, dst, box, &resize_ctrl);
}

int ax_imgproc_align_face(axdl_object_t *obj, axdl_image_t *src, axdl_image_t *dst)
{
    static float target[10] = {38.2946, 51.6963,
                               73.5318, 51.5014,
                               56.0252, 71.7366,
                               41.5493, 92.3655,
                               70.7299, 92.2041};
    float _tmp[10] = {obj->landmark[0].x, obj->landmark[0].y,
                      obj->landmark[1].x, obj->landmark[1].y,
                      obj->landmark[2].x, obj->landmark[2].y,
                      obj->landmark[3].x, obj->landmark[3].y,
                      obj->landmark[4].x, obj->landmark[4].y};
    float _m[6], _m_inv[6];
    get_affine_transform(_tmp, target, 5, _m);
    invert_affine_transform(_m, _m_inv);
    float mat3x3[9] = {
        _m_inv[0], _m_inv[1], _m_inv[2],
        _m_inv[3], _m_inv[4], _m_inv[5],
        0, 0, 1};

    dst->eDtype = src->eDtype;
    if (dst->eDtype == axdl_color_space_rgb || dst->eDtype == axdl_color_space_bgr)
    {
        dst->nSize = 112 * 112 * 3;
    }
    else if (dst->eDtype == axdl_color_space_nv12 || dst->eDtype == axdl_color_space_nv21)
    {
        dst->nSize = 112 * 112 * 1.5;
    }
    else
    {
        ALOGE("just only support BGR/RGB/NV12 format");
    }
    return ax_imgproc_warp(src, dst, &mat3x3[0], 128);
}
#endif

int ax_imgproc_crop_resize_warp(axdl_image_t *src, axdl_image_t *dst, axdl_bbox_t *box, double *m, double *m_inv)
{
    cv::Point2f src_pts[4];

    cv::Point2f dst_pts[4];
    dst_pts[0] = cv::Point2f(0, 0);
    dst_pts[1] = cv::Point2f(dst->nWidth, 0);
    dst_pts[2] = cv::Point2f(dst->nWidth, dst->nHeight);
    dst_pts[3] = cv::Point2f(0, dst->nHeight);

    if (box)
    {
        src_pts[0] = cv::Point2f(box->x, box->y);
        src_pts[1] = cv::Point2f(box->x + box->w, box->y);
        src_pts[2] = cv::Point2f(box->x + box->w, box->y + box->h);
        src_pts[3] = cv::Point2f(box->x, box->y + box->h);
    }
    else
    {
        src_pts[0] = cv::Point2f(0, 0);
        src_pts[1] = cv::Point2f(src->nWidth, 0);
        src_pts[2] = cv::Point2f(src->nWidth, src->nHeight);
        src_pts[3] = cv::Point2f(0, src->nHeight);
    }

    cv::Mat affine_trans_mat = cv::getAffineTransform(src_pts, dst_pts);
    cv::Mat affine_trans_mat_inv;
    cv::invertAffineTransform(affine_trans_mat, affine_trans_mat_inv);
    if (m)
        memcpy(m, affine_trans_mat.data, sizeof(double) * 6);
    if (m_inv)
        memcpy(m_inv, affine_trans_mat_inv.data, sizeof(double) * 6);

    float mat3x3[3][3] = {
        {(float)affine_trans_mat_inv.at<double>(0, 0), (float)affine_trans_mat_inv.at<double>(0, 1), (float)affine_trans_mat_inv.at<double>(0, 2)},
        {(float)affine_trans_mat_inv.at<double>(1, 0), (float)affine_trans_mat_inv.at<double>(1, 1), (float)affine_trans_mat_inv.at<double>(1, 2)},
        {0, 0, 1}};
    // //这里要用AX_NPU_MODEL_TYPE_1_1_2
    return ax_imgproc_warp(src, dst, &mat3x3[0][0], 128);
}

int ax_imgproc_crop_resize_keep_ratio_warp(axdl_image_t *src, axdl_image_t *dst, axdl_bbox_t *box, double *m, double *m_inv)
{
    cv::Point2f src_pts[4];

    cv::Point2f dst_pts[4];
    dst_pts[0] = cv::Point2f(0, 0);
    dst_pts[1] = cv::Point2f(dst->nWidth, 0);
    dst_pts[2] = cv::Point2f(dst->nWidth, dst->nHeight);
    dst_pts[3] = cv::Point2f(0, dst->nHeight);

    axdl_bbox_t bbox;
    if (box)
    {
        // bbox.x = box->x;
        // bbox.y = box->y;
        // bbox.w = box->w;
        // bbox.h = box->h;
        memcpy(&bbox, box, sizeof(axdl_bbox_t));
    }
    else
    {
        bbox.x = 0;
        bbox.y = 0;
        bbox.w = src->nWidth;
        bbox.h = src->nHeight;
    }

    if ((bbox.w / bbox.h) >
        (float(dst->nWidth) / float(dst->nHeight)))
    {
        float offset = ((bbox.w * (float(dst->nHeight) / float(dst->nWidth))) - bbox.h) / 2;

        src_pts[0] = cv::Point2f(bbox.x, bbox.y - offset);
        src_pts[1] = cv::Point2f(bbox.x + bbox.w, bbox.y - offset);
        src_pts[2] = cv::Point2f(bbox.x + bbox.w, bbox.y + bbox.h + offset);
        src_pts[3] = cv::Point2f(bbox.x, bbox.y + bbox.h + offset);
    }
    else
    {
        float offset = ((bbox.h * (float(dst->nWidth) / float(dst->nHeight))) - bbox.w) / 2;

        src_pts[0] = cv::Point2f(bbox.x - offset, bbox.y);
        src_pts[1] = cv::Point2f(bbox.x + bbox.w + offset, bbox.y);
        src_pts[2] = cv::Point2f(bbox.x + bbox.w + offset, bbox.y + bbox.h);
        src_pts[3] = cv::Point2f(bbox.x - offset, bbox.y + bbox.h);
    }

    cv::Mat affine_trans_mat = cv::getAffineTransform(src_pts, dst_pts);
    cv::Mat affine_trans_mat_inv;
    cv::invertAffineTransform(affine_trans_mat, affine_trans_mat_inv);
    if (m)
        memcpy(m, affine_trans_mat.data, sizeof(double) * 6);
    if (m_inv)
        memcpy(m_inv, affine_trans_mat_inv.data, sizeof(double) * 6);

    float mat3x3[3][3] = {
        {(float)affine_trans_mat_inv.at<double>(0, 0), (float)affine_trans_mat_inv.at<double>(0, 1), (float)affine_trans_mat_inv.at<double>(0, 2)},
        {(float)affine_trans_mat_inv.at<double>(1, 0), (float)affine_trans_mat_inv.at<double>(1, 1), (float)affine_trans_mat_inv.at<double>(1, 2)},
        {0, 0, 1}};
    // //这里要用AX_NPU_MODEL_TYPE_1_1_2
    return ax_imgproc_warp(src, dst, &mat3x3[0][0], 128);
}
