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

#include "primitive.h"

namespace embree
{
  /* Stores the vertices of M quads in struct of array layout */
  template <int M>
  struct QuadMv
  { 
  public:
    struct Type : public PrimitiveType 
    {
      Type();
      size_t size(const char* This) const;
      bool last(const char* This) const;
    };
    static Type type;
    static const Leaf::Type leaf_type = Leaf::TY_QUAD;

  public:

    /* Returns maximal number of stored quads */
    static __forceinline size_t max_size() { return M; }
    
    /* Returns required number of primitive blocks for N primitives */
    static __forceinline size_t blocks(size_t N) { return (N+max_size()-1)/max_size(); }
   
  public:

    /* Default constructor */
    __forceinline QuadMv() {}

    /* Construction from vertices and IDs */
    __forceinline QuadMv(const Vec3vf<M>& v0, const Vec3vf<M>& v1, const Vec3vf<M>& v2, const Vec3vf<M>& v3, const vint<M>& geomIDs, const vint<M>& primIDs, const bool last)
      : v0(v0), v1(v1), v2(v2), v3(v3), geomIDs(Leaf::vencode(Leaf::TY_QUAD,geomIDs,last)), primIDs(primIDs) {}
    
    /* Returns a mask that tells which quads are valid */
    __forceinline vbool<M> valid() const { return geomIDs != vint<M>(-1); }

    /* Returns true if the specified quad is valid */
    __forceinline bool valid(const size_t i) const { assert(i<M); return geomIDs[i] != -1; }

    /* Returns the number of stored quads */
    __forceinline size_t size() const { return __bsf(~movemask(valid())); }

    /*! checks if this is the last primitive */
    __forceinline unsigned last() const { return Leaf::decodeLast(geomIDs[0]); }

    /* Returns the geometry IDs */
    __forceinline       vint<M>& geomID()       { return geomIDs; }
    __forceinline const vint<M>& geomID() const { return geomIDs; }
    __forceinline unsigned geomID(const size_t i) const { assert(i<M); return Leaf::decodeID(geomIDs[i]); }

    /* Returns the primitive IDs */
    __forceinline       vint<M> primID()       { return primIDs; }
    __forceinline const vint<M> primID() const { return primIDs; }
    __forceinline unsigned primID(const size_t i) const { assert(i<M); return primIDs[i]; }

    /* returns area of quads */
    __forceinline float area() {
      const float A0 = reduce_add(0.5f*length(cross(v1-v0,v3-v0)));
      const float A1 = reduce_add(0.5f*length(cross(v1-v2,v3-v2)));
      return A0+A1;
    }
    
    /* Calculate the bounds of the quads */
    __forceinline BBox3fa bounds() const 
    {
      Vec3vf<M> lower = min(v0,v1,v2,v3);
      Vec3vf<M> upper = max(v0,v1,v2,v3);
      vbool<M> mask = valid();
      lower.x = select(mask,lower.x,vfloat<M>(pos_inf));
      lower.y = select(mask,lower.y,vfloat<M>(pos_inf));
      lower.z = select(mask,lower.z,vfloat<M>(pos_inf));
      upper.x = select(mask,upper.x,vfloat<M>(neg_inf));
      upper.y = select(mask,upper.y,vfloat<M>(neg_inf));
      upper.z = select(mask,upper.z,vfloat<M>(neg_inf));
      return BBox3fa(Vec3fa(reduce_min(lower.x),reduce_min(lower.y),reduce_min(lower.z)),
                     Vec3fa(reduce_max(upper.x),reduce_max(upper.y),reduce_max(upper.z)));
    }
    
    /* Non temporal store */
    __forceinline static void store_nt(QuadMv* dst, const QuadMv& src)
    {
      vfloat<M>::store_nt(&dst->v0.x,src.v0.x);
      vfloat<M>::store_nt(&dst->v0.y,src.v0.y);
      vfloat<M>::store_nt(&dst->v0.z,src.v0.z);
      vfloat<M>::store_nt(&dst->v1.x,src.v1.x);
      vfloat<M>::store_nt(&dst->v1.y,src.v1.y);
      vfloat<M>::store_nt(&dst->v1.z,src.v1.z);
      vfloat<M>::store_nt(&dst->v2.x,src.v2.x);
      vfloat<M>::store_nt(&dst->v2.y,src.v2.y);
      vfloat<M>::store_nt(&dst->v2.z,src.v2.z);
      vfloat<M>::store_nt(&dst->v3.x,src.v3.x);
      vfloat<M>::store_nt(&dst->v3.y,src.v3.y);
      vfloat<M>::store_nt(&dst->v3.z,src.v3.z);
      vint<M>::store_nt(&dst->geomIDs,src.geomIDs);
      vint<M>::store_nt(&dst->primIDs,src.primIDs);
    }

    /* Fill quad from quad list */
    template<typename PrimRef>
    __forceinline BBox3fa fill(const PrimRef* prims, size_t& begin, size_t end, Scene* scene, bool last)
    {
      vint<M> vgeomID = -1, vprimID = -1;
      Vec3vf<M> v0 = zero, v1 = zero, v2 = zero, v3 = zero;
      
      BBox3fa bounds = empty;
      for (size_t i=0; i<M && begin<end; i++, begin++)
      {
	const PrimRef& prim = prims[begin];
        const unsigned geomID = prim.geomID();
        const unsigned primID = prim.primID();
        const QuadMesh* __restrict__ const mesh = scene->get<QuadMesh>(geomID);
        const QuadMesh::Quad& quad = mesh->quad(primID);
        const Vec3fa& p0 = mesh->vertex(quad.v[0]);
        const Vec3fa& p1 = mesh->vertex(quad.v[1]);
        const Vec3fa& p2 = mesh->vertex(quad.v[2]);
        const Vec3fa& p3 = mesh->vertex(quad.v[3]);
        bounds.extend(p0);
        bounds.extend(p1);
        bounds.extend(p2);
        bounds.extend(p3);
        vgeomID [i] = geomID;
        vprimID [i] = primID;
        v0.x[i] = p0.x; v0.y[i] = p0.y; v0.z[i] = p0.z;
        v1.x[i] = p1.x; v1.y[i] = p1.y; v1.z[i] = p1.z;
        v2.x[i] = p2.x; v2.y[i] = p2.y; v2.z[i] = p2.z;
        v3.x[i] = p3.x; v3.y[i] = p3.y; v3.z[i] = p3.z;
      }
      QuadMv::store_nt(this,QuadMv(v0,v1,v2,v3,vgeomID,vprimID,last));
      return bounds;
    }

    template<typename BVH>
    __forceinline static typename BVH::NodeRef createLeaf(const FastAllocator::CachedAllocator& alloc, PrimRef* prims, const range<size_t>& range, BVH* bvh)
    {
      size_t cur = range.begin();
      size_t items = blocks(range.size());
      QuadMv* accel = (QuadMv*) alloc.malloc1(items*sizeof(QuadMv),BVH::byteAlignment);
      for (size_t i=0; i<items; i++) {
        accel[i].fill(prims,cur,range.end(),bvh->scene,i==(items-1));
      }
      return BVH::encodeLeaf((char*)accel,Leaf::TY_QUAD);
    }

    template<typename BVH>
    __forceinline static const typename BVH::NodeRecordMB4D createLeafMB (const SetMB& set, const FastAllocator::CachedAllocator& alloc, BVH* bvh)
    {
      size_t items = blocks(set.object_range.size());
      size_t start = set.object_range.begin();
      QuadMv* accel = (QuadMv*) alloc.malloc1(items*sizeof(QuadMv),BVH::byteAlignment);
      typename BVH::NodeRef node = bvh->encodeLeaf((char*)accel,Leaf::TY_QUAD);
      float A = 0.0f;
      LBBox3fa allBounds = empty;
      for (size_t i=0; i<items; i++) {
        const BBox3fa b = accel[i].fill(set.prims->data(), start, set.object_range.end(), bvh->scene, i==(items-1)); 
        allBounds.extend(LBBox3fa(b));
        A += accel[i].area();
      }
      return typename BVH::NodeRecordMB4D(node,allBounds,set.time_range,A,1.5f*items);
    }

    /* Updates the primitive */
    __forceinline BBox3fa update(QuadMesh* mesh)
    {
      BBox3fa bounds = empty;
      vint<M> vgeomID = -1, vprimID = -1;
      Vec3vf<M> v0 = zero, v1 = zero, v2 = zero;
	
      for (size_t i=0; i<M; i++)
      {
        if (!valid(i)) break;
        const unsigned geomId = geomIDs[i]; // copies last bit
        const unsigned primId = primID(i);
        const QuadMesh::Quad& quad = mesh->quad(primId);
        const Vec3fa p0 = mesh->vertex(quad.v[0]);
        const Vec3fa p1 = mesh->vertex(quad.v[1]);
        const Vec3fa p2 = mesh->vertex(quad.v[2]);
        const Vec3fa p3 = mesh->vertex(quad.v[3]);
        bounds.extend(merge(BBox3fa(p0),BBox3fa(p1),BBox3fa(p2),BBox3fa(p3)));
        vgeomID [i] = geomId;
        vprimID [i] = primId;
        v0.x[i] = p0.x; v0.y[i] = p0.y; v0.z[i] = p0.z;
        v1.x[i] = p1.x; v1.y[i] = p1.y; v1.z[i] = p1.z;
        v2.x[i] = p2.x; v2.y[i] = p2.y; v2.z[i] = p2.z;
        v3.x[i] = p3.x; v3.y[i] = p3.y; v3.z[i] = p3.z;
      }
      QuadMv::store_nt(this,QuadMv(v0,v1,v2,v3,vgeomID,vprimID,false));
      return bounds;
    }
   
  public:
    Vec3vf<M> v0;      // 1st vertex of the quads
    Vec3vf<M> v1;      // 2nd vertex of the quads
    Vec3vf<M> v2;      // 3rd vertex of the quads
    Vec3vf<M> v3;      // 4rd vertex of the quads
    vint<M> geomIDs; // geometry ID
    vint<M> primIDs; // primitive ID
  };

  template<int M>
  typename QuadMv<M>::Type QuadMv<M>::type;

  typedef QuadMv<4> Quad4v;
}
