// Copyright 2009-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#if defined (__AVX__)

#include "bvh_intersector_hybrid.cpp"

namespace embree
{
  namespace isa
  {
    ////////////////////////////////////////////////////////////////////////////////
    /// BVH8Intersector4 Definitions
    ////////////////////////////////////////////////////////////////////////////////

    IF_ENABLED_TRIS(DEFINE_INTERSECTOR4(BVH8Triangle4Intersector4HybridMoeller,         BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA false COMMA ArrayIntersectorK_1<4 COMMA TriangleMIntersectorKMoeller  <4 COMMA 4 COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR4(BVH8Triangle4Intersector4HybridMoellerNoFilter, BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA false COMMA ArrayIntersectorK_1<4 COMMA TriangleMIntersectorKMoeller  <4 COMMA 4 COMMA false> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR4(BVH8Triangle4iIntersector4HybridMoeller,        BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA false COMMA ArrayIntersectorK_1<4 COMMA TriangleMiIntersectorKMoeller <4 COMMA 4 COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR4(BVH8Triangle4vIntersector4HybridPluecker,       BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA true  COMMA ArrayIntersectorK_1<4 COMMA TriangleMvIntersectorKPluecker<4 COMMA 4 COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR4(BVH8Triangle4iIntersector4HybridPluecker,       BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA true  COMMA ArrayIntersectorK_1<4 COMMA TriangleMiIntersectorKPluecker<4 COMMA 4 COMMA true> > >));

    IF_ENABLED_TRIS(DEFINE_INTERSECTOR4(BVH8Triangle4vMBIntersector4HybridMoeller,  BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN2_AN4D COMMA false COMMA ArrayIntersectorK_1<4 COMMA TriangleMvMBIntersectorKMoeller <4 COMMA 4 COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR4(BVH8Triangle4iMBIntersector4HybridMoeller,  BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN2_AN4D COMMA false COMMA ArrayIntersectorK_1<4 COMMA TriangleMiMBIntersectorKMoeller <4 COMMA 4 COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR4(BVH8Triangle4vMBIntersector4HybridPluecker, BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN2_AN4D COMMA true  COMMA ArrayIntersectorK_1<4 COMMA TriangleMvMBIntersectorKPluecker<4 COMMA 4 COMMA true> > >));
    IF_ENABLED_TRIS(DEFINE_INTERSECTOR4(BVH8Triangle4iMBIntersector4HybridPluecker, BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN2_AN4D COMMA true  COMMA ArrayIntersectorK_1<4 COMMA TriangleMiMBIntersectorKPluecker<4 COMMA 4 COMMA true> > >));

    IF_ENABLED_QUADS(DEFINE_INTERSECTOR4(BVH8Quad4vIntersector4HybridMoeller,        BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA false COMMA ArrayIntersectorK_1<4 COMMA QuadMvIntersectorKMoeller <4 COMMA 4 COMMA true> > >));
    IF_ENABLED_QUADS(DEFINE_INTERSECTOR4(BVH8Quad4vIntersector4HybridMoellerNoFilter,BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA false COMMA ArrayIntersectorK_1<4 COMMA QuadMvIntersectorKMoeller <4 COMMA 4 COMMA false> > >));
    IF_ENABLED_QUADS(DEFINE_INTERSECTOR4(BVH8Quad4iIntersector4HybridMoeller,        BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA false COMMA ArrayIntersectorK_1<4 COMMA QuadMiIntersectorKMoeller <4 COMMA 4 COMMA true> > >));
    IF_ENABLED_QUADS(DEFINE_INTERSECTOR4(BVH8Quad4vIntersector4HybridPluecker,       BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA true  COMMA ArrayIntersectorK_1<4 COMMA QuadMvIntersectorKPluecker<4 COMMA 4 COMMA true> > >));
    IF_ENABLED_QUADS(DEFINE_INTERSECTOR4(BVH8Quad4iIntersector4HybridPluecker,       BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA true  COMMA ArrayIntersectorK_1<4 COMMA QuadMiIntersectorKPluecker<4 COMMA 4 COMMA true> > >));

    IF_ENABLED_QUADS(DEFINE_INTERSECTOR4(BVH8Quad4iMBIntersector4HybridMoeller,      BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN2_AN4D COMMA false COMMA ArrayIntersectorK_1<4 COMMA QuadMiMBIntersectorKMoeller <4 COMMA 4 COMMA true> > >));
    IF_ENABLED_QUADS(DEFINE_INTERSECTOR4(BVH8Quad4iMBIntersector4HybridPluecker,     BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN2_AN4D COMMA true  COMMA ArrayIntersectorK_1<4 COMMA QuadMiMBIntersectorKPluecker<4 COMMA 4 COMMA true> > >));

    IF_ENABLED_CURVES_OR_POINTS(DEFINE_INTERSECTOR4(BVH8OBBVirtualCurveIntersector4Hybrid, BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1_UN1 COMMA false COMMA VirtualCurveIntersectorK<4> >));
    IF_ENABLED_CURVES_OR_POINTS(DEFINE_INTERSECTOR4(BVH8OBBVirtualCurveIntersector4HybridMB, BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN2_AN4D_UN2 COMMA false COMMA VirtualCurveIntersectorK<4> >));

    IF_ENABLED_CURVES_OR_POINTS(DEFINE_INTERSECTOR4(BVH8OBBVirtualCurveIntersectorRobust4Hybrid, BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1_UN1 COMMA true COMMA VirtualCurveIntersectorK<4> >));
    IF_ENABLED_CURVES_OR_POINTS(DEFINE_INTERSECTOR4(BVH8OBBVirtualCurveIntersectorRobust4HybridMB, BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN2_AN4D_UN2 COMMA true COMMA VirtualCurveIntersectorK<4> >));

    IF_ENABLED_USER(DEFINE_INTERSECTOR4(BVH8VirtualIntersector4Chunk, BVHNIntersectorKChunk<8 COMMA 4 COMMA BVH_AN1 COMMA false COMMA ArrayIntersectorK_1<4 COMMA ObjectIntersector4> >));
    IF_ENABLED_USER(DEFINE_INTERSECTOR4(BVH8VirtualMBIntersector4Chunk, BVHNIntersectorKChunk<8 COMMA 4 COMMA BVH_AN2_AN4D COMMA false COMMA ArrayIntersectorK_1<4 COMMA ObjectIntersector4MB> >));

    IF_ENABLED_INSTANCE(DEFINE_INTERSECTOR4(BVH8InstanceIntersector4Chunk, BVHNIntersectorKChunk<8 COMMA 4 COMMA BVH_AN1 COMMA false COMMA ArrayIntersectorK_1<4 COMMA InstanceIntersectorK<4>> >));
    IF_ENABLED_INSTANCE(DEFINE_INTERSECTOR4(BVH8InstanceMBIntersector4Chunk, BVHNIntersectorKChunk<8 COMMA 4 COMMA BVH_AN2_AN4D COMMA false COMMA ArrayIntersectorK_1<4 COMMA InstanceIntersectorKMB<4>> >));

    IF_ENABLED_INSTANCE_ARRAY(DEFINE_INTERSECTOR4(BVH8InstanceArrayIntersector4Chunk, BVHNIntersectorKChunk<8 COMMA 4 COMMA BVH_AN1 COMMA false COMMA ArrayIntersectorK_1<4 COMMA InstanceArrayIntersectorK<4>> >));
    IF_ENABLED_INSTANCE_ARRAY(DEFINE_INTERSECTOR4(BVH8InstanceArrayMBIntersector4Chunk, BVHNIntersectorKChunk<8 COMMA 4 COMMA BVH_AN2_AN4D COMMA false COMMA ArrayIntersectorK_1<4 COMMA InstanceArrayIntersectorKMB<4>> >));

    IF_ENABLED_GRIDS(DEFINE_INTERSECTOR4(BVH8GridIntersector4HybridMoeller, BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA false COMMA SubGridIntersectorKMoeller <8 COMMA 4 COMMA true> >));
    IF_ENABLED_GRIDS(DEFINE_INTERSECTOR4(BVH8GridIntersector4HybridPluecker, BVHNIntersectorKHybrid<8 COMMA 4 COMMA BVH_AN1 COMMA true COMMA SubGridIntersectorKPluecker <8 COMMA 4 COMMA true> >));

  }
}

#endif
