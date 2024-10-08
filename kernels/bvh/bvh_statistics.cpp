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

#include "bvh_statistics.h"
#include "../../common/algorithms/parallel_reduce.h"

namespace embree
{
  template<int N>
  BVHNStatistics<N>::BVHNStatistics (BVH* bvh) : bvh(bvh)
  {
    double A = max(0.0f,bvh->getLinearBounds().expectedHalfArea());
    stat = statistics(bvh->root,A,BBox1f(0.0f,1.0f));
  }
  
  template<int N>
  std::string BVHNStatistics<N>::str()
  {
    std::ostringstream stream;
    stream.setf(std::ios::fixed, std::ios::floatfield);
    stream << "  primitives = " << bvh->numPrimitives << ", vertices = " << bvh->numVertices << ", depth = " << stat.depth << std::endl;
    size_t totalBytes = stat.bytes(bvh);
    double totalSAH = stat.sah(bvh);
    stream << "  total            : sah = "  << std::setw(7) << std::setprecision(3) << totalSAH << " (100.00%), ";
    stream << "#bytes = " << std::setw(7) << std::setprecision(2) << totalBytes/1E6 << " MB (100.00%), ";
    stream << "#nodes = " << std::setw(9) << stat.size() << " (" << std::setw(6) << std::setprecision(2) << 100.0*stat.fillRate(bvh) << "% filled), ";
    stream << "#bytes/prim = " << std::setw(6) << std::setprecision(2) << double(totalBytes)/double(bvh->numPrimitives) << std::endl;
    if (stat.statAlignedNodes.numNodes    ) stream << "  alignedNodes     : "  << stat.statAlignedNodes.toString(bvh,totalSAH,totalBytes) << std::endl;
    if (stat.statUnalignedNodes.numNodes  ) stream << "  unalignedNodes   : "  << stat.statUnalignedNodes.toString(bvh,totalSAH,totalBytes) << std::endl;
    if (stat.statAlignedNodesMB.numNodes  ) stream << "  alignedNodesMB   : "  << stat.statAlignedNodesMB.toString(bvh,totalSAH,totalBytes) << std::endl;
    if (stat.statAlignedNodesMB4D.numNodes) stream << "  alignedNodesMB4D : "  << stat.statAlignedNodesMB4D.toString(bvh,totalSAH,totalBytes) << std::endl;
    if (stat.statUnalignedNodesMB.numNodes) stream << "  unalignedNodesMB : "  << stat.statUnalignedNodesMB.toString(bvh,totalSAH,totalBytes) << std::endl;
    if (stat.statTransformNodes.numNodes  ) stream << "  transformNodes   : "  << stat.statTransformNodes.toString(bvh,totalSAH,totalBytes) << std::endl;
    if (stat.statQuantizedNodes.numNodes  ) stream << "  quantizedNodes   : "  << stat.statQuantizedNodes.toString(bvh,totalSAH,totalBytes) << std::endl;
    for (size_t i=0; i<min(bvh->numTypes,Statistics::MultiLeafStat::M); i++) {
      stream << "  " << std::setw(16) << std::left << bvh->getPrimType(i).name << " : " << stat.statLeaf.stat[i].toString(bvh,totalSAH,totalBytes) << std::endl;
    }
    for (size_t i=0; i<min(bvh->numTypes,Statistics::MultiLeafStat::M); i++) {
      stream << "  " << std::setw(16) << std::left << bvh->getPrimType(i).name << " : "  << stat.statLeaf.stat[i].histToString() << std::endl;
    }
    //if (true)                              
    return stream.str();
  }
  
  template<int N>
  typename BVHNStatistics<N>::Statistics BVHNStatistics<N>::statistics(NodeRef node, const double A, const BBox1f t0t1)
  {
    Statistics s;
    assert(t0t1.size() > 0.0f);
    double dt = max(0.0f,t0t1.size());
    if (node.isAlignedNode())
    {
      AlignedNode* n = node.alignedNode();
      s = s + parallel_reduce(0,N,Statistics(),[&] ( const int i ) {
          if (n->child(i) == BVH::emptyNode) return Statistics();
          const double Ai = max(0.0f,halfArea(n->extend(i)));
          Statistics s = statistics(n->child(i),Ai,t0t1); 
          s.statAlignedNodes.numChildren++;
          return s;
        }, Statistics::add);
      s.statAlignedNodes.numNodes++;
      s.statAlignedNodes.nodeSAH += dt*A;
      s.depth++;
    }
    else if (node.isUnalignedNode())
    {
      UnalignedNode* n = node.unalignedNode();
      s = s + parallel_reduce(0,N,Statistics(),[&] ( const int i ) {
          if (n->child(i) == BVH::emptyNode) return Statistics();
          const double Ai = max(0.0f,halfArea(n->extent(i)));
          Statistics s = statistics(n->child(i),Ai,t0t1); 
          s.statUnalignedNodes.numChildren++;
          return s;
        }, Statistics::add);
      s.statUnalignedNodes.numNodes++;
      s.statUnalignedNodes.nodeSAH += dt*A;
      s.depth++;
    }
    else if (node.isAlignedNodeMB())
    {
      AlignedNodeMB* n = node.alignedNodeMB();
      s = s + parallel_reduce(0,N,Statistics(),[&] ( const int i ) {
          if (n->child(i) == BVH::emptyNode) return Statistics();
          const double Ai = max(0.0f,n->expectedHalfArea(i,t0t1));
          Statistics s = statistics(n->child(i),Ai,t0t1);
          s.statAlignedNodesMB.numChildren++;
          return s;
        }, Statistics::add);
      s.statAlignedNodesMB.numNodes++;
      s.statAlignedNodesMB.nodeSAH += dt*A;
      s.depth++;
    }
    else if (node.isAlignedNodeMB4D())
    {
      AlignedNodeMB4D* n = node.alignedNodeMB4D();
      s = s + parallel_reduce(0,N,Statistics(),[&] ( const int i ) {
          if (n->child(i) == BVH::emptyNode) return Statistics();
          const BBox1f t0t1i = intersect(t0t1,n->timeRange(i));
          assert(!t0t1i.empty());
          const double Ai = n->AlignedNodeMB::expectedHalfArea(i,t0t1i);
          Statistics s =  statistics(n->child(i),Ai,t0t1i);
          s.statAlignedNodesMB4D.numChildren++;
          return s;
        }, Statistics::add);
      s.statAlignedNodesMB4D.numNodes++;
      s.statAlignedNodesMB4D.nodeSAH += dt*A;
      s.depth++;
    }
    else if (node.isUnalignedNodeMB())
    {
      UnalignedNodeMB* n = node.unalignedNodeMB();
      s = s + parallel_reduce(0,N,Statistics(),[&] ( const int i ) {
          if (n->child(i) == BVH::emptyNode) return Statistics();
          const double Ai = max(0.0f,halfArea(n->extent0(i)));
          Statistics s = statistics(n->child(i),Ai,t0t1); 
          s.statUnalignedNodesMB.numChildren++;
          return s;
        }, Statistics::add);
      s.statUnalignedNodesMB.numNodes++;
      s.statUnalignedNodesMB.nodeSAH += dt*A;
      s.depth++;
    }
    else if (node.isTransformNode())
    {
#if 0
      TransformNode* n = node.transformNode();
      s = s + statistics(n->child,0.0f,t0t1); 
#endif
      s.statTransformNodes.numNodes++;
      s.statTransformNodes.nodeSAH += dt*A;
      s.depth++;
    }
    else if (node.isQuantizedNode())
    {
      QuantizedNode* n = node.quantizedNode();
      s = s + parallel_reduce(0,N,Statistics(),[&] ( const int i ) {
          if (n->child(i) == BVH::emptyNode) return Statistics();
          const double Ai = max(0.0f,halfArea(n->extent(i)));
          Statistics s = statistics(n->child(i),Ai,t0t1); 
          s.statQuantizedNodes.numChildren++;
          return s;
        }, Statistics::add);
      s.statQuantizedNodes.numNodes++;
      s.statQuantizedNodes.nodeSAH += dt*A;
      s.depth++;
    }
    else if (node.isLeaf())
    {
      size_t lty; const char* tri = node.leaf(lty);
      if (node != BVH::emptyNode)
      {
        //Leaf::Type ty = Leaf::loadTy(tri);
        Leaf::Type ty = (Leaf::Type) lty;
        if (bvh->primTy) ty = (Leaf::Type) 0; // for non multi leaves always use slot 0
        assert(ty < Statistics::MultiLeafStat::M);
        typename Statistics::LeafStat& stat = s.statLeaf.stat[ty];
        size_t num = 0;
        while (true) {
          const char* cur = tri+num*bvh->getPrimType(ty).bytes;
          stat.numPrims += bvh->getPrimType(ty).size(cur);
          num++;
          if (bvh->getPrimType(ty).last(cur)) break;
        }
        stat.numLeaves++;
        stat.numPrimBlocks += num;
        stat.leafSAH += dt*A*num;
        if (num-1 < Statistics::LeafStat::NHIST) {
          stat.numPrimBlocksHistogram[num-1]++;
        }
      }
    }
    else {
      throw std::runtime_error("not supported node type in bvh_statistics");
    }
    return s;
  } 

#if defined(__AVX__)
  template class BVHNStatistics<8>;
#endif

#if !defined(__AVX__) || !defined(EMBREE_TARGET_SSE2) && !defined(EMBREE_TARGET_SSE42)
  template class BVHNStatistics<4>;
#endif
}
