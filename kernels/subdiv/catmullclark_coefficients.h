// ======================================================================== //
// Copyright 2009-2017 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "../common/geometry.h"

namespace embree
{
//namespace isa
//{
  static const size_t MAX_PATCH_VALENCE = 16;         //!< maximal number of vertices of a patch
  static const size_t MAX_RING_FACE_VALENCE = 32;     //!< maximal number of faces per ring
  static const size_t MAX_RING_EDGE_VALENCE = 2*32;   //!< maximal number of edges per ring

  class CatmullClarkPrecomputedCoefficients 
  {
  private:
    
    float table_cos_2PI_div_n[MAX_RING_FACE_VALENCE+1];

    float* table_limittangent_a[MAX_RING_FACE_VALENCE+1];
    float* table_limittangent_b[MAX_RING_FACE_VALENCE+1];
    float table_limittangent_c[MAX_RING_FACE_VALENCE+1];

    __forceinline float set_cos_2PI_div_n(const size_t n) { 
      if (unlikely(n == 0)) return 1.0f;
      return cosf(2.0f*float(pi)/(float)n); 
    }

    __forceinline float set_limittangent_a(const size_t i, const size_t n)  
    { 
      if (unlikely(n == 0)) return 1.0f;
      const float c0 = 1.0f/(float)n * 1.0f / sqrtf(4.0f + cosf(float(pi)/(float)n)*cosf(float(pi)/(float)n));
      const float c1 = (1.0f/(float)n + cosf(float(pi)/(float)n) * c0); 
      return cosf(2.0f*float(pi)*(float)i/(float)n) * c1;
    }

    __forceinline float set_limittangent_b(const size_t i, const size_t n)  
    { 
      if (unlikely(n == 0)) return 1.0f;
      const float c0 = 1.0f/(float)n * 1.0f / sqrtf(4.0f + cosf(float(pi)/(float)n)*cosf(float(pi)/(float)n));
      return cosf((2.0f*float(pi)*i+float(pi))/(float)n) * c0;
    }

    __forceinline float set_limittangent_c(const size_t n)  
    { 
      if (unlikely(n == 0)) return 1.0f;
      return 2.0f/16.0f * (5.0f + cosf(2.0f*float(pi)/(float)n) + cosf(float(pi)/(float)n) * sqrtf(18.0f+2.0f*cosf(2.0f*float(pi)/(float)n)));
    }

  public:

    __forceinline float cos_2PI_div_n(const size_t n)
    {
      if (likely(n <= MAX_RING_FACE_VALENCE))
        return table_cos_2PI_div_n[n];
      else
        return set_cos_2PI_div_n(n);
    }

    __forceinline float limittangent_a(const size_t i, const size_t n)
    {
      assert(n <= MAX_RING_FACE_VALENCE);
      assert(i < n);
      return table_limittangent_a[n][i];
    }

    __forceinline float limittangent_b(const size_t i, const size_t n)
    {
      assert(n <= MAX_RING_FACE_VALENCE);
      assert(i < n);
      return table_limittangent_b[n][i];
    }

    __forceinline float limittangent_c(const size_t n)
    {
      assert(n <= MAX_RING_FACE_VALENCE);
      return table_limittangent_c[n];
    }

    static CatmullClarkPrecomputedCoefficients table;
 
    CatmullClarkPrecomputedCoefficients();    
    ~CatmullClarkPrecomputedCoefficients();    
  };
//}
}
