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

#include "bvh.h"
#include <sstream>

namespace embree
{
  template<int N>
  class BVHNStatistics
  {
    typedef BVHN<N> BVH;
    typedef typename BVH::AlignedNode AlignedNode;
    typedef typename BVH::UnalignedNode UnalignedNode;
    typedef typename BVH::AlignedNodeMB AlignedNodeMB;
    typedef typename BVH::AlignedNodeMB4D AlignedNodeMB4D;
    typedef typename BVH::UnalignedNodeMB UnalignedNodeMB;
    typedef typename BVH::TransformNode TransformNode;
    typedef typename BVH::QuantizedNode QuantizedNode;

    typedef typename BVH::NodeRef NodeRef;

    struct Statistics 
    {
      template<typename Node>
        struct NodeStat
      {
        NodeStat ( double nodeSAH = 0,
                   size_t numNodes = 0, 
                   size_t numChildren = 0)
        : nodeSAH(nodeSAH),
          numNodes(numNodes), 
          numChildren(numChildren) {}
        
        double sah(BVH* bvh) const {
          return nodeSAH/bvh->getLinearBounds().expectedHalfArea();
        }

        size_t bytes() const {
          return numNodes*sizeof(Node);
        }

        size_t size() const {
          return numNodes;
        }

        double fillRateNom () const { return double(numChildren);  }
        double fillRateDen () const { return double(numNodes*N);  }
        double fillRate    () const { return fillRateNom()/fillRateDen(); }

        __forceinline friend NodeStat operator+ ( const NodeStat& a, const NodeStat& b)
        {
          return NodeStat(a.nodeSAH + b.nodeSAH,
                          a.numNodes+b.numNodes,
                          a.numChildren+b.numChildren);
        }

        std::string toString(BVH* bvh, double sahTotal, size_t bytesTotal) const
        {
          std::ostringstream stream;
          stream.setf(std::ios::fixed, std::ios::floatfield);
          stream << "sah = " << std::setw(7) << std::setprecision(3) << sah(bvh);
          stream << " (" << std::setw(6) << std::setprecision(2) << 100.0*sah(bvh)/sahTotal << "%), ";          
          stream << "#bytes = " << std::setw(7) << std::setprecision(2) << bytes()/1E6  << " MB ";
          stream << "(" << std::setw(6) << std::setprecision(2) << 100.0*double(bytes())/double(bytesTotal) << "%), ";
          stream << "#nodes = " << std::setw(9) << numNodes << " (" << std::setw(6) << std::setprecision(2) << 100.0*fillRate() << "% filled), ";
          stream << "#bytes/prim = " << std::setw(6) << std::setprecision(2) << double(bytes())/double(bvh->numPrimitives);
          return stream.str();
        }

      public:
        double nodeSAH;
        size_t numNodes;
        size_t numChildren;
      };

      struct LeafStat
      {
        static const int NHIST = 8;

        LeafStat ( double leafSAH = 0.0f, 
                   size_t numLeaves = 0,
                   size_t numPrims = 0,
                   size_t numPrimBlocks = 0,
                   int ty = 0)
        : leafSAH(leafSAH),
          numLeaves(numLeaves),
          numPrims(numPrims),
          numPrimBlocks(numPrimBlocks),
          ty(ty)
        {
          for (size_t i=0; i<NHIST; i++)
            numPrimBlocksHistogram[i] = 0;
        }

        double sah(BVH* bvh) const {
          return leafSAH/bvh->getLinearBounds().expectedHalfArea();
        }

        size_t bytes(BVH* bvh) const {
          return numPrimBlocks*bvh->getPrimType(ty).bytes;
        }

        size_t size() const {
          return numLeaves;
        }

        double fillRateNom (BVH* bvh) const { return double(numPrims);  }
        double fillRateDen (BVH* bvh) const { return double(bvh->getPrimType(ty).blockSize*numPrimBlocks);  }
        double fillRate    (BVH* bvh) const { return fillRateNom(bvh)/fillRateDen(bvh); }

        __forceinline friend LeafStat operator+ ( const LeafStat& a, const LeafStat& b)
        {
          assert(a.ty == b.ty);
          LeafStat stat(a.leafSAH + b.leafSAH,
                        a.numLeaves+b.numLeaves,
                        a.numPrims+b.numPrims,
                        a.numPrimBlocks+b.numPrimBlocks,
                        a.ty);
          for (size_t i=0; i<NHIST; i++) {
            stat.numPrimBlocksHistogram[i] += a.numPrimBlocksHistogram[i];
            stat.numPrimBlocksHistogram[i] += b.numPrimBlocksHistogram[i];
          }
          return stat;
        }

        std::string toString(BVH* bvh, double sahTotal, size_t bytesTotal) const
        {
          std::ostringstream stream;
          stream.setf(std::ios::fixed, std::ios::floatfield);
          stream << "sah = " << std::setw(7) << std::setprecision(3) << sah(bvh);
          stream << " (" << std::setw(6) << std::setprecision(2) << 100.0*sah(bvh)/sahTotal << "%), ";
          stream << "#bytes = " << std::setw(7) << std::setprecision(2) << double(bytes(bvh))/1E6  << " MB ";
          stream << "(" << std::setw(6) << std::setprecision(2) << 100.0*double(bytes(bvh))/double(bytesTotal) << "%), ";
          stream << "#nodes = " << std::setw(9) << numLeaves << " (" << std::setw(6) << std::setprecision(2) << 100.0*fillRate(bvh) << "% filled), ";
          stream << "#bytes/prim = " << std::setw(6) << std::setprecision(2) << double(bytes(bvh))/double(bvh->numPrimitives);
          return stream.str();
        }

        std::string histToString() const
        {
          std::ostringstream stream;
          stream.setf(std::ios::fixed, std::ios::floatfield);
          for (size_t i=0; i<NHIST; i++)
            stream << std::setw(6) << std::setprecision(2) << 100.0f*float(numPrimBlocksHistogram[i])/float(numLeaves) << "% ";
          return stream.str();
        }
     
      public:
        double leafSAH;                    //!< SAH of the leaves only
        size_t numLeaves;                  //!< Number of leaf nodes.
        size_t numPrims;                   //!< Number of primitives.
        size_t numPrimBlocks;              //!< Number of primitive blocks.
        size_t numPrimBlocksHistogram[8];
        int ty;
      };

      struct MultiLeafStat
      {
        static const unsigned M = 8;

        MultiLeafStat() 
        {
          for (size_t i=0; i<M; i++)
            stat[i].ty = i;
        }

        double sah(BVH* bvh) const 
        {
          double f = 0.0;
          for (size_t i=0; i<M; i++)
            f += stat[i].leafSAH/bvh->getLinearBounds().expectedHalfArea();
          return f;
        }

        size_t bytes(BVH* bvh) const 
        {
          size_t b = 0;
          for (size_t i=0; i<M; i++)
            b += stat[i].numPrimBlocks*bvh->getPrimType(i).bytes;
          return b;
        }

        size_t size() const 
        {
          size_t n = 0;
          for (size_t i=0; i<M; i++)
            n += stat[i].numLeaves;
          return n;
        }
        
        double fillRateNom (BVH* bvh) const 
        { 
          double n = 0.0;
          for (size_t i=0; i<M; i++)
            n += stat[i].fillRateNom(bvh);
          return n;
        }

        double fillRateDen (BVH* bvh) const 
        { 
          double n = 0.0;
          for (size_t i=0; i<M; i++)
            n += stat[i].fillRateDen(bvh);
          return n;
        }

        double fillRate(BVH* bvh) const { 
          return fillRateNom(bvh)/fillRateDen(bvh); 
        }

        __forceinline friend MultiLeafStat operator+ ( const MultiLeafStat& a, const MultiLeafStat& b)
        {
          MultiLeafStat c;
          for (size_t i=0; i<M; i++)
            c.stat[i] = a.stat[i]+b.stat[i];
          return c;
        }

        LeafStat stat[M];
      };

    public:
      Statistics (size_t depth = 0,
                  MultiLeafStat statLeaf = MultiLeafStat(),
                  NodeStat<AlignedNode> statAlignedNodes = NodeStat<AlignedNode>(),
                  NodeStat<UnalignedNode> statUnalignedNodes = NodeStat<UnalignedNode>(),
                  NodeStat<AlignedNodeMB> statAlignedNodesMB = NodeStat<AlignedNodeMB>(),
                  NodeStat<AlignedNodeMB4D> statAlignedNodesMB4D = NodeStat<AlignedNodeMB4D>(),
                  NodeStat<UnalignedNodeMB> statUnalignedNodesMB = NodeStat<UnalignedNodeMB>(),
                  NodeStat<TransformNode> statTransformNodes = NodeStat<TransformNode>(),
                  NodeStat<QuantizedNode> statQuantizedNodes = NodeStat<QuantizedNode>())

      : depth(depth), 
        statLeaf(statLeaf),
        statAlignedNodes(statAlignedNodes),
        statUnalignedNodes(statUnalignedNodes),
        statAlignedNodesMB(statAlignedNodesMB),
        statAlignedNodesMB4D(statAlignedNodesMB4D),
        statUnalignedNodesMB(statUnalignedNodesMB),
        statTransformNodes(statTransformNodes),
        statQuantizedNodes(statQuantizedNodes) {}

      double sah(BVH* bvh) const 
      {
        return statLeaf.sah(bvh) +
          statAlignedNodes.sah(bvh) + 
          statUnalignedNodes.sah(bvh) + 
          statAlignedNodesMB.sah(bvh) + 
          statAlignedNodesMB4D.sah(bvh) + 
          statUnalignedNodesMB.sah(bvh) + 
          statTransformNodes.sah(bvh) + 
          statQuantizedNodes.sah(bvh);
      }
      
      size_t bytes(BVH* bvh) const {
        return statLeaf.bytes(bvh) +
          statAlignedNodes.bytes() + 
          statUnalignedNodes.bytes() + 
          statAlignedNodesMB.bytes() + 
          statAlignedNodesMB4D.bytes() + 
          statUnalignedNodesMB.bytes() + 
          statTransformNodes.bytes() + 
          statQuantizedNodes.bytes();
      }

      size_t size() const 
      {
        return statLeaf.size() +
          statAlignedNodes.size() + 
          statUnalignedNodes.size() + 
          statAlignedNodesMB.size() + 
          statAlignedNodesMB4D.size() + 
          statUnalignedNodesMB.size() + 
          statTransformNodes.size() + 
          statQuantizedNodes.size();
      }

      double fillRate (BVH* bvh) const 
      {
        double nom = statLeaf.fillRateNom(bvh) +
          statAlignedNodes.fillRateNom() + 
          statUnalignedNodes.fillRateNom() + 
          statAlignedNodesMB.fillRateNom() + 
          statAlignedNodesMB4D.fillRateNom() + 
          statUnalignedNodesMB.fillRateNom() + 
          statTransformNodes.fillRateNom() + 
          statQuantizedNodes.fillRateNom();
        double den = statLeaf.fillRateDen(bvh) +
          statAlignedNodes.fillRateDen() + 
          statUnalignedNodes.fillRateDen() + 
          statAlignedNodesMB.fillRateDen() + 
          statAlignedNodesMB4D.fillRateDen() + 
          statUnalignedNodesMB.fillRateDen() + 
          statTransformNodes.fillRateDen() + 
          statQuantizedNodes.fillRateDen();
        return nom/den;
      }

      friend Statistics operator+ ( const Statistics& a, const Statistics& b )
      {
        return Statistics(max(a.depth,b.depth),
                          a.statLeaf + b.statLeaf,
                          a.statAlignedNodes + b.statAlignedNodes,
                          a.statUnalignedNodes + b.statUnalignedNodes,
                          a.statAlignedNodesMB + b.statAlignedNodesMB,
                          a.statAlignedNodesMB4D + b.statAlignedNodesMB4D,
                          a.statUnalignedNodesMB + b.statUnalignedNodesMB,
                          a.statTransformNodes + b.statTransformNodes,
                          a.statQuantizedNodes + b.statQuantizedNodes);
      }

      static Statistics add ( const Statistics& a, const Statistics& b ) {
        return a+b;
      }

    public:
      size_t depth;
      MultiLeafStat statLeaf;
      NodeStat<AlignedNode> statAlignedNodes;
      NodeStat<UnalignedNode> statUnalignedNodes;
      NodeStat<AlignedNodeMB> statAlignedNodesMB;
      NodeStat<AlignedNodeMB4D> statAlignedNodesMB4D;
      NodeStat<UnalignedNodeMB> statUnalignedNodesMB;
      NodeStat<TransformNode> statTransformNodes;
      NodeStat<QuantizedNode> statQuantizedNodes;
    };

  public:

    /* Constructor gathers statistics. */
    BVHNStatistics (BVH* bvh);

    /*! Convert statistics into a string */
    std::string str();

    double sah() const { 
      return stat.sah(bvh); 
    }

    size_t bytesUsed() const {
      return stat.bytes(bvh);
    }

  private:
    Statistics statistics(NodeRef node, const double A, const BBox1f dt);

  private:
    BVH* bvh;
    Statistics stat;
  };

  typedef BVHNStatistics<4> BVH4Statistics;
  typedef BVHNStatistics<8> BVH8Statistics;
}
