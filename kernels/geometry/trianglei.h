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
#include "../common/scene.h"

namespace embree
{
  /* Stores M triangles from an indexed face set */
  template <int M>
  struct TriangleMi
  {
    /* Virtual interface to query information about the triangle type */
    struct Type : public PrimitiveType
    {
      Type();
      size_t size(const char* This) const;
      bool last(const char* This) const;
    };
    static Type type;
    static const Leaf::Type leaf_type = Leaf::TY_TRIANGLE;

  public:

    /* primitive supports multiple time segments */
    static const bool singleTimeSegment = false;

    /* Returns maximal number of stored triangles */
    static __forceinline size_t max_size() { return M; }

    /* Returns required number of primitive blocks for N primitives */
    static __forceinline size_t blocks(size_t N) { return (N+max_size()-1)/max_size(); }

  public:

    /* Default constructor */
    __forceinline TriangleMi() {  }

    /* Construction from vertices and IDs */
    __forceinline TriangleMi(const vint<M>& v0,
                             const vint<M>& v1,
                             const vint<M>& v2,
                             const vint<M>& geomIDs,
                             const vint<M>& primIDs,
                             const Leaf::Type ty,
                             const bool last)
      : v0(v0), v1(v1), v2(v2), geomIDs(Leaf::vencode(ty,geomIDs,last)), primIDs(primIDs) {}

    /* Returns a mask that tells which triangles are valid */
    __forceinline vbool<M> valid() const { return primIDs != vint<M>(-1); }

    /* Returns if the specified triangle is valid */
    __forceinline bool valid(const size_t i) const { assert(i<M); return primIDs[i] != -1; }

    /* Returns the number of stored triangles */
    __forceinline size_t size() const { return __bsf(~movemask(valid())); }

    /*! checks if this is the last primitive */
    __forceinline unsigned last() const { return Leaf::decodeLast(geomIDs[0]); }

    /* Returns the geometry IDs */
    __forceinline vint<M> geomID() const { return geomIDs; }
    __forceinline unsigned geomID(const size_t i) const { assert(i<M); return Leaf::decodeID(geomIDs[i]); }

    /* Returns the primitive IDs */
    __forceinline vint<M> primID() const { return primIDs; }
    __forceinline unsigned primID(const size_t i) const { assert(i<M); return primIDs[i]; }

    /* loads a single vertex */
    __forceinline Vec3f& getVertex(const vint<M>& v, const size_t index, const Scene *const scene) const
    {
      const int* vertices = scene->vertices[geomID(index)];
      return (Vec3f&) vertices[v[index]];
    }

    template<typename T>
    __forceinline Vec3<T> getVertex(const vint<M>& v, const size_t index, const Scene *const scene, const size_t itime, const T& ftime) const
    {
      const TriangleMesh* mesh = scene->get<TriangleMesh>(geomID(index));
      const int* vertices0 = (const int*) mesh->vertexPtr(0,itime+0);
      const int* vertices1 = (const int*) mesh->vertexPtr(0,itime+1);
      const Vec3fa v0 = Vec3fa::loadu(vertices0+v[index]);
      const Vec3fa v1 = Vec3fa::loadu(vertices1+v[index]);
      const Vec3<T> p0(v0.x,v0.y,v0.z);
      const Vec3<T> p1(v1.x,v1.y,v1.z);
      return lerp(p0,p1,ftime);
    }

    template<int K, typename T>
    __forceinline Vec3<T> getVertex(const vbool<K>& valid, const vint<M>& v, const size_t index, const Scene *const scene, const vint<K>& itime, const T& ftime) const
    {
      Vec3<T> p0, p1;
      const TriangleMesh* mesh = scene->get<TriangleMesh>(geomID(index));

      for (size_t mask=movemask(valid), i=__bsf(mask); mask; mask=__btc(mask,i), i=__bsf(mask))
      {
        const int* vertices0 = (const int*) mesh->vertexPtr(0,itime[i]+0);
        const int* vertices1 = (const int*) mesh->vertexPtr(0,itime[i]+1);
        const Vec3fa v0 = Vec3fa::loadu(vertices0+v[index]);
        const Vec3fa v1 = Vec3fa::loadu(vertices1+v[index]);
        p0.x[i] = v0.x; p0.y[i] = v0.y; p0.z[i] = v0.z;
        p1.x[i] = v1.x; p1.y[i] = v1.y; p1.z[i] = v1.z;
      }
      return (T(one)-ftime)*p0 + ftime*p1;
    }

    /* Gather the triangles */
    __forceinline void gather(Vec3vf<M>& p0, Vec3vf<M>& p1, Vec3vf<M>& p2, const Scene* const scene) const;

    template<int K>
    __forceinline void gather(const vbool<K>& valid,
                              Vec3vf<K>& p0,
                              Vec3vf<K>& p1,
                              Vec3vf<K>& p2,
                              const size_t index,
                              const Scene* const scene,
                              const vfloat<K>& time) const
    {
      const TriangleMesh* mesh = scene->get<TriangleMesh>(geomID(index));

      vfloat<K> ftime;
      const vint<K> itime = getTimeSegment(time, vfloat<K>(mesh->fnumTimeSegments), ftime);

      const size_t first = __bsf(movemask(valid));
      if (likely(all(valid,itime[first] == itime)))
      {
        p0 = getVertex(v0, index, scene, itime[first], ftime);
        p1 = getVertex(v1, index, scene, itime[first], ftime);
        p2 = getVertex(v2, index, scene, itime[first], ftime);
      } else {
        p0 = getVertex(valid, v0, index, scene, itime, ftime);
        p1 = getVertex(valid, v1, index, scene, itime, ftime);
        p2 = getVertex(valid, v2, index, scene, itime, ftime);
      }
    }

    __forceinline void gather(Vec3vf<M>& p0,
                              Vec3vf<M>& p1,
                              Vec3vf<M>& p2,
                              const TriangleMesh* mesh0,
                              const TriangleMesh* mesh1,
                              const TriangleMesh* mesh2,
                              const TriangleMesh* mesh3,
                              const vint<M>& itime) const;

    __forceinline void gather(Vec3vf<M>& p0,
                              Vec3vf<M>& p1,
                              Vec3vf<M>& p2,
                              const Scene *const scene,
                              const float time) const;

     /* returns area of quads */
    __forceinline float area(const Scene* scene, const size_t itime=0)
    {
      float A = 0.0f;
      for (size_t i=0; i<M && valid(i); i++)
      {
        const int* vertices = (const int*) scene->get<TriangleMesh>(geomID(i))->vertexPtr(0,itime);
        const Vec3fa p0 = Vec3fa::loadu(vertices+v0[i]);
        const Vec3fa p1 = Vec3fa::loadu(vertices+v1[i]);
        const Vec3fa p2 = Vec3fa::loadu(vertices+v2[i]);
        A += 0.5f*length(cross(p1-p0,p2-p0));
      }
      return A;
    }
    
    /* Calculate the bounds of the triangles */
    __forceinline const BBox3fa bounds(const Scene *const scene, const size_t itime=0) const
    {
      BBox3fa bounds = empty;
      for (size_t i=0; i<M && valid(i); i++)
      {
        const int* vertices = (const int*) scene->get<TriangleMesh>(geomID(i))->vertexPtr(0,itime);
        bounds.extend(Vec3fa::loadu(vertices+v0[i]));
        bounds.extend(Vec3fa::loadu(vertices+v1[i]));
        bounds.extend(Vec3fa::loadu(vertices+v2[i]));
      }
      return bounds;
    }

    /* Calculate the linear bounds of the primitive */
    __forceinline LBBox3fa linearBounds(const Scene *const scene, size_t itime) {
      return LBBox3fa(bounds(scene,itime+0),bounds(scene,itime+1));
    }

    __forceinline LBBox3fa linearBounds(const Scene *const scene, size_t itime, size_t numTimeSteps)
    {
      LBBox3fa allBounds = empty;
      for (size_t i=0; i<M && valid(i); i++)
      {
        const TriangleMesh* mesh = scene->get<TriangleMesh>(geomID(i));
        allBounds.extend(mesh->linearBounds(primID(i), itime, numTimeSteps));
      }
      return allBounds;
    }

    __forceinline LBBox3fa linearBounds(const Scene *const scene, const BBox1f time_range)
    {
      LBBox3fa allBounds = empty;
      for (size_t i=0; i<M && valid(i); i++)
      {
        const TriangleMesh* mesh = scene->get<TriangleMesh>(geomID(i));
        allBounds.extend(mesh->linearBounds(primID(i), time_range));
      }
      return allBounds;
    }

    /* Non-temporal store */
    __forceinline static void store_nt(TriangleMi* dst, const TriangleMi& src)
    {
      vint<M>::store_nt(&dst->v0,src.v0);
      vint<M>::store_nt(&dst->v1,src.v1);
      vint<M>::store_nt(&dst->v2,src.v2);
      vint<M>::store_nt(&dst->geomIDs,src.geomIDs);
      vint<M>::store_nt(&dst->primIDs,src.primIDs);
    }

    /* Fill triangle from triangle list */
    template<typename PrimRefT>
    __forceinline const BBox3fa fill(const PrimRefT* prims, size_t& begin, size_t end, Scene* scene, bool last, const Leaf::Type ty = Leaf::TY_TRIANGLE)
    {
      vint<M> geomID = -1, primID = -1;
      vint<M> v0 = zero, v1 = zero, v2 = zero;
      const PrimRefT* prim = &prims[begin];

      BBox3fa bounds = empty;
      for (size_t i=0; i<M; i++)
      {
        const TriangleMesh* mesh = scene->get<TriangleMesh>(prim->geomID());
        const TriangleMesh::Triangle& tri = mesh->triangle(prim->primID());
        if (begin<end) {
          geomID[i] = prim->geomID();
          primID[i] = prim->primID();
          bounds.extend(mesh->bounds(prim->primID()));
          unsigned int_stride = mesh->vertices0.getStride()/4;
          v0[i] = tri.v[0] * int_stride;
          v1[i] = tri.v[1] * int_stride;
          v2[i] = tri.v[2] * int_stride;
          begin++;
        } else {
          assert(i);
          if (likely(i > 0)) {
            geomID[i] = geomID[0];
            primID[i] = -1;
            v0[i] = v0[0];
            v1[i] = v0[0];
            v2[i] = v0[0];
          }
        }
        if (begin<end) prim = &prims[begin];
      }

      new (this) TriangleMi(v0,v1,v2,geomID,primID,ty,last); // FIXME: use non temporal store
      return bounds;
    }

    __forceinline LBBox3fa fillMB(const PrimRef* prims, size_t& begin, size_t end, Scene* scene, size_t itime, bool last)
    {
      fill(prims, begin, end, scene, last, Leaf::TY_TRIANGLE_MB);
      return linearBounds(scene, itime);
    }

    __forceinline LBBox3fa fillMB(const PrimRefMB* prims, size_t& begin, size_t end, Scene* scene, const BBox1f time_range, bool last)
    {
      fill(prims, begin, end, scene, last, Leaf::TY_TRIANGLE_MB);
      return linearBounds(scene, time_range);
    }

    template<typename BVH>
    __forceinline static typename BVH::NodeRef createLeaf(const FastAllocator::CachedAllocator& alloc, PrimRef* prims, const range<size_t>& range, BVH* bvh)
    {
      size_t cur = range.begin();
      size_t items = blocks(range.size());
      TriangleMi* accel = (TriangleMi*) alloc.malloc1(items*sizeof(TriangleMi),BVH::byteAlignment);
      for (size_t i=0; i<items; i++) {
        accel[i].fill(prims,cur,range.end(),bvh->scene,i==(items-1));
      }
      return BVH::encodeLeaf((char*)accel,Leaf::TY_TRIANGLE);
    }

    template<typename BVH>
    __forceinline static const typename BVH::NodeRecordMB4D createLeafMB (const SetMB& set, const FastAllocator::CachedAllocator& alloc, BVH* bvh)
    {
      size_t items = blocks(set.object_range.size());
      size_t start = set.object_range.begin();
      TriangleMi* accel = (TriangleMi*) alloc.malloc1(items*sizeof(TriangleMi),BVH::byteAlignment);
      typename BVH::NodeRef node = bvh->encodeLeaf((char*)accel,Leaf::TY_TRIANGLE);
      LBBox3fa allBounds = empty;
      for (size_t i=0; i<items; i++) {
        const BBox3fa b = accel[i].fill(set.prims->data(),start,set.object_range.end(),bvh->scene,i==(items-1));
        allBounds.extend(LBBox3fa(b));
      }
      return typename BVH::NodeRecordMB4D(node,allBounds,set.time_range);
    }

    /* Updates the primitive */
    __forceinline BBox3fa update(TriangleMesh* mesh)
    {
      BBox3fa bounds = empty;
      for (size_t i=0; i<M; i++)
      {
        if (!valid(i)) break;
        const unsigned primId = primID(i);
        const TriangleMesh::Triangle& tri = mesh->triangle(primId);
        const Vec3fa p0 = mesh->vertex(tri.v[0]);
        const Vec3fa p1 = mesh->vertex(tri.v[1]);
        const Vec3fa p2 = mesh->vertex(tri.v[2]);
        bounds.extend(merge(BBox3fa(p0),BBox3fa(p1),BBox3fa(p2)));
      }
      return bounds;
    }

  public:
    vint<M> v0;         // 4 byte offset of 1st vertex
    vint<M> v1;         // 4 byte offset of 2nd vertex
    vint<M> v2;         // 4 byte offset of 3rd vertex
    vint<M> geomIDs;    // geometry ID of mesh
    vint<M> primIDs;    // primitive ID of primitive inside mesh
  };

  template <int M>
    struct TriangleMiMB : public TriangleMi<M>
  {
    static const Leaf::Type leaf_type = Leaf::TY_TRIANGLE_MB;
    
    template<typename BVH>
    __forceinline static typename BVH::NodeRef createLeaf(const FastAllocator::CachedAllocator& alloc, PrimRef* prims, const range<size_t>& range, BVH* bvh)
    {
      assert(false);
      return BVH::emptyNode;
    }

    template<typename BVH>
      __forceinline static const typename BVH::NodeRecordMB4D createLeafMB (const SetMB& set, const FastAllocator::CachedAllocator& alloc, BVH* bvh)
    {
      size_t items = TriangleMi<M>::blocks(set.object_range.size());
      size_t start = set.object_range.begin();
      TriangleMi<M>* accel = (TriangleMi<M>*) alloc.malloc1(items*sizeof(TriangleMi<M>),BVH::byteAlignment);
      typename BVH::NodeRef node = bvh->encodeLeaf((char*)accel,Leaf::TY_TRIANGLE_MB);
      float A = 0.0f;
      LBBox3fa allBounds = empty;
      for (size_t i=0; i<items; i++) {
        allBounds.extend(accel[i].fillMB(set.prims->data(), start, set.object_range.end(), bvh->scene, set.time_range, i==(items-1)));
        A += accel[i].area(bvh->scene);
      }
      return typename BVH::NodeRecordMB4D(node,allBounds,set.time_range,A,2.0f*items);
    }
  };

  template<>
  __forceinline void TriangleMi<4>::gather(Vec3vf4& p0,
                                           Vec3vf4& p1,
                                           Vec3vf4& p2,
                                           const Scene* const scene) const
  {
    const int* vertices0 = scene->vertices[Leaf::decodeID(geomIDs[0])];
    const int* vertices1 = scene->vertices[geomIDs[1]];
    const int* vertices2 = scene->vertices[geomIDs[2]];
    const int* vertices3 = scene->vertices[geomIDs[3]];
    const vfloat4 a0 = vfloat4::loadu(vertices0 + v0[0]);
    const vfloat4 a1 = vfloat4::loadu(vertices1 + v0[1]);
    const vfloat4 a2 = vfloat4::loadu(vertices2 + v0[2]);
    const vfloat4 a3 = vfloat4::loadu(vertices3 + v0[3]);
    const vfloat4 b0 = vfloat4::loadu(vertices0 + v1[0]);
    const vfloat4 b1 = vfloat4::loadu(vertices1 + v1[1]);
    const vfloat4 b2 = vfloat4::loadu(vertices2 + v1[2]);
    const vfloat4 b3 = vfloat4::loadu(vertices3 + v1[3]);
    const vfloat4 c0 = vfloat4::loadu(vertices0 + v2[0]);
    const vfloat4 c1 = vfloat4::loadu(vertices1 + v2[1]);
    const vfloat4 c2 = vfloat4::loadu(vertices2 + v2[2]);
    const vfloat4 c3 = vfloat4::loadu(vertices3 + v2[3]);
    transpose(a0,a1,a2,a3,p0.x,p0.y,p0.z);
    transpose(b0,b1,b2,b3,p1.x,p1.y,p1.z);
    transpose(c0,c1,c2,c3,p2.x,p2.y,p2.z);
  }

  template<>
  __forceinline void TriangleMi<4>::gather(Vec3vf4& p0,
                                           Vec3vf4& p1,
                                           Vec3vf4& p2,
                                           const TriangleMesh* mesh0,
                                           const TriangleMesh* mesh1,
                                           const TriangleMesh* mesh2,
                                           const TriangleMesh* mesh3,
                                           const vint4& itime) const
  {
    const int* vertices0 = (const int*) mesh0->vertexPtr(0,itime[0]);
    const int* vertices1 = (const int*) mesh1->vertexPtr(0,itime[1]);
    const int* vertices2 = (const int*) mesh2->vertexPtr(0,itime[2]);
    const int* vertices3 = (const int*) mesh3->vertexPtr(0,itime[3]);
    const vfloat4 a0 = vfloat4::loadu(vertices0 + v0[0]);
    const vfloat4 a1 = vfloat4::loadu(vertices1 + v0[1]);
    const vfloat4 a2 = vfloat4::loadu(vertices2 + v0[2]);
    const vfloat4 a3 = vfloat4::loadu(vertices3 + v0[3]);
    const vfloat4 b0 = vfloat4::loadu(vertices0 + v1[0]);
    const vfloat4 b1 = vfloat4::loadu(vertices1 + v1[1]);
    const vfloat4 b2 = vfloat4::loadu(vertices2 + v1[2]);
    const vfloat4 b3 = vfloat4::loadu(vertices3 + v1[3]);
    const vfloat4 c0 = vfloat4::loadu(vertices0 + v2[0]);
    const vfloat4 c1 = vfloat4::loadu(vertices1 + v2[1]);
    const vfloat4 c2 = vfloat4::loadu(vertices2 + v2[2]);
    const vfloat4 c3 = vfloat4::loadu(vertices3 + v2[3]);
    transpose(a0,a1,a2,a3,p0.x,p0.y,p0.z);
    transpose(b0,b1,b2,b3,p1.x,p1.y,p1.z);
    transpose(c0,c1,c2,c3,p2.x,p2.y,p2.z);
  }

  template<>
  __forceinline void TriangleMi<4>::gather(Vec3vf4& p0,
                                           Vec3vf4& p1,
                                           Vec3vf4& p2,
                                           const Scene *const scene,
                                           const float time) const
  {
    const TriangleMesh* mesh0 = scene->get<TriangleMesh>(Leaf::decodeID(geomIDs[0]));
    const TriangleMesh* mesh1 = scene->get<TriangleMesh>(geomIDs[1]);
    const TriangleMesh* mesh2 = scene->get<TriangleMesh>(geomIDs[2]);
    const TriangleMesh* mesh3 = scene->get<TriangleMesh>(geomIDs[3]);

    const vfloat4 numTimeSegments(mesh0->fnumTimeSegments, mesh1->fnumTimeSegments, mesh2->fnumTimeSegments, mesh3->fnumTimeSegments);
    vfloat4 ftime;
    const vint4 itime = getTimeSegment(vfloat4(time), numTimeSegments, ftime);

    Vec3vf4 a0,a1,a2; gather(a0,a1,a2,mesh0,mesh1,mesh2,mesh3,itime);
    Vec3vf4 b0,b1,b2; gather(b0,b1,b2,mesh0,mesh1,mesh2,mesh3,itime+1);
    p0 = lerp(a0,b0,ftime);
    p1 = lerp(a1,b1,ftime);
    p2 = lerp(a2,b2,ftime);
  }

  template<int M>
  typename TriangleMi<M>::Type TriangleMi<M>::type;

  typedef TriangleMi<4> Triangle4i;
  typedef TriangleMiMB<4> Triangle4iMB;
}
