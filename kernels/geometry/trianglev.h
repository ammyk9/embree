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
  /* Stores the vertices of M triangles in struct of array layout */
  template <int M>
  struct TriangleMv
  { 
  public:
    struct Type : public PrimitiveType 
    {
      Type();
      size_t size(const char* This) const;
      bool last(const char* This) const;
    };
    static Type type;
    static const Leaf::Type leaf_type = Leaf::TY_TRIANGLE;

  public:

    /* Returns maximal number of stored triangles */
    static __forceinline size_t max_size() { return M; }
    
    /* Returns required number of primitive blocks for N primitives */
    static __forceinline size_t blocks(size_t N) { return (N+max_size()-1)/max_size(); }
   
  public:

    /* Default constructor */
    __forceinline TriangleMv() {}

    /* Construction from vertices and IDs */
    __forceinline TriangleMv(const Vec3vf<M>& v0, const Vec3vf<M>& v1, const Vec3vf<M>& v2, const vint<M>& geomIDs, const vint<M>& primIDs, const bool last)
      : v0(v0), v1(v1), v2(v2), geomIDs(Leaf::vencode(Leaf::TY_TRIANGLE,geomIDs,last)), primIDs(primIDs) {}
    
    /* Returns a mask that tells which triangles are valid */
    __forceinline vbool<M> valid() const { return geomIDs != vint<M>(-1); }

    /* Returns true if the specified triangle is valid */
    __forceinline bool valid(const size_t i) const { assert(i<M); return geomIDs[i] != -1; }

    /* Returns the number of stored triangles */
    __forceinline size_t size() const { return __bsf(~movemask(valid())); }

    /*! checks if this is the last primitive */
    __forceinline unsigned last() const { return Leaf::decodeLast(geomIDs[0]); }

    /* Returns the geometry IDs */
    __forceinline       vint<M>& geomID()       { return geomIDs; }
    __forceinline const vint<M>& geomID() const { return geomIDs; }
    __forceinline unsigned geomID(const size_t i) const { assert(i<M); return Leaf::decodeID(geomIDs[i]); }

    /* Returns the primitive IDs */
    __forceinline       vint<M>& primID()       { return primIDs; }
    __forceinline const vint<M>& primID() const { return primIDs; }
    __forceinline unsigned  primID(const size_t i) const { assert(i<M); return primIDs[i]; }

    /* Calculate the bounds of the triangles */
    __forceinline BBox3fa bounds() const 
    {
      Vec3vf<M> lower = min(v0,v1,v2);
      Vec3vf<M> upper = max(v0,v1,v2);
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
    __forceinline static void store_nt(TriangleMv* dst, const TriangleMv& src)
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
      vint<M>::store_nt(&dst->geomIDs,src.geomIDs);
      vint<M>::store_nt(&dst->primIDs,src.primIDs);
    }

    /* Fill triangle from triangle list */
    __forceinline void fill(const PrimRef* prims, size_t& begin, size_t end, Scene* scene, bool last)
    {
      vint<M> vgeomID = -1, vprimID = -1;
      Vec3vf<M> v0 = zero, v1 = zero, v2 = zero;
      
      for (size_t i=0; i<M && begin<end; i++, begin++)
      {
	const PrimRef& prim = prims[begin];
        const unsigned geomID = prim.geomID();
        const unsigned primID = prim.primID();
        const TriangleMesh* __restrict__ const mesh = scene->get<TriangleMesh>(geomID);
        const TriangleMesh::Triangle& tri = mesh->triangle(primID);
        const Vec3fa& p0 = mesh->vertex(tri.v[0]);
        const Vec3fa& p1 = mesh->vertex(tri.v[1]);
        const Vec3fa& p2 = mesh->vertex(tri.v[2]);
        vgeomID [i] = geomID;
        vprimID [i] = primID;
        v0.x[i] = p0.x; v0.y[i] = p0.y; v0.z[i] = p0.z;
        v1.x[i] = p1.x; v1.y[i] = p1.y; v1.z[i] = p1.z;
        v2.x[i] = p2.x; v2.y[i] = p2.y; v2.z[i] = p2.z;
      }
      TriangleMv::store_nt(this,TriangleMv(v0,v1,v2,vgeomID,vprimID,last));
    }

    /* Updates the primitive */
    __forceinline BBox3fa update(TriangleMesh* mesh)
    {
      BBox3fa bounds = empty;
      vint<M> vgeomID = -1, vprimID = -1;
      Vec3vf<M> v0 = zero, v1 = zero, v2 = zero;
      
      for (size_t i=0; i<M; i++)
      {
        if (!valid(i)) break;
        const unsigned geomId = geomIDs[i]; // copies last bit
        const unsigned primId = primID(i);
        const TriangleMesh::Triangle& tri = mesh->triangle(primId);
        const Vec3fa p0 = mesh->vertex(tri.v[0]);
        const Vec3fa p1 = mesh->vertex(tri.v[1]);
        const Vec3fa p2 = mesh->vertex(tri.v[2]);
        bounds.extend(merge(BBox3fa(p0),BBox3fa(p1),BBox3fa(p2)));
        vgeomID [i] = geomId;
        vprimID [i] = primId;
        v0.x[i] = p0.x; v0.y[i] = p0.y; v0.z[i] = p0.z;
        v1.x[i] = p1.x; v1.y[i] = p1.y; v1.z[i] = p1.z;
        v2.x[i] = p2.x; v2.y[i] = p2.y; v2.z[i] = p2.z;
      }
      new (this) TriangleMv(v0,v1,v2,vgeomID,vprimID,false);
      return bounds;
    }
   
  public:
    Vec3vf<M> v0;      // 1st vertex of the triangles
    Vec3vf<M> v1;      // 2nd vertex of the triangles
    Vec3vf<M> v2;      // 3rd vertex of the triangles
    vint<M> geomIDs; // geometry ID
    vint<M> primIDs; // primitive ID
  };

  template<int M>
  typename TriangleMv<M>::Type TriangleMv<M>::type;

  typedef TriangleMv<4> Triangle4v;
}
