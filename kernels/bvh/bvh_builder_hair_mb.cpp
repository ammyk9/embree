// ======================================================================== //
// Copyright 2009-2018 Intel Corporation                                    //
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

#include "../builders/bvh_builder_hair.h"
#include "../builders/bvh_builder_msmblur_hair.h"
#include "../builders/primrefgen.h"

#include "../geometry/bezier1i.h"
#include "../geometry/bezierNi_mb.h"

#if defined(EMBREE_GEOMETRY_CURVES)

namespace embree
{
  namespace isa
  {
    /* FIXME: add fast path for single-segment motion blur */
    template<int N, typename Primitive>
    struct BVHNHairMBlurBuilderSAH : public Builder
    {
      typedef BVHN<N> BVH;
      typedef typename BVH::NodeRef NodeRef;
      typedef typename BVH::NodeRecordMB4D NodeRecordMB4D;

      BVH* bvh;
      Scene* scene;

      BVHNHairMBlurBuilderSAH (BVH* bvh, Scene* scene)
        : bvh(bvh), scene(scene) {}
      
      void build() 
      {
        /* fast path for empty BVH */
        const size_t numPrimitives = scene->getNumPrimitives<NativeCurves,true>();
        if (numPrimitives == 0) {
          bvh->set(BVH::emptyNode,empty,0);
          return;
        }

        double t0 = bvh->preBuild(TOSTRING(isa) "::BVH" + toString(N) + "HairMBlurBuilderSAH");

        //profile(1,5,numPrimitives,[&] (ProfileTimer& timer) {

        /* create primref array */
        mvector<PrimRefMB> prims0(scene->device,numPrimitives);
        const PrimInfoMB pinfo = createPrimRefArrayMSMBlur<NativeCurves>(scene,prims0,bvh->scene->progressInterface);

        /* estimate acceleration structure size */
        const size_t node_bytes = pinfo.num_time_segments*sizeof(typename BVH::AlignedNodeMB)/(4*N);
        const size_t leaf_bytes = size_t(1.2*Primitive::blocks(pinfo.num_time_segments)*sizeof(Primitive));
        bvh->alloc.init_estimate(node_bytes+leaf_bytes);
    
        /* settings for BVH build */
        BVHBuilderHairMSMBlur::Settings settings;
        settings.branchingFactor = N;
        settings.maxDepth = BVH::maxBuildDepthLeaf;
        settings.logBlockSize = __bsf(Primitive::max_size());
        settings.minLeafSize = Primitive::max_size();
        settings.maxLeafSize = Primitive::max_size()*BVH::maxLeafBlocks;

        /* creates a leaf node */
        auto createLeaf = [&] (const SetMB& prims, const FastAllocator::CachedAllocator& alloc) -> NodeRecordMB4D {
          return Primitive::createLeafMB(bvh,prims,alloc);
        };

        /* build the hierarchy */
        auto root = BVHBuilderHairMSMBlur::build<NodeRef>
          (scene, prims0, pinfo,
           RecalculatePrimRef<NativeCurves>(scene),
           typename BVH::CreateAlloc(bvh),
           typename BVH::AlignedNodeMB4D::Create(),
           typename BVH::AlignedNodeMB4D::Set(),
           typename BVH::UnalignedNodeMB::Create(),
           typename BVH::UnalignedNodeMB::Set(),
           createLeaf,
           bvh->scene->progressInterface,
           settings);
        
        bvh->set(root.ref,root.lbounds,pinfo.num_time_segments);
        
        //});
        
        /* clear temporary data for static geometry */
        if (scene->isStaticAccel()) bvh->shrink();
        bvh->cleanup();
        bvh->postBuild(t0);
      }

      void clear() {
      }
    };
    
    /*! entry functions for the builder */
    Builder* BVH4OBBBezier1iMBBuilder_OBB (void* bvh, Scene* scene, size_t mode) { return new BVHNHairMBlurBuilderSAH<4,Bezier1i>((BVH4*)bvh,scene); }
    Builder* BVH4OBBBezierNiMBBuilder_OBB (void* bvh, Scene* scene, size_t mode) { return new BVHNHairMBlurBuilderSAH<4,BezierNiMB<8>>((BVH4*)bvh,scene); }

#if defined(__AVX__)
    Builder* BVH8OBBBezier1iMBBuilder_OBB (void* bvh, Scene* scene, size_t mode) { return new BVHNHairMBlurBuilderSAH<8,Bezier1i>((BVH8*)bvh,scene); }
    Builder* BVH8OBBBezierNiMBBuilder_OBB (void* bvh, Scene* scene, size_t mode) { return new BVHNHairMBlurBuilderSAH<8,BezierNiMB<8>>((BVH8*)bvh,scene); }
#endif

  }
}
#endif
