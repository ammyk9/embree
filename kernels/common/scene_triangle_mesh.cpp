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

#include "scene_triangle_mesh.h"
#include "scene.h"

namespace embree
{
#if defined(EMBREE_LOWEST_ISA)

  TriangleMesh::TriangleMesh (Scene* scene, RTCGeometryFlags flags, size_t numTriangles, size_t numVertices, size_t numTimeSteps)
    : Geometry(scene,TRIANGLE_MESH,numTriangles,numTimeSteps,flags)
  {
    triangles.init(scene->device,numTriangles,sizeof(Triangle));
    vertices.resize(numTimeSteps);
    for (size_t i=0; i<numTimeSteps; i++) {
      vertices[i].init(scene->device,numVertices,sizeof(Vec3fa));
    }
    enabling();
  }

  void TriangleMesh::enabling() 
  { 
    if (numTimeSteps == 1) scene->world.numTriangles += triangles.size();
    else                   scene->worldMB.numTriangles += triangles.size();
  }
  
  void TriangleMesh::disabling() 
  { 
    if (numTimeSteps == 1) scene->world.numTriangles -= triangles.size();
    else                   scene->worldMB.numTriangles -= triangles.size();
  }

  void TriangleMesh::setMask (unsigned mask) 
  {
    if (scene->isStatic() && scene->isBuild())
      throw_RTCError(RTC_INVALID_OPERATION,"static scenes cannot get modified");

    this->mask = mask; 
    Geometry::update();
  }

  void TriangleMesh::setBuffer(RTCBufferType type, void* ptr, size_t offset, size_t stride, size_t size) 
  { 
    if (scene->isStatic() && scene->isBuild()) 
      throw_RTCError(RTC_INVALID_OPERATION,"static scenes cannot get modified");

    /* verify that all accesses are 4 bytes aligned */
    if (((size_t(ptr) + offset) & 0x3) || (stride & 0x3)) 
      throw_RTCError(RTC_INVALID_OPERATION,"data must be 4 bytes aligned");

    unsigned bid = type & 0xFFFF;
    if (type >= RTC_VERTEX_BUFFER0 && type < RTCBufferType(RTC_VERTEX_BUFFER0 + numTimeSteps)) 
    {
       size_t t = type - RTC_VERTEX_BUFFER0;
       if (size == -1) size = vertices[t].size();

       /* if buffer is larger than 16GB the premultiplied index optimization does not work */
      if (stride*size > 16ll*1024ll*1024ll*1024ll)
       throw_RTCError(RTC_INVALID_OPERATION,"vertex buffer can be at most 16GB large");

      vertices[t].set(ptr,offset,stride,size);
      vertices[t].checkPadding16();
      vertices0 = vertices[0];
    } 
    else if (type >= RTC_USER_VERTEX_BUFFER0 && type < RTC_USER_VERTEX_BUFFER0+RTC_MAX_USER_VERTEX_BUFFERS)
    {
      if (bid >= userbuffers.size()) userbuffers.resize(bid+1);
      userbuffers[bid] = APIBuffer<char>(scene->device,numVertices(),stride);
      userbuffers[bid].set(ptr,offset,stride,size);
      userbuffers[bid].checkPadding16();
    }
    else if (type == RTC_INDEX_BUFFER) 
    {
      if (size != (size_t)-1) disabling();
      triangles.set(ptr,offset,stride,size); 
      setNumPrimitives(size);
      if (size != (size_t)-1) enabling();
    }
    else 
      throw_RTCError(RTC_INVALID_ARGUMENT,"unknown buffer type");
  }

  void* TriangleMesh::map(RTCBufferType type) 
  {
    if (scene->isStatic() && scene->isBuild())
      throw_RTCError(RTC_INVALID_OPERATION,"static scenes cannot get modified");
    
    if (type == RTC_INDEX_BUFFER) {
      return triangles.map(scene->numMappedBuffers);
    }
    else if (type >= RTC_VERTEX_BUFFER0 && type < RTCBufferType(RTC_VERTEX_BUFFER0 + numTimeSteps)) {
      return vertices[type - RTC_VERTEX_BUFFER0].map(scene->numMappedBuffers);
    }
    else {
      throw_RTCError(RTC_INVALID_ARGUMENT,"unknown buffer type"); 
      return nullptr;
    }
  }

  void TriangleMesh::unmap(RTCBufferType type) 
  {
    if (scene->isStatic() && scene->isBuild())
      throw_RTCError(RTC_INVALID_OPERATION,"static scenes cannot get modified");

    if (type == RTC_INDEX_BUFFER) {
      triangles.unmap(scene->numMappedBuffers);
    }
    else if (type >= RTC_VERTEX_BUFFER0 && type < RTCBufferType(RTC_VERTEX_BUFFER0 + numTimeSteps)) {
      vertices[type - RTC_VERTEX_BUFFER0].unmap(scene->numMappedBuffers);
      vertices0 = vertices[0];
    }
    else {
      throw_RTCError(RTC_INVALID_ARGUMENT,"unknown buffer type"); 
    }
  }

  void TriangleMesh::preCommit () 
  {
    /* verify that stride of all time steps are identical */
    for (size_t t=0; t<numTimeSteps; t++)
      if (vertices[t].getStride() != vertices[0].getStride())
        throw_RTCError(RTC_INVALID_OPERATION,"stride of vertex buffers have to be identical for each time step");
  }

  void TriangleMesh::postCommit () 
  {
    scene->vertices[geomID] = (int*) vertices0.getPtr();
    Geometry::postCommit();
  }

  void TriangleMesh::immutable () 
  {
    const bool freeTriangles = !scene->needTriangleIndices;
    const bool freeVertices  = !scene->needTriangleVertices;
    if (freeTriangles) triangles.free(); 
    if (freeVertices )
      for (auto& buffer : vertices)
        buffer.free();
  }

  bool TriangleMesh::verify () 
  {
    /*! verify size of vertex arrays */
    if (vertices.size() == 0) return false;
    for (const auto& buffer : vertices)
      if (buffer.size() != numVertices())
        return false;

    /*! verify size of user vertex arrays */
    for (const auto& buffer : userbuffers)
      if (buffer.size() != numVertices())
        return false;

    /*! verify triangle indices */
    for (size_t i=0; i<triangles.size(); i++) {     
      if (triangles[i].v[0] >= numVertices()) return false; 
      if (triangles[i].v[1] >= numVertices()) return false; 
      if (triangles[i].v[2] >= numVertices()) return false; 
    }

    /*! verify vertices */
    for (const auto& buffer : vertices)
      for (size_t i=0; i<buffer.size(); i++)
	if (!isvalid(buffer[i])) 
	  return false;

    return true;
  }
  
  void TriangleMesh::interpolate(unsigned primID, float u, float v, RTCBufferType buffer, float* P, float* dPdu, float* dPdv, float* ddPdudu, float* ddPdvdv, float* ddPdudv, size_t numFloats) 
  {
    /* test if interpolation is enabled */
#if defined(DEBUG)
    if ((scene->aflags & RTC_INTERPOLATE) == 0) 
      throw_RTCError(RTC_INVALID_OPERATION,"rtcInterpolate can only get called when RTC_INTERPOLATE is enabled for the scene");
#endif
    
    /* calculate base pointer and stride */
    assert((buffer >= RTC_VERTEX_BUFFER0 && buffer < RTCBufferType(RTC_VERTEX_BUFFER0 + numTimeSteps)) ||
           (buffer >= RTC_USER_VERTEX_BUFFER0 && buffer <= RTC_USER_VERTEX_BUFFER1));
    const char* src = nullptr; 
    size_t stride = 0;
    if (buffer >= RTC_USER_VERTEX_BUFFER0) {
      src    = userbuffers[buffer&0xFFFF].getPtr();
      stride = userbuffers[buffer&0xFFFF].getStride();
    } else {
      src    = vertices[buffer&0xFFFF].getPtr();
      stride = vertices[buffer&0xFFFF].getStride();
    }
    
    for (size_t i=0; i<numFloats; i+=VSIZEX)
    {
      size_t ofs = i*sizeof(float);
      const float w = 1.0f-u-v;
      const Triangle& tri = triangle(primID);
      const vboolx valid = vintx((int)i)+vintx(step) < vintx(numFloats);
      const vfloatx p0 = vfloatx::loadu(valid,(float*)&src[tri.v[0]*stride+ofs]);
      const vfloatx p1 = vfloatx::loadu(valid,(float*)&src[tri.v[1]*stride+ofs]);
      const vfloatx p2 = vfloatx::loadu(valid,(float*)&src[tri.v[2]*stride+ofs]);
      
      if (P) {
        vfloatx::storeu(valid,P+i,madd(w,p0,madd(u,p1,v*p2)));
      }
      if (dPdu) {
        assert(dPdu); vfloatx::storeu(valid,dPdu+i,p1-p0);
        assert(dPdv); vfloatx::storeu(valid,dPdv+i,p2-p0);
      }
      if (ddPdudu) {
        assert(ddPdudu); vfloatx::storeu(valid,ddPdudu+i,vfloatx(zero));
        assert(ddPdvdv); vfloatx::storeu(valid,ddPdvdv+i,vfloatx(zero));
        assert(ddPdudv); vfloatx::storeu(valid,ddPdudv+i,vfloatx(zero));
      }
    }
  }
  
#endif
  
  namespace isa
  {
    TriangleMesh* createTriangleMesh(Scene* scene, RTCGeometryFlags flags, size_t numTriangles, size_t numVertices, size_t numTimeSteps) {
      return new TriangleMeshISA(scene,flags,numTriangles,numVertices,numTimeSteps);
    }

    LBBox3fa TriangleMeshISA::virtualLinearBounds(size_t primID, const BBox1f& time_range) const 
    {
      if (numTimeSteps == 1)
        return LBBox3fa(bounds(primID));
      else
        return linearBounds(primID,time_range);
    }
  
    PrimInfo TriangleMeshISA::createPrimRefArray(mvector<PrimRef>& prims, const range<size_t>& src, size_t dst)
    {
      PrimInfo pinfo(empty);
      Leaf::Type ty = leafType();
      for (size_t j=src.begin(); j<src.end(); j++)
      {
        BBox3fa bounds = empty;
        if (!buildBounds(j,&bounds)) continue;
        const PrimRef prim(bounds,ty,geomID,unsigned(j));
        pinfo.add_center2(prim);
        prims[dst++] = prim;
      }
      return pinfo;
    }

    PrimInfoMB TriangleMeshISA::createPrimRefArrayMB(mvector<PrimRefMB>& prims, const BBox1f t0t1, const range<size_t>& src, size_t dst) 
    {
      PrimInfoMB pinfo(empty);
      Leaf::Type ty = leafType();
      for (size_t j=src.begin(); j<src.end(); j++)
      {
        LBBox3fa bounds = empty;
        if (!linearBoundsSafe(j,t0t1,bounds)) continue;
        const PrimRefMB prim(bounds,numTimeSegments(),numTimeSegments(),ty,geomID,unsigned(j));
        pinfo.add_primref(prim);
        prims[dst++] = prim;
      }
      return pinfo;
    }
  }
}
