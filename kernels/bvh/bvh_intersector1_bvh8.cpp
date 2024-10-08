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

#include "bvh_intersector1.cpp"

namespace embree
{
  namespace isa
  {
    ////////////////////////////////////////////////////////////////////////////////
    /// BVH8Intersector1 Definitions
    ////////////////////////////////////////////////////////////////////////////////

    IF_ENABLED_TRIS(DEFINE_INTERSECTOR1(BVH8Triangle4Intersector1Moeller,  BVHNIntersector1<8 COMMA BVH_AN1 COMMA false COMMA ArrayIntersector1<TriangleMIntersector1Moeller  <SIMD_MODE(4) COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR1(BVH8Triangle4iIntersector1Moeller, BVHNIntersector1<8 COMMA BVH_AN1 COMMA false COMMA ArrayIntersector1<TriangleMiIntersector1Moeller <SIMD_MODE(4) COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR1(BVH8Triangle4vIntersector1Pluecker,BVHNIntersector1<8 COMMA BVH_AN1 COMMA true  COMMA ArrayIntersector1<TriangleMvIntersector1Pluecker<SIMD_MODE(4) COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR1(BVH8Triangle4iIntersector1Pluecker,BVHNIntersector1<8 COMMA BVH_AN1 COMMA true  COMMA ArrayIntersector1<TriangleMiIntersector1Pluecker<SIMD_MODE(4) COMMA true> > >));

    IF_ENABLED_TRIS(DEFINE_INTERSECTOR1(BVH8Triangle4vMBIntersector1Moeller, BVHNIntersector1<8 COMMA BVH_AN2_AN4D COMMA false COMMA ArrayIntersector1<TriangleMvMBIntersector1Moeller <SIMD_MODE(4) COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR1(BVH8Triangle4iMBIntersector1Moeller, BVHNIntersector1<8 COMMA BVH_AN2_AN4D COMMA false COMMA ArrayIntersector1<TriangleMiMBIntersector1Moeller <SIMD_MODE(4) COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR1(BVH8Triangle4vMBIntersector1Pluecker,BVHNIntersector1<8 COMMA BVH_AN2_AN4D COMMA true  COMMA ArrayIntersector1<TriangleMvMBIntersector1Pluecker<SIMD_MODE(4) COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR1(BVH8Triangle4iMBIntersector1Pluecker,BVHNIntersector1<8 COMMA BVH_AN2_AN4D COMMA true  COMMA ArrayIntersector1<TriangleMiMBIntersector1Pluecker<SIMD_MODE(4) COMMA true> > >));

    IF_ENABLED_QUADS(DEFINE_INTERSECTOR1(BVH8Quad4vIntersector1Moeller, BVHNIntersector1<8 COMMA BVH_AN1 COMMA false COMMA ArrayIntersector1<QuadMvIntersector1Moeller <4 COMMA true> > >));
    IF_ENABLED_QUADS(DEFINE_INTERSECTOR1(BVH8Quad4iIntersector1Moeller, BVHNIntersector1<8 COMMA BVH_AN1 COMMA false COMMA ArrayIntersector1<QuadMiIntersector1Moeller <4 COMMA true> > >));
    IF_ENABLED_QUADS(DEFINE_INTERSECTOR1(BVH8Quad4vIntersector1Pluecker,BVHNIntersector1<8 COMMA BVH_AN1 COMMA true  COMMA ArrayIntersector1<QuadMvIntersector1Pluecker<4 COMMA true> > >));
    IF_ENABLED_QUADS(DEFINE_INTERSECTOR1(BVH8Quad4iIntersector1Pluecker,BVHNIntersector1<8 COMMA BVH_AN1 COMMA true  COMMA ArrayIntersector1<QuadMiIntersector1Pluecker<4 COMMA true> > >));

    IF_ENABLED_QUADS(DEFINE_INTERSECTOR1(BVH8Quad4iMBIntersector1Moeller, BVHNIntersector1<8 COMMA BVH_AN2_AN4D COMMA false COMMA ArrayIntersector1<QuadMiMBIntersector1Moeller <4 COMMA true> > >));
    IF_ENABLED_QUADS(DEFINE_INTERSECTOR1(BVH8Quad4iMBIntersector1Pluecker,BVHNIntersector1<8 COMMA BVH_AN2_AN4D COMMA true  COMMA ArrayIntersector1<QuadMiMBIntersector1Pluecker<4 COMMA true> > >));

    IF_ENABLED_TRIS(DEFINE_INTERSECTOR1(QBVH8Triangle4iIntersector1Pluecker,BVHNIntersector1<8 COMMA BVH_QN1 COMMA false COMMA ArrayIntersector1<TriangleMiIntersector1Pluecker<SIMD_MODE(4) COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR1(QBVH8Triangle4Intersector1Moeller,BVHNIntersector1<8 COMMA BVH_QN1 COMMA false COMMA ArrayIntersector1<TriangleMIntersector1Moeller  <SIMD_MODE(4) COMMA true> > >));

    IF_ENABLED_QUADS(DEFINE_INTERSECTOR1(QBVH8Quad4iIntersector1Pluecker,BVHNIntersector1<8 COMMA BVH_QN1 COMMA false COMMA ArrayIntersector1<QuadMiIntersector1Pluecker<4 COMMA true> > >));

    IF_ENABLED_HAIR(DEFINE_INTERSECTOR1(BVH8Bezier1vIntersector1_OBB,BVHNIntersector1<8 COMMA BVH_AN1_UN1 COMMA false COMMA ArrayIntersector1<Bezier1vIntersector1> >));
    IF_ENABLED_HAIR(DEFINE_INTERSECTOR1(BVH8Bezier1iIntersector1_OBB,BVHNIntersector1<8 COMMA BVH_AN1_UN1 COMMA false COMMA ArrayIntersector1<Bezier1iIntersector1> >));
    IF_ENABLED_HAIR(DEFINE_INTERSECTOR1(BVH8OBBBezier1iMBIntersector1_OBB,BVHNIntersector1<8 COMMA BVH_AN2_AN4D_UN2 COMMA false COMMA ArrayIntersector1<Bezier1iIntersector1MB> >));

    IF_ENABLED_LINES(DEFINE_INTERSECTOR1(BVH8Line4iIntersector1,BVHNIntersector1<8 COMMA BVH_AN1 COMMA false COMMA ArrayIntersector1<LineMiIntersector1<SIMD_MODE(4) COMMA true> > >));
    IF_ENABLED_LINES(DEFINE_INTERSECTOR1(BVH8Line4iMBIntersector1,BVHNIntersector1<8 COMMA BVH_AN2_AN4D COMMA false COMMA ArrayIntersector1<LineMiMBIntersector1<SIMD_MODE(4) COMMA true> > >));

    IF_ENABLED_USER(DEFINE_INTERSECTOR1(BVH8VirtualIntersector1,BVHNIntersector1<8 COMMA BVH_AN1 COMMA false COMMA ArrayIntersector1<ObjectIntersector1<false>> >));
    IF_ENABLED_USER(DEFINE_INTERSECTOR1(BVH8VirtualMBIntersector1,BVHNIntersector1<8 COMMA BVH_AN2_AN4D COMMA false COMMA ArrayIntersector1<ObjectIntersector1<true>> >));
    
    typedef ArrayIntersector1<TriangleMIntersector1Moeller  <SIMD_MODE(4) COMMA true> > ArrayTriangleMIntersector1Moeller;
    typedef ArrayIntersector1<TriangleMiMBIntersector1Moeller <SIMD_MODE(4) COMMA true> > ArrayTriangleMiMBIntersector1Moeller;
    typedef ArrayIntersector1<QuadMvIntersector1Moeller <4 COMMA true> > ArrayQuadMvIntersector1Moeller;
    typedef ArrayIntersector1<QuadMiMBIntersector1Moeller <4 COMMA true> > ArrayQuadMiMBIntersector1Moeller;
    typedef ArrayIntersector1<Bezier1vIntersector1> ArrayBezier1vIntersector1;
    typedef ArrayIntersector1<Bezier1iIntersector1MB> ArrayBezier1iIntersector1MB;
    typedef ArrayIntersector1<LineMiIntersector1<SIMD_MODE(4) COMMA true> > ArrayLineMiIntersector1;
    typedef ArrayIntersector1<LineMiMBIntersector1<SIMD_MODE(4) COMMA true> > ArrayLineMiMBIntersector1;

#define LEAF_INTERSECTORS_FAST                                          \
      ArrayTriangleMIntersector1Moeller,                                \
      ArrayTriangleMiMBIntersector1Moeller,                             \
      ArrayQuadMvIntersector1Moeller,                                   \
      ArrayQuadMiMBIntersector1Moeller,                                 \
      ArrayBezier1vIntersector1,                                        \
      ArrayBezier1iIntersector1MB,                                      \
      ArrayLineMiIntersector1,                                          \
      ArrayLineMiMBIntersector1
    
    DEFINE_INTERSECTOR1(BVH8Set0MultiFastIntersector1     ,BVHNIntersector1<8 COMMA BVH_AN1                  COMMA false COMMA Virtual8LeafIntersector1<LEAF_INTERSECTORS_FAST> >);
    DEFINE_INTERSECTOR1(BVH8Set0MultiFastMBIntersector1   ,BVHNIntersector1<8 COMMA BVH_AN1_AN2_AN4D         COMMA false COMMA Virtual8LeafIntersector1<LEAF_INTERSECTORS_FAST> >);
    DEFINE_INTERSECTOR1(BVH8Set1MultiFastOBBIntersector1  ,BVHNIntersector1<8 COMMA BVH_AN1_UN1              COMMA false COMMA Virtual8LeafIntersector1<LEAF_INTERSECTORS_FAST> >);
    DEFINE_INTERSECTOR1(BVH8Set1MultiFastOBBMBIntersector1,BVHNIntersector1<8 COMMA BVH_AN1_AN2_AN4D_UN1_UN2 COMMA false COMMA Virtual8LeafIntersector1<LEAF_INTERSECTORS_FAST> >);
    //DEFINE_INTERSECTOR1(BVH8Set1MultiFastOBBMBIntersector1,BVHNIntersector1<8 COMMA BVH_AN2_AN4D_UN2 COMMA false COMMA Virtual8LeafIntersector1<LEAF_INTERSECTORS_FAST> >);
  }
}
