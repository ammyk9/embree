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

#include "scene_line_segments.h"
#include "scene.h"

namespace embree
{
#if defined(EMBREE_LOWEST_ISA)

  LineSegments::LineSegments (Scene* scene, RTCGeometryFlags flags, size_t numPrimitives, size_t numVertices, size_t numTimeSteps)
    : Geometry(scene,LINE_SEGMENTS,numPrimitives,numTimeSteps,flags)
  {
    segments.init(scene->device,numPrimitives,sizeof(int));
    vertices.resize(numTimeSteps);
    for (size_t i=0; i<numTimeSteps; i++) {
      vertices[i].init(scene->device,numVertices,sizeof(Vec3fa));
    }
    enabling();
  }

  void LineSegments::enabling()
  {
    if (numTimeSteps == 1) scene->world.numLineSegments += numPrimitives;
    else                   scene->worldMB.numLineSegments += numPrimitives;
  }

  void LineSegments::disabling()
  {
    if (numTimeSteps == 1) scene->world.numLineSegments -= numPrimitives;
    else                   scene->worldMB.numLineSegments -= numPrimitives;
  }

  void LineSegments::setMask (unsigned mask)
  {
    if (scene->isStatic() && scene->isBuild())
      throw_RTCError(RTC_INVALID_OPERATION,"static geometries cannot get modified");

    this->mask = mask;
    Geometry::update();
  }

  void LineSegments::setBuffer(RTCBufferType type, void* ptr, size_t offset, size_t stride, size_t size)
  {
    if (scene->isStatic() && scene->isBuild())
      throw_RTCError(RTC_INVALID_OPERATION,"static geometries cannot get modified");

    /* verify that all accesses are 4 bytes aligned */
    if (((size_t(ptr) + offset) & 0x3) || (stride & 0x3))
      throw_RTCError(RTC_INVALID_OPERATION,"data must be 4 bytes aligned");

    unsigned bid = type & 0xFFFF;
    if (type >= RTC_VERTEX_BUFFER0 && type < RTCBufferType(RTC_VERTEX_BUFFER0 + numTimeSteps)) 
    {
      size_t t = type - RTC_VERTEX_BUFFER0;
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
      segments.set(ptr,offset,stride,size); 
      setNumPrimitives(size);
      if (size != (size_t)-1) enabling();
    }
    else
      throw_RTCError(RTC_INVALID_ARGUMENT,"unknown buffer type");
  }

  void* LineSegments::map(RTCBufferType type)
  {
    if (scene->isStatic() && scene->isBuild()) {
      throw_RTCError(RTC_INVALID_OPERATION,"static geometries cannot get modified");
      return nullptr;
    }

    if (type == RTC_INDEX_BUFFER) {
      return segments.map(scene->numMappedBuffers);
    }
    else if (type >= RTC_VERTEX_BUFFER0 && type < RTCBufferType(RTC_VERTEX_BUFFER0 + numTimeSteps)) {
      return vertices[type - RTC_VERTEX_BUFFER0].map(scene->numMappedBuffers);
    }
    else {
      throw_RTCError(RTC_INVALID_ARGUMENT,"unknown buffer type"); 
      return nullptr;
    }
  }

  void LineSegments::unmap(RTCBufferType type)
  {
    if (scene->isStatic() && scene->isBuild())
      throw_RTCError(RTC_INVALID_OPERATION,"static geometries cannot get modified");

    if (type == RTC_INDEX_BUFFER) {
      segments.unmap(scene->numMappedBuffers);
    }
    else if (type >= RTC_VERTEX_BUFFER0 && type < RTCBufferType(RTC_VERTEX_BUFFER0 + numTimeSteps)) {
      vertices[type - RTC_VERTEX_BUFFER0].unmap(scene->numMappedBuffers);
      vertices0 = vertices[0];
    }
    else {
      throw_RTCError(RTC_INVALID_ARGUMENT,"unknown buffer type"); 
    }
  }

  void LineSegments::immutable ()
  {
    const bool freeIndices  = !scene->needLineIndices;
    const bool freeVertices = !scene->needLineVertices;
    if (freeIndices) segments.free();
    if (freeVertices )
      for (auto& buffer : vertices)
        buffer.free();
  }

  bool LineSegments::verify ()
  { 
    /*! verify consistent size of vertex arrays */
    if (vertices.size() == 0) return false;
    for (const auto& buffer : vertices)
      if (buffer.size() != numVertices())
        return false;

    /*! verify segment indices */
    for (size_t i=0; i<numPrimitives; i++) {
      if (segments[i]+1 >= numVertices()) return false;
    }

    /*! verify vertices */
    for (const auto& buffer : vertices) {
      for (size_t i=0; i<buffer.size(); i++) {
	if (!isvalid(buffer[i].x)) return false;
        if (!isvalid(buffer[i].y)) return false;
        if (!isvalid(buffer[i].z)) return false;
        if (!isvalid(buffer[i].w)) return false;
      }
    }
    return true;
  }

  void LineSegments::interpolate(unsigned primID, float u, float v, RTCBufferType buffer, float* P, float* dPdu, float* dPdv, float* ddPdudu, float* ddPdvdv, float* ddPdudv, size_t numFloats)
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
      const size_t ofs = i*sizeof(float);
      const size_t segment = segments[primID];
      const vboolx valid = vintx((int)i)+vintx(step) < vintx(numFloats);
      const vfloatx p0 = vfloatx::loadu(valid,(float*)&src[(segment+0)*stride+ofs]);
      const vfloatx p1 = vfloatx::loadu(valid,(float*)&src[(segment+1)*stride+ofs]);
      if (P      ) vfloatx::storeu(valid,P+i,lerp(p0,p1,u));
      if (dPdu   ) vfloatx::storeu(valid,dPdu+i,p1-p0);
      if (ddPdudu) vfloatx::storeu(valid,dPdu+i,vfloatx(zero));
    }
  }
#endif

  namespace isa
  {
    LineSegments* createLineSegments(Scene* scene, RTCGeometryFlags flags, size_t numSegments, size_t numVertices, size_t numTimeSteps) {
      return new LineSegmentsISA(scene,flags,numSegments,numVertices,numTimeSteps);
    }

    LBBox3fa LineSegmentsISA::virtualLinearBounds(size_t primID, const BBox1f& time_range) const 
    {
      if (numTimeSteps == 1)
        return LBBox3fa(bounds(primID));
      else
        return linearBounds(primID,time_range);
    }

    PrimInfo LineSegmentsISA::createPrimRefArray(mvector<PrimRef>& prims, const range<size_t>& src, size_t dst)
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

    PrimInfoMB LineSegmentsISA::createPrimRefArrayMB(mvector<PrimRefMB>& prims, const BBox1f t0t1, const range<size_t>& src, size_t dst) 
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
