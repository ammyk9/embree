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

#include "../common/default.h"
#include "../common/primref.h"
#include "../common/primref_mb.h"

namespace embree
{
  //namespace isa
  //{
    template<typename BBox>
      class CentGeom
    {
    public:
      __forceinline CentGeom () {}

      __forceinline CentGeom (EmptyTy) 
	: geomBounds(empty), centBounds(empty), types(0) {}
      
      __forceinline CentGeom (const BBox& geomBounds, const BBox3fa& centBounds, unsigned types) 
	: geomBounds(geomBounds), centBounds(centBounds), types(types) {}
      
      __forceinline bool oneType() const {
        return ((types-1)&types) == 0;
      }

      __forceinline bool hasType( const Leaf::Type ty ) const {
        return types & Leaf::typeMask(ty);
      }

      __forceinline bool isType( const Leaf::Type ty ) const {
        return types == Leaf::typeMask(ty);
      }

      __forceinline bool isType( const Leaf::Type ty0, const Leaf::Type ty1) const 
      {
        const unsigned mask = Leaf::typeMask(ty0) | Leaf::typeMask(ty1);
        return (types & mask) && !(types & ~mask);
      }

      __forceinline bool hasMBlur() const {
        return types & Leaf::typeMaskMBlur();
      }

      template<typename PrimRef> 
        __forceinline void extend_primref(const PrimRef& prim) 
      {
        BBox bounds; Vec3fa center;
        prim.binBoundsAndCenter(bounds,center);
        geomBounds.extend(bounds);
        centBounds.extend(center);
        types |= Leaf::typeMask(prim.type());
      }

      template<typename PrimRef> 
        __forceinline void extend_center2(const PrimRef& prim) 
      {
        BBox3fa bounds = prim.bounds();
        geomBounds.extend(bounds);
        centBounds.extend(bounds.center2());
        types |= Leaf::typeMask(prim.type());
      }
      
      __forceinline void extend(const BBox& geomBounds_, const Leaf::Type ty) {
	geomBounds.extend(geomBounds_);
	centBounds.extend(center2(geomBounds_));
        types |= Leaf::typeMask(ty);
      }

      __forceinline void merge(const CentGeom& other) 
      {
	geomBounds.extend(other.geomBounds);
	centBounds.extend(other.centBounds);
        types |= other.types;
      }

      static __forceinline const CentGeom extend( CentGeom& a, const CentGeom& b ) {
        a.extend(b);
      }
      static __forceinline const CentGeom merge2(const CentGeom& a, const CentGeom& b) {
        CentGeom r = a; r.merge(b); return r;
      }

    public:
      BBox geomBounds;   //!< geometry bounds of primitives
      BBox3fa centBounds;   //!< centroid bounds of primitives
      unsigned types;          //!< types of primitives
    };

    typedef CentGeom<BBox3fa> CentGeomBBox3fa;

    /*! stores bounding information for a set of primitives */
    template<typename BBox>
      class PrimInfoT : public CentGeom<BBox>
    {
    public:
      using CentGeom<BBox>::geomBounds;
      using CentGeom<BBox>::centBounds;

      __forceinline PrimInfoT () {}

      __forceinline PrimInfoT (EmptyTy) 
	: CentGeom<BBox>(empty), begin(0), end(0) {}

      __forceinline PrimInfoT (size_t begin, size_t end, const CentGeomBBox3fa& centGeomBounds) 
        : CentGeom<BBox>(centGeomBounds), begin(begin), end(end) {}

      template<typename PrimRef> 
        __forceinline void add_primref(const PrimRef& prim) 
      {
        CentGeom<BBox>::extend_primref(prim);
        end++;
      }
      
       template<typename PrimRef> 
         __forceinline void add_center2(const PrimRef& prim) {
         CentGeom<BBox>::extend_center2(prim);
         end++;
       }

        template<typename PrimRef> 
          __forceinline void add_center2(const PrimRef& prim, const size_t i) {
          CentGeom<BBox>::extend_center2(prim);
          end+=i;
        }
        
      __forceinline void add(const BBox& geomBounds_, Leaf::Type type) {
        CentGeom<BBox>::extend(geomBounds_,type);
        end++;
      }
      
      __forceinline void merge(const PrimInfoT& other) 
      {
	CentGeom<BBox>::merge(other);
        begin += other.begin;
	end += other.end;
      }

      static __forceinline const PrimInfoT merge(const PrimInfoT& a, const PrimInfoT& b) {
        PrimInfoT r = a; r.merge(b); return r;
      }
      static __forceinline const PrimInfoT merge2(const PrimInfoT& a, const PrimInfoT& b) {
        PrimInfoT r = a; r.merge(b); return r;
      }

      /*! returns the number of primitives */
      __forceinline size_t size() const { 
	return end-begin; 
      }

      __forceinline float halfArea() {
        return expectedApproxHalfArea(geomBounds);
      }

      __forceinline float leafSAH() const { 
	return expectedApproxHalfArea(geomBounds)*float(size()); 
	//return halfArea(geomBounds)*blocks(num); 
      }
      
      __forceinline float leafSAH(size_t sahBlockSize) const {
	return expectedApproxHalfArea(geomBounds)*float((size()+sahBlockSize-1) / sahBlockSize);
	//return halfArea(geomBounds)*float((num+3) >> 2);
	//return halfArea(geomBounds)*blocks(num); 
      }
      
      /*! stream output */
      friend std::ostream& operator<<(std::ostream& cout, const PrimInfoT& pinfo) {
	return cout << "PrimInfo { begin = " << pinfo.begin << ", end = " << pinfo.end << ", types = " << pinfo.types << ", geomBounds = " << pinfo.geomBounds << ", centBounds = " << pinfo.centBounds << "}";
      }
      
    public:
      size_t begin,end;          //!< number of primitives
    };

    typedef PrimInfoT<BBox3fa> PrimInfo;
    //typedef PrimInfoT<LBBox3fa> PrimInfoMB;

    /*! stores bounding information for a set of primitives */
    template<typename BBox>
      class PrimInfoMBT : public CentGeom<BBox>
    {
    public:
      using CentGeom<BBox>::geomBounds;
      using CentGeom<BBox>::centBounds;

      __forceinline PrimInfoMBT () {
      } 

      __forceinline PrimInfoMBT (EmptyTy)
        : CentGeom<BBox>(empty), object_range(0,0), num_time_segments(0), max_num_time_segments(0), time_range(0.0f,1.0f) {}

      __forceinline PrimInfoMBT (size_t begin, size_t end)
        : CentGeom<BBox>(empty), object_range(begin,end), num_time_segments(0), max_num_time_segments(0), time_range(0.0f,1.0f) {}

      template<typename PrimRef> 
        __forceinline void add_primref(const PrimRef& prim) 
      {
        CentGeom<BBox>::extend_primref(prim);
        object_range._end++;
        num_time_segments += prim.size();
        max_num_time_segments = max(max_num_time_segments,size_t(prim.totalTimeSegments()));
      }

      __forceinline void merge(const PrimInfoMBT& other)
      {
        CentGeom<BBox>::merge(other);
        object_range._begin += other.object_range.begin();
	object_range._end += other.object_range.end();
        num_time_segments += other.num_time_segments;
        max_num_time_segments = max(max_num_time_segments,other.max_num_time_segments);
      }

      static __forceinline const PrimInfoMBT merge2(const PrimInfoMBT& a, const PrimInfoMBT& b) {
        PrimInfoMBT r = a; r.merge(b); return r;
      }
      
      /*! returns the number of primitives */
      __forceinline size_t size() const { 
	return object_range.size(); 
      }

      __forceinline float halfArea() const {
        return time_range.size()*expectedApproxHalfArea(geomBounds);
      }

      __forceinline float leafSAH() const { 
	return time_range.size()*expectedApproxHalfArea(geomBounds)*float(num_time_segments); 
      }
      
      __forceinline float leafSAH(size_t sahBlockSize) const {
	return time_range.size()*expectedApproxHalfArea(geomBounds)*float((num_time_segments+sahBlockSize-1) / sahBlockSize);
      }
      
      /*! stream output */
      friend std::ostream& operator<<(std::ostream& cout, const PrimInfoMBT& pinfo) 
      {
	return cout << "PrimInfo { " << 
          "object_range = " << pinfo.object_range << 
          ", time_range = " << pinfo.time_range << 
          ", time_segments = " << pinfo.num_time_segments << 
          ", geomBounds = " << pinfo.geomBounds << 
          ", centBounds = " << pinfo.centBounds << 
          "}";
      }
      
    public:
      range<size_t> object_range; //!< primitive range
      size_t num_time_segments;  //!< total number of time segments of all added primrefs
      size_t max_num_time_segments; //!< maximal number of time segments of a primitive
      BBox1f time_range;
    };

    typedef PrimInfoMBT<typename PrimRefMB::BBox> PrimInfoMB;

    struct SetMB : public PrimInfoMB
    {
      static const size_t PARALLEL_THRESHOLD = 3 * 1024;
      static const size_t PARALLEL_FIND_BLOCK_SIZE = 1024;
      static const size_t PARALLEL_PARTITION_BLOCK_SIZE = 128;

      typedef mvector<PrimRefMB>* PrimRefVector;

      __forceinline SetMB() {}

      __forceinline SetMB(const PrimInfoMB& pinfo_i, PrimRefVector prims)
        : PrimInfoMB(pinfo_i), prims(prims) {}

      __forceinline SetMB(const PrimInfoMB& pinfo_i, PrimRefVector prims, range<size_t> object_range_in, BBox1f time_range_in) // FIXME: remove
        : PrimInfoMB(pinfo_i), prims(prims)
      {
        object_range = object_range_in;
        time_range = time_range_in;
      }
      
      __forceinline SetMB(const PrimInfoMB& pinfo_i, PrimRefVector prims, BBox1f time_range_in)
        : PrimInfoMB(pinfo_i), prims(prims)
      {
        object_range = range<size_t>(0,prims->size());
        time_range = time_range_in;
      }

      void deterministic_order() const 
      {
        /* required as parallel partition destroys original primitive order */
        PrimRefMB* prim = prims->data();
        std::sort(&prim[object_range.begin()],&prim[object_range.end()]);
      }

      template<typename RecalculatePrimRef>
      __forceinline LBBox3fa linearBounds(const RecalculatePrimRef& recalculatePrimRef) const
      {
        auto reduce = [&](const range<size_t>& r) -> LBBox3fa
        {
          LBBox3fa cbounds(empty);
          for (size_t j = r.begin(); j < r.end(); j++)
          {
            PrimRefMB& ref = (*prims)[j];
            const LBBox3fa bn = recalculatePrimRef.linearBounds(ref, time_range);
            cbounds.extend(bn);
          };
          return cbounds;
        };
        
        return parallel_reduce(object_range.begin(), object_range.end(), PARALLEL_FIND_BLOCK_SIZE, PARALLEL_THRESHOLD, LBBox3fa(empty),
                               reduce,
                               [&](const LBBox3fa& b0, const LBBox3fa& b1) -> LBBox3fa { return embree::merge(b0, b1); });
      }

      template<typename RecalculatePrimRef>
        __forceinline LBBox3fa linearBounds(const RecalculatePrimRef& recalculatePrimRef, const LinearSpace3fa& space) const
      {
        auto reduce = [&](const range<size_t>& r) -> LBBox3fa
        {
          LBBox3fa cbounds(empty);
          for (size_t j = r.begin(); j < r.end(); j++)
          {
            PrimRefMB& ref = (*prims)[j];
            const LBBox3fa bn = recalculatePrimRef.linearBounds(ref, time_range, space);
            cbounds.extend(bn);
          };
          return cbounds;
        };
        
        return parallel_reduce(object_range.begin(), object_range.end(), PARALLEL_FIND_BLOCK_SIZE, PARALLEL_THRESHOLD, LBBox3fa(empty),
                               reduce,
                               [&](const LBBox3fa& b0, const LBBox3fa& b1) -> LBBox3fa { return embree::merge(b0, b1); });
      }

      template<typename RecalculatePrimRef>
        const SetMB primInfo(const RecalculatePrimRef& recalculatePrimRef, const LinearSpace3fa& space) const
      {
        auto computePrimInfo = [&](const range<size_t>& r) -> PrimInfoMB
        {
          PrimInfoMB pinfo(empty);
          for (size_t j=r.begin(); j<r.end(); j++)
          {
            PrimRefMB& ref = (*prims)[j];
            PrimRefMB ref1 = recalculatePrimRef(ref,time_range,space);
            pinfo.add_primref(ref1);
          };
          return pinfo;
        };
        
        const PrimInfoMB pinfo = parallel_reduce(object_range.begin(), object_range.end(), PARALLEL_FIND_BLOCK_SIZE, PARALLEL_THRESHOLD, 
                                                 PrimInfoMB(empty), computePrimInfo, PrimInfoMB::merge2);

        return SetMB(pinfo,prims,object_range,time_range);
      }
      
    public:
      PrimRefVector prims;
    };
  //}
}
