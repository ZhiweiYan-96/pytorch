#pragma once
#include <ATen/ATen.h>
#include <ATen/record_function.h>
#include "Utils.h"

#include <oneapi/dnnl/dnnl.hpp>

namespace at{
namespace native::xpu {
namespace onednn {

struct ReorderAttr {
 public:
  ReorderAttr(bool is_group = false)
      : pattr_(dnnl::primitive_attr()),
        src_has_sc_(false),
        src_has_zp_(false),
        dst_has_sc_(false),
        dst_has_zp_(false) {}

 public:
  // [Note: Scale setting for reorder]
  // For no post op on reorder, dst = src_scale * src / dst_scale;
  // dst_scale should be set carefully.
  void set_src_sc_mask(int mask) {
    pattr_.set_scales_mask(DNNL_ARG_SRC, mask);
    src_has_sc_ = true;
  }

  void set_src_zp_mask(int mask) {
    pattr_.set_zero_points_mask(DNNL_ARG_SRC, mask);
    src_has_zp_ = true;
  }

  void set_dst_sc_mask(int mask) {
    pattr_.set_scales_mask(DNNL_ARG_DST, mask);
    dst_has_sc_ = true;
  }

  void set_dst_zp_mask(int mask) {
    pattr_.set_zero_points_mask(DNNL_ARG_DST, mask);
    dst_has_zp_ = true;
  }

  dnnl::primitive_attr pattr() const {
    return pattr_;
  }

  bool src_has_sc() const {
    return src_has_sc_;
  }

  bool src_has_zp() const {
    return src_has_zp_;
  }

  bool dst_has_sc() const {
    return dst_has_sc_;
  }

  bool dst_has_zp() const {
    return dst_has_zp_;
  }

 private:
  dnnl::primitive_attr pattr_;
  bool src_has_sc_;
  bool src_has_zp_;
  bool dst_has_sc_;
  bool dst_has_zp_;
};

static inline dnnl::memory::desc check_group_and_create_plain_md(
    const at::Tensor& src,
    const at::Tensor& dst) {
  if (src.ndimension() == dst.ndimension()) {
    return dnnl::memory::desc(
        get_onednn_dims(src),
        get_onednn_dtype_include_double(src),
        get_onednn_strides(src));
  } else if (
      ((src.ndimension() == dst.ndimension() - 1) &&
       (src.size(0) == dst.size(0) * dst.size(1))) ||
      ((src.ndimension() == dst.ndimension() + 1) &&
       (dst.size(0) == src.size(0) * src.size(1)))) {
    // group tensor
    return dnnl::memory::desc(
        get_onednn_dims(dst),
        get_onednn_dtype_include_double(src),
        get_onednn_strides(dst.contiguous()));
  } else {
    TORCH_CHECK(0, "invalid src/dst dimension in oneDNN reorder ...");
  }
}

static inline void reorder(
    const at::Tensor& src,
    at::Tensor& dst,
    const ReorderAttr& rattr = ReorderAttr()) {
  RECORD_FUNCTION("dnnl_reorder", std::vector<c10::IValue>({src}));

  if (dst.is_same(src))
    return;

  auto engine =
      GpuEngineManager::Instance().get_engine({c10::kXPU, current_device()});
  auto strm = GpuStreamManager::Instance().get_stream();

  dnnl::memory::desc src_md = check_group_and_create_plain_md(src, dst);
  auto src_mem = xpu_onednn_memory(src_md, engine, src.data_ptr());

  dnnl::memory::desc dst_md = dnnl::memory::desc(
            get_onednn_dims(dst),
            get_onednn_dtype_include_double(dst),
            get_onednn_strides(dst));
  auto dst_mem = xpu_onednn_memory(dst_md, engine, dst.data_ptr());

  dnnl::primitive prim;
  prim = dnnl::reorder(src_mem, dst_mem);

  XPU_ONEDNN_EXEC(
      prim, strm, {{DNNL_ARG_SRC, src_mem}, {DNNL_ARG_DST, dst_mem}});
}


static inline void reorder_copy(const at::Tensor& src, at::Tensor& dst) {
  RECORD_FUNCTION("reorder_copy", std::vector<c10::IValue>({src}));
  xpu::onednn::reorder(src, dst);
}

} // namespace oneDNN
} // namespace xpu
} // namespace at
