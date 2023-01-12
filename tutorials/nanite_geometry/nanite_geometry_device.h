// Copyright 2009-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../common/tutorial/tutorial_device.h"
#include "../common/math/random_sampler.h"
#include "../common/core/differential_geometry.h"
#include "../common/tutorial/scene_device.h"

namespace embree {

  enum RenderMode {
    RENDER_PRIMARY        = 0,
    RENDER_DEBUG_GRIDS    = 1,
    RENDER_DEBUG_SUBGRIDS = 2,    
    RENDER_DEBUG_QUADS    = 3,
    RENDER_DEBUG_LOD      = 4,
    RENDER_DEBUG_CRACK_FIXING = 5,    
  };
  
struct TutorialData
{
  /* scene data */
  RTCScene g_scene;

};

#if __SYCL_COMPILER_VERSION >= 20210801
}
namespace sycl {
  template<> struct is_device_copyable<embree::TutorialData> : std::true_type {};
  template<> struct is_device_copyable<const embree::TutorialData> : std::true_type {};
}
namespace embree {
#endif

inline void TutorialData_Constructor(TutorialData* This)
{
  This->g_scene  = nullptr;
}

inline void TutorialData_Destructor(TutorialData* This)
{
  rtcReleaseScene (This->g_scene); This->g_scene = nullptr;
}

} // namespace embree
