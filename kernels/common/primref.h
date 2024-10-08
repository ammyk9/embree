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

#include "default.h"
#include "../geometry/leaftype.h"

namespace embree
{
  /*! A primitive reference stores the bounds of the primitive and its ID. */
  struct __aligned(16) PrimRef 
  {
    __forceinline PrimRef () {}

#if defined(__AVX__)
    __forceinline PrimRef(const PrimRef& v) { 
      vfloat8::storeu((float*)this,vfloat8::loadu((float*)&v));
    }
    __forceinline void operator=(const PrimRef& v) { 
      vfloat8::storeu((float*)this,vfloat8::loadu((float*)&v));
    }
#endif

    __forceinline PrimRef (const BBox3fa& bounds, unsigned int geomID, unsigned int primID) 
    {
      lower = bounds.lower; lower.a = geomID;
      upper = bounds.upper; upper.a = primID;
    }

    __forceinline PrimRef (const BBox3fa& bounds, Leaf::Type ty, unsigned int geomID, unsigned int primID) 
    {
      lower = bounds.lower; lower.a = Leaf::encode(ty,geomID);
      upper = bounds.upper; upper.a = primID;
    }

    __forceinline PrimRef (const BBox3fa& bounds, size_t id) 
    {
#if defined(__X86_64__)
      lower = bounds.lower; lower.u = id & 0xFFFFFFFF;
      upper = bounds.upper; upper.u = (id >> 32) & 0xFFFFFFFF;
#else
      lower = bounds.lower; lower.u = id;
      upper = bounds.upper; upper.u = 0;
#endif
    }

    /*! calculates twice the center of the primitive */
    __forceinline const Vec3fa center2() const {
      return lower+upper;
    }
    
    /*! return the bounding box of the primitive */
    __forceinline const BBox3fa bounds() const {
      return BBox3fa(lower,upper);
    }

    /*! size for bin heuristic is 1 */
    __forceinline unsigned size() const { 
      return 1;
    }

    /*! returns bounds and centroid used for binning */
    __forceinline void binBoundsAndCenter(BBox3fa& bounds_o, Vec3fa& center_o) const 
    {
      bounds_o = bounds();
      center_o = embree::center2(bounds_o);
    }

    __forceinline unsigned& geomIDref() {  // FIXME: remove !!!!!!!
      return lower.u;
    }
    __forceinline unsigned& primIDref() {  // FIXME: remove !!!!!!!
      return upper.u;
    }
    
    /*! returns the primitive type */
    __forceinline Leaf::Type type() const { 
      return Leaf::decodeTy(lower.a);
    }

    /*! returns the geometry ID */
    __forceinline unsigned geomID() const { 
      return Leaf::decodeID(lower.a);
    }

    /*! returns the primitive ID */
    __forceinline unsigned primID() const { 
      return upper.a;
    }

    /*! returns an size_t sized ID */
    __forceinline size_t ID() const { 
#if defined(__X86_64__)
      return size_t(lower.u) + (size_t(upper.u) << 32);
#else
      return size_t(lower.u);
#endif
    }

    /*! special function for operator< */
    __forceinline uint64_t ID64() const {
      return (((uint64_t)primID()) << 32) + (uint64_t)geomID();
    }
    
    /*! allows sorting the primrefs by ID */
    friend __forceinline bool operator<(const PrimRef& p0, const PrimRef& p1) {
      return p0.ID64() < p1.ID64();
    }

    /*! Outputs primitive reference to a stream. */
    friend __forceinline std::ostream& operator<<(std::ostream& cout, const PrimRef& ref) {
      return cout << "{ lower = " << ref.lower << ", upper = " << ref.upper << ", geomID = " << ref.geomID() << ", primID = " << ref.primID() << " }";
    }

  public:
    Vec3fa lower;     //!< lower bounds and geomID
    Vec3fa upper;     //!< upper bounds and primID
  };

  /*! fast exchange for PrimRefs */
  __forceinline void xchg(PrimRef& a, PrimRef& b)
  {
#if defined(__AVX__)
    const vfloat8 aa = vfloat8::loadu((float*)&a);
    const vfloat8 bb = vfloat8::loadu((float*)&b);
    vfloat8::storeu((float*)&a,bb);
    vfloat8::storeu((float*)&b,aa);
#else
    std::swap(a,b);
#endif
  }
}
