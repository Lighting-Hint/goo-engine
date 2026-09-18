/* SPDX-FileCopyrightText: 2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gpu
 */

#pragma once

#include "GPU_vertex_buffer.hh"

#include <mutex>

#include "vk_buffer.hh"
#include "vk_data_conversion.hh"

namespace blender::gpu {

class VKVertexBuffer : public VertBuf {
  VKBuffer buffer_;
  /** When a vertex buffer is used as a UNIFORM_TEXEL_BUFFER the buffer requires a buffer view. */
  VkBufferView vk_buffer_view_ = VK_NULL_HANDLE;
  /**
   * Guards the lazy creation of `vk_buffer_view_`, which a drawing path can reach from several
   * threads at once. Without it two threads would each create a view and leak one of them.
   *
   * A plain mutex rather than a `std::once_flag`: `release_data()` discards the view, so the
   * creation is not a once-only operation, it is a "create it while the handle is null"
   * operation. A `once_flag` would stay set after the view is discarded and the next
   * `ensure_buffer_view()` would then return with `vk_buffer_view_get()` still asserting.
   * The check inside the lock makes that reuse safe.
   */
  mutable std::mutex vk_buffer_view_mutex_;

  VertexFormatConverter vertex_format_converter;
  bool data_uploaded_ = false;

 public:
  ~VKVertexBuffer();

  void bind_as_ssbo(uint binding) override;
  void bind_as_texture(uint binding) override;
  void wrap_handle(uint64_t handle) override;

  void update_sub(uint start, uint len, const void *data) override;
  void read(void *data) const override;

  VkBuffer vk_handle() const
  {
    BLI_assert(buffer_.is_allocated());
    return buffer_.vk_handle();
  }

  VkBufferView vk_buffer_view_get() const
  {
    /* Synchronized with `ensure_buffer_view()` and `release_data()`, so a caller cannot read a
     * view that another thread is in the middle of discarding. */
    std::scoped_lock lock(vk_buffer_view_mutex_);
    BLI_assert(vk_buffer_view_ != VK_NULL_HANDLE);
    return vk_buffer_view_;
  }

  void device_format_ensure();
  const GPUVertFormat &device_format_get() const;
  void ensure_updated();
  void ensure_buffer_view();

 protected:
  void acquire_data() override;
  void resize_data() override;
  void release_data() override;
  void upload_data() override;
  void duplicate_data(VertBuf *dst) override;

 private:
  void allocate();

  void upload_data_direct(const VKBuffer &host_buffer);
  void upload_data_via_staging_buffer(VKContext &context);

  /* VKTexture requires access to `buffer_` to convert a vertex buffer to a texture. */
  friend class VKTexture;
};

BLI_INLINE VKVertexBuffer *unwrap(VertBuf *vertex_buffer)
{
  return static_cast<VKVertexBuffer *>(vertex_buffer);
}

}  // namespace blender::gpu
