/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gpu
 */

#include "BLI_index_range.hh"

#include "vk_resource_state_tracker.hh"

namespace blender::gpu::render_graph {

/* -------------------------------------------------------------------- */
/** \name Adding resources
 * \{ */
ResourceHandle VKResourceStateTracker::create_resource_slot()
{
  ResourceHandle handle;
  if (unused_handles_.is_empty()) {
    handle = resources_.size();
  }
  else {
    handle = unused_handles_.pop_last();
  }

  Resource new_resource = {};
  resources_.add_new(handle, new_resource);
  return handle;
}

void VKResourceStateTracker::add_image(VkImage vk_image, uint32_t layer_count, const char *name)
{
  UNUSED_VARS_NDEBUG(name);
  BLI_assert_msg(!image_resources_.contains(vk_image),
                 "Image resource is added twice to the render graph.");
  std::scoped_lock lock(mutex);
  ResourceHandle handle = create_resource_slot();
  Resource &resource = resources_.lookup(handle);
  image_resources_.add_new(vk_image, handle);

  resource.type = VKResourceType::IMAGE;
  resource.image.vk_image = vk_image;
  resource.image.layer_count = layer_count;
  resource.stamp = 0;
#ifndef NDEBUG
  resource.name = name;
#endif

#ifdef VK_RESOURCE_STATE_TRACKER_VALIDATION
  validate();
#endif
}

void VKResourceStateTracker::add_buffer(VkBuffer vk_buffer, const char *name)
{
  UNUSED_VARS_NDEBUG(name);
  BLI_assert_msg(!buffer_resources_.contains(vk_buffer),
                 "Buffer resource is added twice to the render graph.");
  std::scoped_lock lock(mutex);
  ResourceHandle handle = create_resource_slot();
  Resource &resource = resources_.lookup(handle);
  buffer_resources_.add_new(vk_buffer, handle);

  resource.type = VKResourceType::BUFFER;
  resource.buffer.vk_buffer = vk_buffer;
  resource.stamp = 0;
#ifndef NDEBUG
  resource.name = name;
#endif

#ifdef VK_RESOURCE_STATE_TRACKER_VALIDATION
  validate();
#endif
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Remove resources
 * \{ */

void VKResourceStateTracker::remove_buffer(VkBuffer vk_buffer)
{
  std::scoped_lock lock(mutex);
  ResourceHandle handle = buffer_resources_.pop(vk_buffer);
  resources_.pop(handle);
  unused_handles_.append(handle);

#ifdef VK_RESOURCE_STATE_TRACKER_VALIDATION
  validate();
#endif
}

void VKResourceStateTracker::remove_image(VkImage vk_image)
{
  std::scoped_lock lock(mutex);
  ResourceHandle handle = image_resources_.pop(vk_image);
  resources_.pop(handle);
  unused_handles_.append(handle);

#ifdef VK_RESOURCE_STATE_TRACKER_VALIDATION
  validate();
#endif
}

/** \} */

ResourceWithStamp VKResourceStateTracker::get_stamp(ResourceHandle handle,
                                                    const Resource &resource)
{
  ResourceWithStamp result;
  result.handle = handle;
  result.stamp = resource.stamp;
  return result;
}

ResourceWithStamp VKResourceStateTracker::get_and_increase_stamp(ResourceHandle handle,
                                                                 Resource &resource)
{
  ResourceWithStamp result = get_stamp(handle, resource);
  resource.stamp += 1;
  return result;
}

ResourceWithStamp VKResourceStateTracker::get_image_and_increase_stamp(VkImage vk_image)
{
  ResourceHandle handle = image_resources_.lookup(vk_image);
  Resource &resource = resources_.lookup(handle);
  return get_and_increase_stamp(handle, resource);
}

ResourceWithStamp VKResourceStateTracker::get_buffer_and_increase_stamp(VkBuffer vk_buffer)
{
  ResourceHandle handle = buffer_resources_.lookup(vk_buffer);
  Resource &resource = resources_.lookup(handle);
  return get_and_increase_stamp(handle, resource);
}

ResourceWithStamp VKResourceStateTracker::get_buffer(VkBuffer vk_buffer) const
{
  ResourceHandle handle = buffer_resources_.lookup(vk_buffer);
  const Resource &resource = resources_.lookup(handle);
  return get_stamp(handle, resource);
}

ResourceWithStamp VKResourceStateTracker::get_image(VkImage vk_image) const
{
  const ResourceHandle *found = image_resources_.lookup_ptr(vk_image);
  if (found == nullptr) {
    /* An image that was never registered cannot be tracked, and the handle lookup below would
     * read past the end of `resources_`. Registering a null handle is the way this happens in
     * practice: it reaches here through the descriptor sets, which skip such handles now, but a
     * future caller could still produce one.
     *
     * Return a neutral stamp rather than asserting. The render graph then schedules the node
     * without a dependency on the missing image, which is what a resource the graph does not
     * know about can be given without inventing state for it. */
    return {ResourceHandle(0), 0};
  }
  ResourceHandle handle = *found;
  const Resource &resource = resources_.lookup(handle);
  return get_stamp(handle, resource);
}

#ifdef VK_RESOURCE_STATE_TRACKER_VALIDATION
void VKResourceStateTracker::validate() const
{
  for (const Map<VkImage, ResourceHandle>::Item &item : image_resources_.items()) {
    for (ResourceHandle buffer_handle : buffer_resources_.values()) {
      BLI_assert(item.value != buffer_handle);
    }
    BLI_assert(resources_.contains(item.value));
    const Resource &resource = resources_.lookup(item.value);
    BLI_assert(resource.type == VKResourceType::IMAGE);
  }

  for (const Map<VkBuffer, ResourceHandle>::Item &item : buffer_resources_.items()) {
    for (ResourceHandle image_handle : image_resources_.values()) {
      BLI_assert(item.value != image_handle);
    }
    BLI_assert(resources_.contains(item.value));
    const Resource &resource = resources_.lookup(item.value);
    BLI_assert(resource.type == VKResourceType::BUFFER);
  }

  BLI_assert(resources_.size() == image_resources_.size() + buffer_resources_.size());
}
#endif

}  // namespace blender::gpu::render_graph
