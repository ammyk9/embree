#pragma once

#include "qbvh6.h"
#include "gpu/AABB3f.h"
#include "gpu/sort.h"
#include "gpu/morton.h"
#include <memory>
#include "../../common/scene.h" 
#include "rthwif_builder.h"


#if defined(EMBREE_SYCL_SUPPORT)

#define BVH_BRANCHING_FACTOR         6
#define FATLEAF_THRESHOLD            6
#define PAIR_OFFSET_SHIFT            28
#define GEOMID_MASK                  (((unsigned int)1<<PAIR_OFFSET_SHIFT)-1)
#define LARGE_WG_SIZE                1024
#define TRIANGLE_QUAD_BLOCK_SIZE     64
#define QBVH6_HEADER_OFFSET          128
#define HOST_DEVICE_COMM_BUFFER_SIZE 16*sizeof(unsigned int)
#define EQUAL_DISTANCES_WORKAROUND   1
#define REBALANCE_BVH2_MINIMUM_DEPTH 30

#if 1
#define TOP_LEVEL_RATIO              5.0f
#define BOTTOM_LEVEL_RATIO           5.0f
#else
#define TOP_LEVEL_RATIO              0.0f
#define BOTTOM_LEVEL_RATIO           0.0f
#endif

namespace embree
{  
  // ===================================================================================================================================================================================
  // =============================================================================== General ===========================================================================================
  // ===================================================================================================================================================================================
  
  struct __aligned(64) PLOCGlobals
  {
    // === first 64 bytes ===       
    gpu::AABB3f geometryBounds;
    gpu::AABB3f centroidBounds; 
    char *qbvh_base_pointer;
    unsigned int node_mem_allocator_start;
    unsigned int node_mem_allocator_cur;
    // === second 64 bytes ===    
    unsigned int numTriangles;
    unsigned int numQuads;
    unsigned int numMergedTrisQuads;
    unsigned int numProcedurals;
    unsigned int numInstances;
    unsigned int numQuadBlocks;
    unsigned int numPrimitives;    
    unsigned int bvh2_index_allocator;    
    unsigned int leaf_mem_allocator_start;
    unsigned int leaf_mem_allocator_cur;
    unsigned int range_start;
    unsigned int range_end;
    unsigned int sync;
    unsigned int rootIndex;
    unsigned int wgID;
    unsigned int numLeaves;

    __forceinline void reset()
    {
      geometryBounds.init();
      centroidBounds.init();
      qbvh_base_pointer = nullptr;
      numTriangles               = 0;
      numQuads                   = 0;
      numMergedTrisQuads         = 0;
      numProcedurals             = 0;
      numInstances               = 0;
      numQuadBlocks              = 0;      
      node_mem_allocator_cur     = 0;
      node_mem_allocator_start   = 0;
      numPrimitives              = 0;
      bvh2_index_allocator       = 0;
      leaf_mem_allocator_cur     = 0;
      leaf_mem_allocator_start   = 0;
      range_start                = 0;
      range_end                  = 0;
      sync                       = 0;      
      rootIndex                  = 0;
      wgID                       = 0;
      numLeaves                  = 0;
    }
      
    /* allocate data in the node memory section */
    __forceinline char* atomic_allocNode(const unsigned int bytes)
    {
      unsigned int blocks = (unsigned int)bytes / 64;
      const unsigned int current = gpu::atomic_add_global(&node_mem_allocator_cur,blocks);      
      char* ptr = qbvh_base_pointer + 64 * (size_t)current;
      return ptr;
    }

    /* allocate memory in the leaf memory section */
    __forceinline char* atomic_allocLeaf(const unsigned int bytes)
    {
      unsigned int blocks = (unsigned int)bytes / 64;      
      const unsigned int current = gpu::atomic_add_global(&leaf_mem_allocator_cur,blocks);            
      char* ptr = qbvh_base_pointer + 64 * (size_t)current;
      return ptr;
    }
      
    __forceinline char* basePtr() {
      return qbvh_base_pointer;
    }

    __forceinline char* nodeBlockPtr(const unsigned int blockID) {
      return qbvh_base_pointer + 64 * size_t(blockID);
    }

    __forceinline char* leafLocalPtr(const unsigned int localID=0) {
      return qbvh_base_pointer + 64 * size_t(leaf_mem_allocator_start + localID);
    }

    __forceinline unsigned int getBlockIDFromPtr(const char *addr) {
      return (addr - qbvh_base_pointer)/64;
    }
    
    __forceinline unsigned int atomic_add_sub_group_varying_allocNodeBlocks(const unsigned int blocks)
    {
      return gpu::atomic_add_global_sub_group_varying(&node_mem_allocator_cur,blocks);      
    }    
    
    __forceinline char* atomic_add_sub_group_varying_allocLeaf(const unsigned int bytes)
    {
      unsigned int blocks = (unsigned int)bytes / 64;      
      const unsigned int current = gpu::atomic_add_global_sub_group_varying(&leaf_mem_allocator_cur,blocks);            
      char* ptr = qbvh_base_pointer + 64 * (size_t)current;
      return ptr;
    }      
            
    __forceinline void resetGlobalCounters()
    {
      node_mem_allocator_cur = node_mem_allocator_start;
      leaf_mem_allocator_cur = leaf_mem_allocator_start;
    }
            
  };

  static_assert(sizeof(PLOCGlobals) == 128, "PLOCGlobals must be 128 bytes large");

  struct BVH2SubTreeState
  {
    static const unsigned int DEPTH_BITS = 7;
    static const unsigned int LEAVES_BITS = 32 - DEPTH_BITS - 1;

    unsigned int depth  : DEPTH_BITS;
    unsigned int leaves : LEAVES_BITS;
    unsigned int mark   : 1;
    
    static const unsigned int MAX_LEAVES = ((unsigned int)1 << LEAVES_BITS)-1;
    static const unsigned int MAX_DEPTH  = ((unsigned int)1 << DEPTH_BITS)-1;

    __forceinline BVH2SubTreeState() {}

    __forceinline BVH2SubTreeState(const unsigned int leaves, const unsigned int depth) : leaves(leaves), depth(depth) {}
    
    __forceinline BVH2SubTreeState(const BVH2SubTreeState &left, const BVH2SubTreeState &right)
    {
      leaves = sycl::min((unsigned int)(left.leaves)+(unsigned int)right.leaves,MAX_LEAVES);
      const unsigned int leftFatLeaf  = left.leaves  <= FATLEAF_THRESHOLD ? 1 : 0;
      const unsigned int rightFatLeaf = right.leaves <= FATLEAF_THRESHOLD ? 1 : 0;
      const unsigned int sum = leftFatLeaf + rightFatLeaf;
#if 1     
      if (sum == 0) depth = 0;
      else if (sum == 2) depth = 1;
      else depth = sycl::max(left.depth,right.depth)+1;
      
      if (sum == 0 && sycl::max(left.depth,right.depth) >= REBALANCE_BVH2_MINIMUM_DEPTH)
        mark = 1;
#else      
      depth = sycl::max(left.depth+(unsigned int)1,right.depth+(unsigned int)1);
#endif      
    }
    
    __forceinline bool isMarked() const
    {
      return mark == 1;
    }
    
  };
        
  struct __aligned(64) QBVHNodeN
  {
    float bounds_lower[3];
    int offset;

    struct {
      uint8_t type;
      uint8_t pad;
      char exp[3];
      uint8_t instMask;
      uint8_t childData[6];
    };

    struct {
      uint8_t lower_x[BVH_BRANCHING_FACTOR];
      uint8_t upper_x[BVH_BRANCHING_FACTOR];
      uint8_t lower_y[BVH_BRANCHING_FACTOR];
      uint8_t upper_y[BVH_BRANCHING_FACTOR];
      uint8_t lower_z[BVH_BRANCHING_FACTOR];
      uint8_t upper_z[BVH_BRANCHING_FACTOR];
    };

    __forceinline float3 start() const { return float3(bounds_lower[0],bounds_lower[1],bounds_lower[2]); }
    
    static __forceinline const gpu::AABB3f quantize_bounds(const float3 &start, const char exp_x, const char exp_y, const char exp_z, const gpu::AABB3f &fbounds) 
    {
      const float3 lower = fbounds.lower()-start;
      const float3 upper = fbounds.upper()-start;
      float qlower_x = ldexpf(lower.x(), -exp_x + 8); 
      float qlower_y = ldexpf(lower.y(), -exp_y + 8); 
      float qlower_z = ldexpf(lower.z(), -exp_z + 8); 
      float qupper_x = ldexpf(upper.x(), -exp_x + 8); 
      float qupper_y = ldexpf(upper.y(), -exp_y + 8); 
      float qupper_z = ldexpf(upper.z(), -exp_z + 8); 
      assert(qlower_x >= 0.0f && qlower_x <= 255.0f);
      assert(qlower_y >= 0.0f && qlower_y <= 255.0f);
      assert(qlower_z >= 0.0f && qlower_z <= 255.0f);
      assert(qupper_x >= 0.0f && qupper_x <= 255.0f);
      assert(qupper_y >= 0.0f && qupper_y <= 255.0f);
      assert(qupper_z >= 0.0f && qupper_z <= 255.0f); 
      qlower_x = min(max(sycl::floor(qlower_x),0.0f),255.0f);
      qlower_y = min(max(sycl::floor(qlower_y),0.0f),255.0f);
      qlower_z = min(max(sycl::floor(qlower_z),0.0f),255.0f);
      qupper_x = min(max(sycl::ceil(qupper_x),0.0f),255.0f);
      qupper_y = min(max(sycl::ceil(qupper_y),0.0f),255.0f);
      qupper_z = min(max(sycl::ceil(qupper_z),0.0f),255.0f);
      gpu::AABB3f qbounds(float3(qlower_x, qlower_y, qlower_z), float3(qupper_x, qupper_y, qupper_z));
      return qbounds;
    }    
  };

  struct __aligned(64) QuadLeafData {
    unsigned int shaderIndex;   
    unsigned int geomIndex;     
    unsigned int primIndex0;
    unsigned int primIndex1Delta;
    float v[4][3];

    __forceinline QuadLeafData() {}

    __forceinline QuadLeafData(const Vec3f &v0, const Vec3f &v1, const Vec3f &v2, const Vec3f &v3,
                               const unsigned int j0, const unsigned int j1, const unsigned int j2,
                               const unsigned int shaderID, const unsigned int geomID, const unsigned int primID0, const unsigned int primID1, const GeometryFlags geomFlags, const unsigned int geomMask)
    {
      shaderIndex = (geomMask << 24) | shaderID;
      geomIndex = geomID | ((unsigned int)geomFlags << 30);
      primIndex0 = primID0;
      const unsigned int delta = primID1 - primID0;
      const unsigned int j = (((j0) << 0) | ((j1) << 2) | ((j2) << 4));
      primIndex1Delta = delta | (j << 16) | (1 << 22); // single prim in leaf            
      v[0][0] = v0.x;
      v[0][1] = v0.y;
      v[0][2] = v0.z;
      v[1][0] = v1.x;
      v[1][1] = v1.y;
      v[1][2] = v1.z;
      v[2][0] = v2.x;
      v[2][1] = v2.y;
      v[2][2] = v2.z;
      v[3][0] = v3.x;
      v[3][1] = v3.y;
      v[3][2] = v3.z;

    }
  };
  

  struct LeafGenerationData {

    __forceinline LeafGenerationData() {}
    __forceinline LeafGenerationData(const unsigned int blockID, const unsigned int primID, const unsigned int geomID) : blockID(blockID),primID(primID),geomID(geomID) {}
    
    unsigned int blockID;
    union {
      unsigned int primID;
      unsigned int bvh2_index;
    };
    union {
      unsigned int geomID;
      unsigned int data;
    };
  };

  struct __aligned(16) TmpNodeState { 
    unsigned int header;
    unsigned int bvh2_index;

    __forceinline TmpNodeState() {}
    __forceinline TmpNodeState(const unsigned int _bvh2_index) : header(0x7fffffff), bvh2_index(_bvh2_index) {}
    __forceinline void init(const unsigned int _bvh2_index) { header = 0x7fffffff; bvh2_index = _bvh2_index;  }
  };


  struct GeometryTypeRanges
  {
    unsigned int triQuad_end;
    unsigned int procedural_end;    
    unsigned int instances_end;

    __forceinline GeometryTypeRanges(const unsigned int triQuads, const unsigned int numProcedurals, const unsigned int numInstances) 
    {
      triQuad_end    = triQuads;
      procedural_end = triQuads + numProcedurals;      
      instances_end  = triQuads + numProcedurals + numInstances;
    }
    
    __forceinline bool isTriQuad(const unsigned int index)    const { return index >= 0              && index < triQuad_end;    }
    __forceinline bool isProcedural(const unsigned int index) const { return index >= triQuad_end    && index < procedural_end; }    
    __forceinline bool isInstance(const unsigned int index)   const { return index >= procedural_end && index < instances_end;  }
    
  };
  
  // ===================================================================================================================================================================================
  // ================================================================================= BVH2 ============================================================================================
  // ===================================================================================================================================================================================
  
  class __aligned(32) BVH2Ploc
  {
  public:
    
    static const unsigned int FATLEAF_SHIFT0     =  31;
    static const unsigned int FATLEAF_SHIFT1     =  30;    
    static const unsigned int FATLEAF_BIT0       =  (unsigned int)1<<FATLEAF_SHIFT0;
    static const unsigned int FATLEAF_MASK       = ~(FATLEAF_BIT0);
    
    unsigned int left;   // 4 bytes
    unsigned int right;  // 4 bytes

    gpu::AABB3f bounds;       // 24 bytes
    
    __forceinline BVH2Ploc() {}

    __forceinline unsigned int leftIndex()      const { return left & FATLEAF_MASK;  }
    __forceinline unsigned int getLeafIndex()   const { return left & FATLEAF_MASK;  }
    __forceinline unsigned int rightIndex()     const { return right & FATLEAF_MASK; }
    
    __forceinline void init(const unsigned int _left, const unsigned int _right, const gpu::AABB3f &_bounds, const BVH2SubTreeState &subtree_size_left, const BVH2SubTreeState &subtree_size_right)
    {
      left    = _left  | ((subtree_size_left.leaves  <= FATLEAF_THRESHOLD ? 1 : 0)<<FATLEAF_SHIFT0);
      right   = _right | ((subtree_size_right.leaves <= FATLEAF_THRESHOLD ? 1 : 0)<<FATLEAF_SHIFT0);

      // === better coalescing ===             
      bounds.lower_x = _bounds.lower_x;
      bounds.lower_y = _bounds.lower_y;
      bounds.lower_z = _bounds.lower_z;
      bounds.upper_x = _bounds.upper_x;
      bounds.upper_y = _bounds.upper_y;
      bounds.upper_z = _bounds.upper_z;                  
    }
    
    __forceinline void initLeaf(const unsigned int _geomID, const unsigned int _primID, const gpu::AABB3f &_bounds)
    {
      left    = _geomID;      
      right   = _primID;

      // === better coalescing ===       
      bounds.lower_x = _bounds.lower_x;
      bounds.lower_y = _bounds.lower_y;
      bounds.lower_z = _bounds.lower_z;
      bounds.upper_x = _bounds.upper_x;
      bounds.upper_y = _bounds.upper_y;
      bounds.upper_z = _bounds.upper_z;                        
    }

    __forceinline void store(BVH2Ploc *dest)
    {
      dest->left  = left;
      dest->right = right;
      dest->bounds.lower_x = bounds.lower_x;
      dest->bounds.lower_y = bounds.lower_y;
      dest->bounds.lower_z = bounds.lower_z;
      dest->bounds.upper_x = bounds.upper_x;
      dest->bounds.upper_y = bounds.upper_y;
      dest->bounds.upper_z = bounds.upper_z;            
    }

    static  __forceinline bool isFatLeaf     (const unsigned int index, const unsigned int numPrimitives) { return (index & FATLEAF_BIT0) || (index & FATLEAF_MASK) < numPrimitives;  }
    static  __forceinline unsigned int getIndex      (const unsigned int index)                           { return index & FATLEAF_MASK;  }
    static  __forceinline bool isLeaf        (const unsigned int index, const unsigned int numPrimitives) { return getIndex(index) < numPrimitives;  }
    static  __forceinline unsigned int makeFatLeaf   (const unsigned int index, const unsigned int numChildren)   { return index | (1<<FATLEAF_SHIFT0);      }

    __forceinline operator const gpu::AABB3f &() const { return bounds; }

    friend __forceinline embree_ostream operator<<(embree_ostream cout, const BVH2Ploc &n)
    {
      cout << "left " << n.leftIndex() << " right " << n.rightIndex() << " left " << n.left << " right " << n.right << " AABB3f { ";
      cout << "  lower = (" << n.bounds.lower_x << ", " << n.bounds.lower_y << ", " << n.bounds.lower_z << ") ";
      cout << "  upper = (" << n.bounds.upper_x << ", " << n.bounds.upper_y << ", " << n.bounds.upper_z << ") ";
      return cout << "}";
    }    
      
  };


  struct __aligned(64) InstancePrimitive
  {
    unsigned int shaderIndex_geomMask; // : 24 shader index used to calculate instancing shader in case of software instancing
                                   // : 8; geometry mask used for ray masking
      
    unsigned int instanceContributionToHitGroupIndex; // : 24;

    // unsigned int type : 1;          // enables/disables opaque culling
    // unsigned int geomFlags : 2;     // unused for instances
      
    uint64_t startNodePtr_instFlags; // : 48;  start node where to continue traversal of the instanced object
      
    Vec3f world2obj_vx;              // 1st column of Worl2Obj transform
    Vec3f world2obj_vy;              // 2nd column of Worl2Obj transform
    Vec3f world2obj_vz;              // 3rd column of Worl2Obj transform
    Vec3f obj2world_p;               // translation of Obj2World transform (on purpose in first 64 bytes)

    uint64_t bvhPtr;                 // pointer to BVH where start node belongs too
      
    unsigned int instanceID;             // user defined value per DXR spec
    unsigned int instanceIndex;          // geometry index of the instance (n'th geometry in scene)
      
    Vec3f obj2world_vx;              // 1st column of Obj2World transform
    Vec3f obj2world_vy;              // 2nd column of Obj2World transform
    Vec3f obj2world_vz;              // 3rd column of Obj2World transform
    Vec3f world2obj_p;               // translation of World2Obj transform

    __forceinline InstancePrimitive() {}
    __forceinline InstancePrimitive(AffineSpace3f obj2world, uint64_t startNodePtr, unsigned int instID, unsigned int geometry_index, uint8_t instMask)
    {
      shaderIndex_geomMask = instMask << 24; 
      instanceContributionToHitGroupIndex = ((unsigned int)PrimLeafDesc::TYPE_OPACITY_CULLING_ENABLED << 29) | ((unsigned int)GeometryFlags::NONE << 30);
    
      startNodePtr_instFlags = startNodePtr;
      
      instanceID = instID;
      instanceIndex = geometry_index;
      bvhPtr = (uint64_t) 0;

      obj2world_vx = obj2world.l.vx;
      obj2world_vy = obj2world.l.vy;
      obj2world_vz = obj2world.l.vz;
      obj2world_p = obj2world.p;
      
      const AffineSpace3f world2obj = rcp(obj2world);
      world2obj_vx = world2obj.l.vx;
      world2obj_vy = world2obj.l.vy;
      world2obj_vz = world2obj.l.vz;
      world2obj_p = world2obj.p;      
    }
  };
  
  static_assert(sizeof(InstancePrimitive) == 128, "InstanceLeaf must be 128 bytes large");
 
  // ===================================================================================================================================================================================
  // ============================================================================== Quadifier ==========================================================================================
  // ===================================================================================================================================================================================


  __forceinline const _ze_raytracing_triangle_indices_uint32_ext_t &getTriangleDesc(const  _ze_raytracing_geometry_triangles_ext_desc_t& mesh, const unsigned int triID)
  {
    return *(_ze_raytracing_triangle_indices_uint32_ext_t*)((char*)mesh.triangleBuffer + mesh.triangleStride * uint64_t(triID));    
  }

  __forceinline const _ze_raytracing_quad_indices_uint32_ext_t &getQuadDesc(const _ze_raytracing_geometry_quads_ext_desc_t& mesh, const unsigned int triID)
  {
    return *(_ze_raytracing_quad_indices_uint32_ext_t*)((char*)mesh.quadBuffer + mesh.quadStride * uint64_t(triID));    
  }
    
  __forceinline Vec3f getVec3f(const  _ze_raytracing_geometry_triangles_ext_desc_t& mesh, const unsigned int vtxID)
  {
    return *(Vec3f*)((char*)mesh.vertexBuffer + mesh.vertexStride * uint64_t(vtxID));
    
  }

  __forceinline Vec3f getVec3f(const _ze_raytracing_geometry_quads_ext_desc_t& mesh, const unsigned int vtxID)
  {
    return *(Vec3f*)((char*)mesh.vertexBuffer + mesh.vertexStride * uint64_t(vtxID));
    
  }
    
  __forceinline bool isValidTriangle(const  _ze_raytracing_geometry_triangles_ext_desc_t& mesh, const unsigned int i, uint3 &indices, gpu::AABB3f &bounds)
  {
    const _ze_raytracing_triangle_indices_uint32_ext_t &tri = getTriangleDesc(mesh,i);
    const unsigned int numVertices = mesh.vertexCount;
    indices.x() = tri.v0;
    indices.y() = tri.v1;
    indices.z() = tri.v2;    
    if (max(tri.v0,max(tri.v1,tri.v2)) >= numVertices) return false;
    const Vec3fa v0 = getVec3f(mesh,tri.v0);
    const Vec3fa v1 = getVec3f(mesh,tri.v1);
    const Vec3fa v2 = getVec3f(mesh,tri.v2);
    const float max_v0 = max(fabsf(v0.x),fabsf(v0.y),fabsf(v0.z));
    const float max_v1 = max(fabsf(v1.x),fabsf(v1.y),fabsf(v1.z));
    const float max_v2 = max(fabsf(v2.x),fabsf(v2.y),fabsf(v2.z));    
    const static float FLT_LARGE = 1.844E18f;
    const float max_value = max(max_v0,max(max_v1,max_v2));
    if (max_value >= FLT_LARGE || !sycl::isfinite(max_value)) return false;
    float3 vtx0(v0.x,v0.y,v0.z);
    float3 vtx1(v1.x,v1.y,v1.z);
    float3 vtx2(v2.x,v2.y,v2.z);

    bounds.extend(vtx0);
    bounds.extend(vtx1);
    bounds.extend(vtx2);    
    return true;
  }
  
  __forceinline bool isValidQuad(const _ze_raytracing_geometry_quads_ext_desc_t& mesh, const unsigned int i, uint4 &indices, gpu::AABB3f &bounds)
  {
    const _ze_raytracing_quad_indices_uint32_ext_t &quad = getQuadDesc(mesh,i);
    const unsigned int numVertices = mesh.vertexCount;
    indices.x() = quad.v0;
    indices.y() = quad.v1;
    indices.z() = quad.v2;
    indices.w() = quad.v3;    
    if (max(max(quad.v0,quad.v1),max(quad.v2,quad.v3)) >= numVertices) return false;
    
    const Vec3fa v0 = getVec3f(mesh,quad.v0);
    const Vec3fa v1 = getVec3f(mesh,quad.v1);
    const Vec3fa v2 = getVec3f(mesh,quad.v2);
    const Vec3fa v3 = getVec3f(mesh,quad.v3);
    
    const float max_v0 = max(fabsf(v0.x),fabsf(v0.y),fabsf(v0.z));
    const float max_v1 = max(fabsf(v1.x),fabsf(v1.y),fabsf(v1.z));
    const float max_v2 = max(fabsf(v2.x),fabsf(v2.y),fabsf(v2.z));
    const float max_v3 = max(fabsf(v3.x),fabsf(v3.y),fabsf(v3.z));    
    
    const static float FLT_LARGE = 1.844E18f;
    const float max_value = max(max(max_v0,max_v1),max(max_v2,max_v3));
    if (max_value >= FLT_LARGE && !sycl::isfinite(max_value)) return false;
    float3 vtx0(v0.x,v0.y,v0.z);
    float3 vtx1(v1.x,v1.y,v1.z);
    float3 vtx2(v2.x,v2.y,v2.z);
    float3 vtx3(v3.x,v3.y,v3.z);    
    bounds.extend(vtx0);
    bounds.extend(vtx1);
    bounds.extend(vtx2);    
    bounds.extend(vtx3);
    return true;
  }

  __forceinline unsigned int try_pair_triangles(const uint3 &a, const uint3 &b, unsigned int& lb0, unsigned int& lb1, unsigned int &lb2)    
  {
    lb0 = 3;
    lb1 = 3;
    lb2 = 3;
    
    lb0 = ( b.x() == a.x() ) ? 0 : lb0;
    lb1 = ( b.y() == a.x() ) ? 0 : lb1;
    lb2 = ( b.z() == a.x() ) ? 0 : lb2;
    
    lb0 = ( b.x() == a.y() ) ? 1 : lb0;
    lb1 = ( b.y() == a.y() ) ? 1 : lb1;
    lb2 = ( b.z() == a.y() ) ? 1 : lb2;
    
    lb0 = ( b.x() == a.z() ) ? 2 : lb0;
    lb1 = ( b.y() == a.z() ) ? 2 : lb1;
    lb2 = ( b.z() == a.z() ) ? 2 : lb2;    
    if ((lb0 == 3) + (lb1 == 3) + (lb2 == 3) <= 1)
    {
      unsigned int p3_index = 0;
      p3_index = (lb1 == 3) ? 1 : p3_index;
      p3_index = (lb2 == 3) ? 2 : p3_index;
      return p3_index;
    }
    else
      return -1;
  }

  __forceinline bool try_pair_triangles(const uint3 &a, const uint3 &b)    
  {
    unsigned int lb0,lb1,lb2;
    lb0 = 3;
    lb1 = 3;
    lb2 = 3;
    
    lb0 = ( b.x() == a.x() ) ? 0 : lb0;
    lb1 = ( b.y() == a.x() ) ? 0 : lb1;
    lb2 = ( b.z() == a.x() ) ? 0 : lb2;
    
    lb0 = ( b.x() == a.y() ) ? 1 : lb0;
    lb1 = ( b.y() == a.y() ) ? 1 : lb1;
    lb2 = ( b.z() == a.y() ) ? 1 : lb2;
    
    lb0 = ( b.x() == a.z() ) ? 2 : lb0;
    lb1 = ( b.y() == a.z() ) ? 2 : lb1;
    lb2 = ( b.z() == a.z() ) ? 2 : lb2;    
    if ((lb0 == 3) + (lb1 == 3) + (lb2 == 3) <= 1)
      return true;
    else
      return false;
  }

  __forceinline unsigned int getBlockQuadificationCount(const  _ze_raytracing_geometry_triangles_ext_desc_t* const triMesh, const unsigned int localID, const unsigned int startPrimID, const unsigned int endPrimID)
  {
    const unsigned int subgroupLocalID = get_sub_group_local_id();
    const unsigned int subgroupSize    = get_sub_group_size();            
    const unsigned int ID           = (startPrimID + localID) < endPrimID ? startPrimID + localID : -1;
    uint3 tri_indices;
    gpu::AABB3f tmp_bounds;
    bool valid = ID < endPrimID ? isValidTriangle(*triMesh,ID,tri_indices,tmp_bounds) : false;
    bool paired = false;
    unsigned int numQuads = 0;
    unsigned int active_mask = sub_group_ballot(valid);

    while(active_mask)
    {
      active_mask = sub_group_broadcast(active_mask,0);
                                                                              
      const unsigned int broadcast_lane = sycl::ctz(active_mask);
      if (subgroupLocalID == broadcast_lane) valid = false;

      active_mask &= active_mask-1;
                                                                              
      const bool broadcast_paired = sub_group_broadcast(paired, broadcast_lane);
      const unsigned int broadcast_ID     = sub_group_broadcast(ID    , broadcast_lane);

      if (!broadcast_paired)
      {
        const uint3 tri_indices_broadcast(sub_group_broadcast(tri_indices.x(),broadcast_lane),
                                          sub_group_broadcast(tri_indices.y(),broadcast_lane),
                                          sub_group_broadcast(tri_indices.z(),broadcast_lane));
        bool pairable = false;
        if (ID != broadcast_ID && !paired && valid)
          pairable = try_pair_triangles(tri_indices_broadcast,tri_indices);
                                                                            
        const unsigned int first_paired_lane = sycl::ctz(sub_group_ballot(pairable));
        if (first_paired_lane < subgroupSize)
        {
          active_mask &= ~((unsigned int)1 << first_paired_lane);
          if (subgroupLocalID == first_paired_lane) { valid = false; }
        }
      }
      numQuads++;
    }
    return numQuads;
  }

  __forceinline unsigned int getMergedQuadBounds(const  _ze_raytracing_geometry_triangles_ext_desc_t* const triMesh, const unsigned int ID, const unsigned int endPrimID, gpu::AABB3f &bounds)
  {
    const unsigned int subgroupLocalID = get_sub_group_local_id();
    const unsigned int subgroupSize    = get_sub_group_size();        
    
    uint3 tri_indices;
    bool valid = ID < endPrimID ? isValidTriangle(*triMesh,ID,tri_indices,bounds) : false;
    bool paired = false;
    unsigned int paired_ID = -1;
    unsigned int active_mask = sub_group_ballot(valid);

    while(active_mask)
    {
      active_mask = sub_group_broadcast(active_mask,0);
                                                                              
      const unsigned int broadcast_lane = sycl::ctz(active_mask);

      if (subgroupLocalID == broadcast_lane) valid = false;
                                                                            
      active_mask &= active_mask-1;
                                                                              
      const bool broadcast_paired = sub_group_broadcast(paired, broadcast_lane);
      const unsigned int broadcast_ID     = sub_group_broadcast(ID    , broadcast_lane);

      if (!broadcast_paired)
      {
        const uint3 tri_indices_broadcast(sub_group_broadcast(tri_indices.x(),broadcast_lane),
                                          sub_group_broadcast(tri_indices.y(),broadcast_lane),
                                          sub_group_broadcast(tri_indices.z(),broadcast_lane));
        bool pairable = false;
        if (ID != broadcast_ID && !paired && valid)
          pairable = try_pair_triangles(tri_indices_broadcast,tri_indices);
                                                                            
        const unsigned int first_paired_lane = sycl::ctz(sub_group_ballot(pairable));
        if (first_paired_lane < subgroupSize)
        {
          active_mask &= ~((unsigned int)1 << first_paired_lane);
          if (subgroupLocalID == first_paired_lane) { valid = false; }
          const unsigned int secondID = sub_group_broadcast(ID,first_paired_lane);
          gpu::AABB3f second_bounds = bounds.sub_group_broadcast(first_paired_lane);
          if (subgroupLocalID == broadcast_lane)  {
            paired_ID = secondID;
            bounds.extend(second_bounds);
          }
        }
        else
          if (subgroupLocalID == broadcast_lane)
            paired_ID = ID;
                                                                                
      }
    }
    return paired_ID;
  }  
  
  // ===================================================================================================================================================================================
  // ============================================================================== Instances ==========================================================================================
  // ===================================================================================================================================================================================

  __forceinline AffineSpace3fa getTransform(const _ze_raytracing_geometry_instance_ext_desc_t* geom)
  {
    switch (geom->transformFormat)
    {
    case ZE_RAYTRACING_FORMAT_EXT_FLOAT3X4_COLUMN_MAJOR: {
      const ze_raytracing_transform_float3x4_column_major_ext_t* xfm = (const ze_raytracing_transform_float3x4_column_major_ext_t*) geom->transform;
      return {
        { xfm->vx_x, xfm->vx_y, xfm->vx_z },
        { xfm->vy_x, xfm->vy_y, xfm->vy_z },
        { xfm->vz_x, xfm->vz_y, xfm->vz_z },
        { xfm-> p_x, xfm-> p_y, xfm-> p_z }
      };
    }
    case ZE_RAYTRACING_FORMAT_EXT_FLOAT3X4_ALIGNED_COLUMN_MAJOR: {
      const ze_raytracing_transform_float3x4_aligned_column_major_ext_t* xfm = (const ze_raytracing_transform_float3x4_aligned_column_major_ext_t*) geom->transform;
      return {
        { xfm->vx_x, xfm->vx_y, xfm->vx_z },
        { xfm->vy_x, xfm->vy_y, xfm->vy_z },
        { xfm->vz_x, xfm->vz_y, xfm->vz_z },
        { xfm-> p_x, xfm-> p_y, xfm-> p_z }
      };
    }
    case ZE_RAYTRACING_FORMAT_EXT_FLOAT3X4_ROW_MAJOR: {
      const ze_raytracing_transform_float3x4_row_major_ext_t* xfm = (const ze_raytracing_transform_float3x4_row_major_ext_t*) geom->transform;
      return {
        { xfm->vx_x, xfm->vx_y, xfm->vx_z },
        { xfm->vy_x, xfm->vy_y, xfm->vy_z },
        { xfm->vz_x, xfm->vz_y, xfm->vz_z },
        { xfm-> p_x, xfm-> p_y, xfm-> p_z }
      };
    }
    default:
      return AffineSpace3fa(); 
    }
  }  

  __forceinline gpu::AABB3f getInstanceBounds(const _ze_raytracing_geometry_instance_ext_desc_t &instance)
  {
    const Vec3fa lower(instance.bounds->lower.x,instance.bounds->lower.y,instance.bounds->lower.z);
    const Vec3fa upper(instance.bounds->upper.x,instance.bounds->upper.y,instance.bounds->upper.z);
    const BBox3fa org_bounds(lower,upper);
    const AffineSpace3fa local2world = getTransform(&instance);    
    const BBox3fa instance_bounds = xfmBounds(local2world,org_bounds);
    const gpu::AABB3f bounds(instance_bounds.lower.x,instance_bounds.lower.y,instance_bounds.lower.z,
                       instance_bounds.upper.x,instance_bounds.upper.y,instance_bounds.upper.z);       
    return bounds;
  }
  
  __forceinline bool isValidInstance(const _ze_raytracing_geometry_instance_ext_desc_t& instance, gpu::AABB3f &bounds)
  {
    if (instance.bounds == nullptr) return false;    
    bounds = getInstanceBounds(instance);
    if (bounds.empty()) return false;
    if (!bounds.checkNumericalBounds()) return false;    
    return true;
  }


  // =====================================================================================================================================================================================
  // ============================================================================== Prefix Sums ==========================================================================================
  // =====================================================================================================================================================================================

  __forceinline void clearScratchMem(sycl::queue &gpu_queue, unsigned int *scratch_mem, const unsigned int value, const unsigned int numEntries, double &iteration_time, const bool verbose)
   {
     const unsigned int wgSize = 256;
     const sycl::nd_range<1> nd_range1(gpu::alignTo(numEntries,wgSize),sycl::range<1>(wgSize)); 
     sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
         cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(32)
                          {
                            const unsigned int globalID         = item.get_global_id(0);
                            if (globalID < numEntries)
                              scratch_mem[globalID] = value;
                          });                                                         
       });
     gpu::waitOnEventAndCatchException(queue_event);
     if (unlikely(verbose)) iteration_time += gpu::getDeviceExecutionTiming(queue_event); 
   }

  __forceinline unsigned int prefixSumWorkgroup(const unsigned int count, const unsigned int WG_SIZE, unsigned int *const counts, unsigned int *const counts_prefix_sum, const sycl::nd_item<1> &item, unsigned int &total_reduction)
  {
    const unsigned int subgroupID      = get_sub_group_id();
    const unsigned int subgroupSize    = get_sub_group_size();
    const unsigned int subgroupLocalID = get_sub_group_local_id();                                                        
    const unsigned int exclusive_scan  = sub_group_exclusive_scan(count, std::plus<unsigned int>());
    const unsigned int reduction       = sub_group_reduce(count, std::plus<unsigned int>());
    counts[subgroupID]         = reduction;
                             
    item.barrier(sycl::access::fence_space::local_space);

    /* -- prefix sum over reduced sub group counts -- */        
    total_reduction = 0;
    for (unsigned int j=subgroupLocalID;j<WG_SIZE/subgroupSize;j+=subgroupSize)
    {
      const unsigned int subgroup_counts = counts[j];
      const unsigned int sums_exclusive_scan = sub_group_exclusive_scan(subgroup_counts, std::plus<unsigned int>());
      const unsigned int reduction = sub_group_broadcast(subgroup_counts,subgroupSize-1) + sub_group_broadcast(sums_exclusive_scan,subgroupSize-1);
      counts_prefix_sum[j] = sums_exclusive_scan + total_reduction;
      total_reduction += reduction;
    }

    item.barrier(sycl::access::fence_space::local_space);

    const unsigned int sums_prefix_sum = counts_prefix_sum[subgroupID];                                                                 
    return sums_prefix_sum + exclusive_scan;
  }

  __forceinline unsigned int prefixSumWorkgroup(const unsigned int ps, const unsigned int WG_SIZE, unsigned int *const counts, const sycl::nd_item<1> &item)
  {
    const unsigned int subgroupID      = get_sub_group_id();
    const unsigned int subgroupSize    = get_sub_group_size();    
    const unsigned int exclusive_scan = sub_group_exclusive_scan(ps, std::plus<unsigned int>());
    const unsigned int reduction = sub_group_reduce(ps, std::plus<unsigned int>());
    counts[subgroupID] = reduction;
                                                                          
    item.barrier(sycl::access::fence_space::local_space);

    /* -- prefix sum over reduced sub group counts -- */        
    unsigned int p_sum = 0;
    for (unsigned int j=0;j<TRIANGLE_QUAD_BLOCK_SIZE/subgroupSize;j++)
      if (j<subgroupID) p_sum += counts[j];
                                                                          
    return p_sum + exclusive_scan;
  }

  
  
  sycl::event prefixSumOverCounts (sycl::queue &gpu_queue, sycl::event &input_event, const unsigned int numGeoms, unsigned int *const counts_per_geom_prefix_sum, unsigned int *host_device_tasks, const bool verbose) // FIXME: general, host_device_tasks
  {
    static const unsigned int GEOM_PREFIX_SUB_GROUP_WIDTH = 16;
    static const unsigned int GEOM_PREFIX_WG_SIZE  = LARGE_WG_SIZE;

    sycl::event queue_event =  gpu_queue.submit([&](sycl::handler &cgh) {
        sycl::local_accessor< unsigned int       , 1> counts(sycl::range<1>((GEOM_PREFIX_WG_SIZE/GEOM_PREFIX_SUB_GROUP_WIDTH)),cgh);
        sycl::local_accessor< unsigned int       , 1> counts_prefix_sum(sycl::range<1>((GEOM_PREFIX_WG_SIZE/GEOM_PREFIX_SUB_GROUP_WIDTH)),cgh);
        const sycl::nd_range<1> nd_range(GEOM_PREFIX_WG_SIZE,sycl::range<1>(GEOM_PREFIX_WG_SIZE));
        cgh.depends_on(input_event);
        cgh.parallel_for(nd_range,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(GEOM_PREFIX_SUB_GROUP_WIDTH) {
            const unsigned int localID        = item.get_local_id(0);
            const unsigned int localSize       = item.get_local_range().size();            
            const unsigned int aligned_numGeoms = gpu::alignTo(numGeoms,GEOM_PREFIX_WG_SIZE);

            unsigned int total_offset = 0;
            for (unsigned int t=localID;t<aligned_numGeoms;t+=localSize)
            {
              item.barrier(sycl::access::fence_space::local_space);
                                                          
              unsigned int count = 0;
              if (t < numGeoms)
                count = counts_per_geom_prefix_sum[t];

              unsigned int total_reduction = 0;
              const unsigned int p_sum = total_offset + prefixSumWorkgroup(count,GEOM_PREFIX_WG_SIZE,counts.get_pointer(),counts_prefix_sum.get_pointer(),item,total_reduction);
              total_offset += total_reduction;              

              if (t < numGeoms)
                counts_per_geom_prefix_sum[t] = p_sum;

            }
                                                        
            if (localID == 0)
            {
              counts_per_geom_prefix_sum[numGeoms] = total_offset;                                                          
              *host_device_tasks = total_offset;
            }
                                                        
          });
      });
    return queue_event;
  }

  // =====================================================================================================================================================================================
  // ========================================================================== Counting Primitives ======================================================================================
  // =====================================================================================================================================================================================
  
  struct __aligned(64) PrimitiveCounts {
    unsigned int numTriangles;
    unsigned int numQuads;
    unsigned int numProcedurals;
    unsigned int numInstances;
    unsigned int numMergedTrisQuads;
    unsigned int numQuadBlocks;

    __forceinline void reset()
    {
      numTriangles = numQuads = numProcedurals = numInstances = numMergedTrisQuads = numQuadBlocks = 0;
    }
    __forceinline PrimitiveCounts() 
    {
      reset();
    }
  };

  __forceinline PrimitiveCounts operator +(const PrimitiveCounts& a, const PrimitiveCounts& b) {
    PrimitiveCounts c;
    c.numTriangles       = a.numTriangles       + b.numTriangles;
    c.numQuads           = a.numQuads           + b.numQuads;
    c.numProcedurals     = a.numProcedurals     + b.numProcedurals;
    c.numInstances       = a.numInstances       + b.numInstances;
    c.numMergedTrisQuads = a.numMergedTrisQuads + b.numMergedTrisQuads;    
    c.numQuadBlocks      = a.numQuadBlocks      + b.numQuadBlocks;
    return c;
  }

  __forceinline bool operator ==(const PrimitiveCounts& a, const PrimitiveCounts& b) {
    if (a.numTriangles       == b.numTriangles &&
        a.numQuads           == b.numQuads &&
        a.numProcedurals     == b.numProcedurals &&
        a.numInstances       == b.numInstances &&
        a.numMergedTrisQuads == b.numMergedTrisQuads &&
        a.numQuadBlocks      == b.numQuadBlocks)
      return true;
    return false;
  }
    
  __forceinline bool operator !=(const PrimitiveCounts& a, const PrimitiveCounts& b) {
    return !(a==b);
  }
    
  __forceinline unsigned int find_geomID_from_blockID(const unsigned int *const prefix_sum, const int numItems, const int blockID)
  {
    int l = 0;
    int r = numItems-1;
    while (r-l>1)
    {
      const int m = (l + r) / 2;
      if ( prefix_sum[m] > blockID)
        r = m;
      else if (prefix_sum[m] < blockID)
        l = m;
      else
      {
        if (m == numItems-1 || prefix_sum[m] != prefix_sum[m+1]) return m;
        l = m + 1;
       }
    }
    const int final = prefix_sum[r] <= blockID ? r : l;
    return final;
  }
  

  PrimitiveCounts countPrimitives(sycl::queue &gpu_queue, const _ze_raytracing_geometry_ext_desc_t **const geometries, const unsigned int numGeometries, PLOCGlobals *const globals, unsigned int *blocksPerGeom, unsigned int *host_device_tasks, double &iteration_time, const bool verbose)    
  {
    PrimitiveCounts count;
    const unsigned int wgSize = LARGE_WG_SIZE;
    const sycl::nd_range<1> nd_range1(gpu::alignTo(numGeometries,wgSize),sycl::range<1>(wgSize));          
    sycl::event count_event = gpu_queue.submit([&](sycl::handler &cgh) {
        sycl::local_accessor< unsigned int, 0> _numTriangles(cgh);
        sycl::local_accessor< unsigned int, 0> _numQuads(cgh);
        sycl::local_accessor< unsigned int, 0> _numProcedurals(cgh);
        sycl::local_accessor< unsigned int, 0> _numInstances(cgh);
        sycl::local_accessor< unsigned int, 0> _numQuadBlocks(cgh);        
        cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(16)
                         {
                           const unsigned int geomID    = item.get_global_id(0);
                           const unsigned int localID   = item.get_local_id(0);
                           
                           unsigned int &numTriangles   = *_numTriangles.get_pointer();
                           unsigned int &numQuads       = *_numQuads.get_pointer();
                           unsigned int &numProcedurals = *_numProcedurals.get_pointer();
                           unsigned int &numInstances   = *_numInstances.get_pointer();
                           unsigned int &numQuadBlocks  = *_numQuadBlocks.get_pointer();
                           if (localID == 0)
                           {
                             numTriangles   = 0;
                             numQuads       = 0;
                             numProcedurals = 0;
                             numInstances   = 0;
                             numQuadBlocks  = 0;
                           }
                           item.barrier(sycl::access::fence_space::local_space);

                           if (geomID < numGeometries)
                           {
                             unsigned int numBlocks = 0;                             
                             const _ze_raytracing_geometry_ext_desc_t* geom = geometries[geomID];
                             if (geom != nullptr) {
                               switch (geom->geometryType)
                               {
                               case ZE_RAYTRACING_GEOMETRY_TYPE_EXT_TRIANGLES  :
                               {
                                 gpu::atomic_add_local(&numTriangles  , (( _ze_raytracing_geometry_triangles_ext_desc_t *)geom)->triangleCount);
                                 gpu::atomic_add_local(&numQuadBlocks ,((( _ze_raytracing_geometry_triangles_ext_desc_t *)geom)->triangleCount+TRIANGLE_QUAD_BLOCK_SIZE-1)/TRIANGLE_QUAD_BLOCK_SIZE);
                                 numBlocks += ((( _ze_raytracing_geometry_triangles_ext_desc_t *)geom)->triangleCount+TRIANGLE_QUAD_BLOCK_SIZE-1)/TRIANGLE_QUAD_BLOCK_SIZE;
                                 break;
                               }
                               case ZE_RAYTRACING_GEOMETRY_TYPE_EXT_QUADS      :
                               {
                                 gpu::atomic_add_local(&numQuads      , ((_ze_raytracing_geometry_quads_ext_desc_t     *)geom)->quadCount);
                                 gpu::atomic_add_local(&numQuadBlocks ,(((_ze_raytracing_geometry_quads_ext_desc_t *)geom)->quadCount+TRIANGLE_QUAD_BLOCK_SIZE-1)/TRIANGLE_QUAD_BLOCK_SIZE);
                                 numBlocks += (((_ze_raytracing_geometry_quads_ext_desc_t *)geom)->quadCount+TRIANGLE_QUAD_BLOCK_SIZE-1)/TRIANGLE_QUAD_BLOCK_SIZE;
                                 break;
                               }
                               case ZE_RAYTRACING_GEOMETRY_TYPE_EXT_AABBS_FPTR :  gpu::atomic_add_local(&numProcedurals,((_ze_raytracing_geometry_aabbs_fptr_ext_desc_t*)geom)->primCount);     break;
                               case ZE_RAYTRACING_GEOMETRY_TYPE_EXT_INSTANCE   :  gpu::atomic_add_local(&numInstances  ,(unsigned int)1); break;
                               };
                             }
                             blocksPerGeom[geomID] = numBlocks;
                           }
                           
                           item.barrier(sycl::access::fence_space::local_space);
                           if (localID == 0)
                           {
                             gpu::atomic_add_global(&globals->numTriangles,numTriangles);
                             gpu::atomic_add_global(&globals->numQuads,numQuads);
                             gpu::atomic_add_global(&globals->numProcedurals,numProcedurals);
                             gpu::atomic_add_global(&globals->numInstances,numInstances);
                             gpu::atomic_add_global(&globals->numQuadBlocks,numQuadBlocks);                             
                           }
                         });
		  
      });    
    sycl::event copy_event =  gpu_queue.submit([&](sycl::handler &cgh) {
        cgh.depends_on(count_event);
        cgh.single_task([=]() {
            host_device_tasks[0] = globals->numTriangles;
            host_device_tasks[1] = globals->numQuads;
            host_device_tasks[2] = globals->numProcedurals;
            host_device_tasks[3] = globals->numInstances;
            host_device_tasks[4] = globals->numQuadBlocks;
          });
      });
    gpu::waitOnEventAndCatchException(copy_event);

    if (unlikely(verbose))
    {
      iteration_time += gpu::getDeviceExecutionTiming(count_event);    
      iteration_time += gpu::getDeviceExecutionTiming(copy_event);
    }
    
    count.numTriangles   = host_device_tasks[0];
    count.numQuads       = host_device_tasks[1];
    count.numProcedurals = host_device_tasks[2];
    count.numInstances   = host_device_tasks[3];
    count.numQuadBlocks  = host_device_tasks[4];

    return count;
  }  


  unsigned int countQuadsPerGeometryUsingBlocks(sycl::queue &gpu_queue, PLOCGlobals *const globals, const _ze_raytracing_geometry_ext_desc_t **const geometries, const unsigned int numGeoms, const unsigned int numQuadBlocks, unsigned int *const blocksPerGeom, unsigned int *const quadsPerBlock, unsigned int *const host_device_tasks, double &iteration_time, const bool verbose)    
  {
    sycl::event initial;
    sycl::event prefix_sum_blocks_event = prefixSumOverCounts(gpu_queue,initial,numGeoms,blocksPerGeom,host_device_tasks,verbose);     

    // ================================================================================================================

    const sycl::nd_range<1> nd_range2(numQuadBlocks*TRIANGLE_QUAD_BLOCK_SIZE,sycl::range<1>(TRIANGLE_QUAD_BLOCK_SIZE));
    sycl::event count_quads_event = gpu_queue.submit([&](sycl::handler &cgh) {
        sycl::local_accessor< unsigned int      ,  0> _active_counter(cgh);
        sycl::local_accessor< unsigned int      ,  0> _geomID(cgh);
        cgh.depends_on(prefix_sum_blocks_event);
        cgh.parallel_for(nd_range2,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(16)      
                         {
                           const unsigned int localID         = item.get_local_id(0);                                                                      
                           const unsigned int subgroupLocalID = get_sub_group_local_id();
                           const unsigned int globalBlockID   = item.get_group(0);
                           unsigned int &active_counter       = *_active_counter.get_pointer();
                           unsigned int &geomID               = *_geomID.get_pointer();
                           active_counter = 0;
                           if (localID == 0)
                             geomID = find_geomID_from_blockID(blocksPerGeom,numGeoms,globalBlockID);
                           
                           item.barrier(sycl::access::fence_space::local_space);
                           const unsigned int blockID        = globalBlockID - blocksPerGeom[geomID];
                           const _ze_raytracing_geometry_ext_desc_t *const geometryDesc = geometries[geomID];
                           
                           // ====================                           
                           // === TriangleMesh ===
                           // ====================
                           
                           if (geometryDesc->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_TRIANGLES)
                           {
                              _ze_raytracing_geometry_triangles_ext_desc_t* triMesh = ( _ze_raytracing_geometry_triangles_ext_desc_t*)geometryDesc;                             
                             const unsigned int numTriangles   = triMesh->triangleCount;
                             {                                                                          
                               const unsigned int startPrimID = blockID*TRIANGLE_QUAD_BLOCK_SIZE;
                               const unsigned int endPrimID   = min(startPrimID+TRIANGLE_QUAD_BLOCK_SIZE,numTriangles);
                               const unsigned int numQuads    = getBlockQuadificationCount(triMesh,localID,startPrimID,endPrimID);
                               if (subgroupLocalID == 0)                                 
                                 gpu::atomic_add_local(&active_counter,numQuads);
                             }
                           }
                           // ================                           
                           // === QuadMesh ===
                           // ================
                           else if (geometryDesc->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_QUADS)
                           {
                             _ze_raytracing_geometry_quads_ext_desc_t* quadMesh = (_ze_raytracing_geometry_quads_ext_desc_t*)geometryDesc;                                                          
                             const unsigned int numQuads  = quadMesh->quadCount;
                             {                                                                          
                               const unsigned int startPrimID  = blockID*TRIANGLE_QUAD_BLOCK_SIZE;
                               const unsigned int endPrimID    = min(startPrimID+TRIANGLE_QUAD_BLOCK_SIZE,numQuads);
                               const unsigned int ID           = (startPrimID + localID) < endPrimID ? startPrimID + localID : -1;
                               uint4 quad_indices;
                               gpu::AABB3f tmp_bounds;
                               bool valid = ID < endPrimID ? isValidQuad(*quadMesh,ID,quad_indices,tmp_bounds) : false;
                               unsigned int active_mask = sub_group_ballot(valid);
                               unsigned int numQuads = sycl::popcount(active_mask);
                               if (subgroupLocalID == 0)
                                 gpu::atomic_add_local(&active_counter,numQuads);
                             }
                           }
                             
                           item.barrier(sycl::access::fence_space::local_space);
                                                                        
                           if (localID == 0)
                           {
                             gpu::atomic_add_global(&globals->numMergedTrisQuads,active_counter);
                             if (quadsPerBlock)
                               quadsPerBlock[globalBlockID] = active_counter;
                           }
                           
                         });
		  
      });

    // ================================================================================================================
    
    sycl::event copy_event =  gpu_queue.submit([&](sycl::handler &cgh) {
        cgh.depends_on(count_quads_event);
        cgh.single_task([=]() {
            host_device_tasks[0] = globals->numMergedTrisQuads;
          });
         
      });

    // ================================================================================================================
    
    if (quadsPerBlock)
    {
      sycl::event prefix_sum_quads_per_block_event = prefixSumOverCounts(gpu_queue,copy_event,numQuadBlocks,quadsPerBlock,host_device_tasks,verbose);    
      gpu::waitOnEventAndCatchException(prefix_sum_quads_per_block_event);
      if (unlikely(verbose))
        iteration_time += gpu::getDeviceExecutionTiming(prefix_sum_quads_per_block_event);
    }
    else
      gpu::waitOnEventAndCatchException(copy_event);

    if (unlikely(verbose))
    {
      iteration_time += gpu::getDeviceExecutionTiming(prefix_sum_blocks_event);             
      iteration_time += gpu::getDeviceExecutionTiming(count_quads_event);        
      iteration_time += gpu::getDeviceExecutionTiming(copy_event);
    }

    const unsigned int numMergedTrisQuads = host_device_tasks[0];
    return numMergedTrisQuads;
  }
   
   PrimitiveCounts getEstimatedPrimitiveCounts(sycl::queue &gpu_queue, const _ze_raytracing_geometry_ext_desc_t **const geometries, const unsigned int numGeoms, const bool verbose)    
  {
    if (numGeoms == 0) return PrimitiveCounts();

    const bool verbose2 = verbose;
    unsigned int *host_device_tasks = (unsigned int*)sycl::aligned_alloc(64,HOST_DEVICE_COMM_BUFFER_SIZE,gpu_queue.get_device(),gpu_queue.get_context(),sycl::usm::alloc::host);    
    char *scratch           = (char*)sycl::aligned_alloc(64,sizeof(PrimitiveCounts)+sizeof(PLOCGlobals)+numGeoms*sizeof(unsigned int),gpu_queue.get_device(),gpu_queue.get_context(),sycl::usm::alloc::device);
    
    PLOCGlobals *globals = (PLOCGlobals*)scratch;   
    unsigned int  *blocksPerGeom = (unsigned int*)(scratch + sizeof(PLOCGlobals));

    sycl::event queue_event =  gpu_queue.submit([&](sycl::handler &cgh) {
        cgh.single_task([=]() {
            globals->reset();
          });
      });
    gpu::waitOnEventAndCatchException(queue_event);
    
    double device_prim_counts_time;
    PrimitiveCounts primCounts = countPrimitives(gpu_queue,geometries,numGeoms,globals,blocksPerGeom,host_device_tasks,device_prim_counts_time,verbose2); 
    const unsigned int numQuadBlocks = primCounts.numQuadBlocks;
    if (numQuadBlocks)
    {
      // === get accurate quadification count ===
      double device_quadification_time = 0.0;    
      primCounts.numMergedTrisQuads = countQuadsPerGeometryUsingBlocks(gpu_queue,globals,geometries,numGeoms,numQuadBlocks,blocksPerGeom,nullptr,host_device_tasks,device_quadification_time,verbose2);
    }

    sycl::free(scratch          ,gpu_queue.get_context());
    sycl::free(host_device_tasks,gpu_queue.get_context());
    
    return primCounts;
  }
   

  // =========================================================================================================================================================================================
  // ============================================================================== Create Primrefs ==========================================================================================
  // =========================================================================================================================================================================================

   void createQuads_initPLOCPrimRefs(sycl::queue &gpu_queue, PLOCGlobals *const globals, const _ze_raytracing_geometry_ext_desc_t **const geometries, const unsigned int numGeoms, const unsigned int numQuadBlocks, const unsigned int *const scratch, BVH2Ploc *const bvh2, const unsigned int prim_type_offset, double &iteration_time, const bool verbose)    
  {
    const unsigned int *const blocksPerGeom          = scratch;
    const unsigned int *const quadsPerBlockPrefixSum = scratch + numGeoms;
    static const unsigned int MERGE_TRIANGLES_TO_QUADS_SUB_GROUP_WIDTH = 16;


    
    const sycl::nd_range<1> nd_range1(numQuadBlocks*TRIANGLE_QUAD_BLOCK_SIZE,sycl::range<1>(TRIANGLE_QUAD_BLOCK_SIZE));
    sycl::event quadification_event = gpu_queue.submit([&](sycl::handler &cgh) {
        sycl::local_accessor< unsigned int      , 0> _active_counter(cgh);
        sycl::local_accessor< unsigned int      , 0> _geomID(cgh);                
        sycl::local_accessor< unsigned int      , 1> counts(sycl::range<1>((TRIANGLE_QUAD_BLOCK_SIZE/MERGE_TRIANGLES_TO_QUADS_SUB_GROUP_WIDTH)),cgh);       
        cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(MERGE_TRIANGLES_TO_QUADS_SUB_GROUP_WIDTH)      
                         {
                           const unsigned int localID         = item.get_local_id(0);
                           const unsigned int globalBlockID   = item.get_group(0);
                           unsigned int &active_counter       = *_active_counter.get_pointer();
                           unsigned int &geomID               = *_geomID.get_pointer();
                           active_counter = 0;
                           if (localID == 0)
                             geomID = find_geomID_from_blockID(blocksPerGeom,numGeoms,globalBlockID);
                           
                           item.barrier(sycl::access::fence_space::local_space);
                           const unsigned int blockID        = globalBlockID - blocksPerGeom[geomID];
                           const unsigned int startQuadOffset = quadsPerBlockPrefixSum[globalBlockID] + prim_type_offset;
                           const _ze_raytracing_geometry_ext_desc_t *const geometryDesc = geometries[geomID];
                                                      
                           // ====================                           
                           // === TriangleMesh ===
                           // ====================
                           if (geometryDesc->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_TRIANGLES)
                           {
                              _ze_raytracing_geometry_triangles_ext_desc_t* triMesh = ( _ze_raytracing_geometry_triangles_ext_desc_t*)geometryDesc;                             
                             const unsigned int numTriangles = triMesh->triangleCount;                                                                                                     
                             {                                                                          
                               const unsigned int startPrimID  = blockID*TRIANGLE_QUAD_BLOCK_SIZE;
                               const unsigned int endPrimID    = min(startPrimID+TRIANGLE_QUAD_BLOCK_SIZE,numTriangles);
                               const unsigned int ID           = (startPrimID + localID) < endPrimID ? startPrimID + localID : -1;
                               {
                                 gpu::AABB3f bounds;
                                 bounds.init();                                 
                                 const unsigned int paired_ID = getMergedQuadBounds(triMesh,ID,endPrimID,bounds);                                                                          
                                 const unsigned int flag = paired_ID != -1 ? 1 : 0;
                                 const unsigned int ps = ID < endPrimID ? flag : 0;
                                 const unsigned int dest_offset = startQuadOffset + prefixSumWorkgroup(ps,TRIANGLE_QUAD_BLOCK_SIZE,counts.get_pointer(),item);

                                 /* --- store cluster representative into destination array --- */
                                 if (ID < endPrimID)
                                   if (paired_ID != -1)
                                   {
                                     const unsigned int pair_offset = paired_ID - ID;
                                     const unsigned int pair_geomID = (pair_offset << PAIR_OFFSET_SHIFT) | geomID;
                                     BVH2Ploc node;
                                     node.initLeaf(pair_geomID,ID,bounds); // need to consider pair_offset
                                     node.store(&bvh2[dest_offset]);
                                   }                                                                          
                               }
                             }                                                                        
                           }
                           // ================                           
                           // === QuadMesh ===
                           // ================                           
                           else if (geometryDesc->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_QUADS)
                           {
                             _ze_raytracing_geometry_quads_ext_desc_t* quadMesh = (_ze_raytracing_geometry_quads_ext_desc_t*)geometryDesc;                                                          
                             const unsigned int numQuads  = quadMesh->quadCount;                             
                             {                                                                          
                               const unsigned int startPrimID  = blockID*TRIANGLE_QUAD_BLOCK_SIZE;
                               const unsigned int endPrimID    = min(startPrimID+TRIANGLE_QUAD_BLOCK_SIZE,numQuads);
                               const unsigned int ID           = (startPrimID + localID) < endPrimID ? startPrimID + localID : -1;
                               {
                                 uint4 quad_indices;
                                 gpu::AABB3f bounds;
                                 bounds.init();
                                 const bool valid = ID < endPrimID ? isValidQuad(*quadMesh,ID,quad_indices,bounds) : false;                                                                          
                                 const unsigned int ps = valid ? 1 : 0;
                                 const unsigned int dest_offset = startQuadOffset + prefixSumWorkgroup(ps,TRIANGLE_QUAD_BLOCK_SIZE,counts.get_pointer(),item);

                                 /* --- store cluster representative into destination array --- */
                                 if (ID < endPrimID)
                                   if (valid)
                                   {
                                     BVH2Ploc node;
                                     node.initLeaf(geomID,ID,bounds); // need to consider pair_offset
                                     node.store(&bvh2[dest_offset]);
                                   }                                                                          
                               }
                             }                                                                                                     
                           }
                         });
		  
      });
    gpu::waitOnEventAndCatchException(quadification_event);
    if (unlikely(verbose)) iteration_time += gpu::getDeviceExecutionTiming(quadification_event);
  }
       
   unsigned int createInstances_initPLOCPrimRefs(sycl::queue &gpu_queue, const _ze_raytracing_geometry_ext_desc_t **const geometry_desc, const unsigned int numGeoms, unsigned int *scratch_mem, const unsigned int MAX_WGS, BVH2Ploc *const bvh2, const unsigned int prim_type_offset, unsigned int *host_device_tasks, double &iteration_time, const bool verbose)    
  {
    const unsigned int numWGs = min((numGeoms+LARGE_WG_SIZE-1)/LARGE_WG_SIZE,(unsigned int)MAX_WGS);
    clearScratchMem(gpu_queue,scratch_mem,0,numWGs,iteration_time,verbose);
   
    static const unsigned int CREATE_INSTANCES_SUB_GROUP_WIDTH = 16;
    static const unsigned int CREATE_INSTANCES_WG_SIZE  = LARGE_WG_SIZE;
    const sycl::nd_range<1> nd_range1(numWGs*CREATE_INSTANCES_WG_SIZE,sycl::range<1>(CREATE_INSTANCES_WG_SIZE));
    sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
        sycl::local_accessor< unsigned int      , 1> counts(sycl::range<1>((CREATE_INSTANCES_WG_SIZE/CREATE_INSTANCES_SUB_GROUP_WIDTH)),cgh);
        sycl::local_accessor< unsigned int      , 1> counts_prefix_sum(sycl::range<1>((CREATE_INSTANCES_WG_SIZE/CREATE_INSTANCES_SUB_GROUP_WIDTH)),cgh);                
        sycl::local_accessor< unsigned int      , 0> _active_counter(cgh);
        sycl::local_accessor< unsigned int      , 0> _global_count_prefix_sum(cgh);
        
        cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item)       
                         {
                           const unsigned int groupID         = item.get_group(0);
                           const unsigned int numGroups       = item.get_group_range(0);                                                        
                           const unsigned int localID         = item.get_local_id(0);
                           const unsigned int step_local      = item.get_local_range().size();
                           const unsigned int startID         = ((size_t)groupID + 0)*(size_t)numGeoms / numWGs;
                           const unsigned int endID           = ((size_t)groupID + 1)*(size_t)numGeoms / numWGs;
                           const unsigned int sizeID          = endID-startID;
                           const unsigned int aligned_sizeID  = gpu::alignTo(sizeID,CREATE_INSTANCES_WG_SIZE);
                           
                           unsigned int &active_counter          = *_active_counter.get_pointer();
                           unsigned int &global_count_prefix_sum = *_global_count_prefix_sum.get_pointer();
                           
                           active_counter = 0;
                           global_count_prefix_sum = 0;            

                           sycl::atomic_ref<unsigned int, sycl::memory_order::relaxed, sycl::memory_scope::work_group,sycl::access::address_space::local_space> counter(active_counter);
                           
                           item.barrier(sycl::access::fence_space::local_space);

                           
                           for (unsigned int ID = localID; ID < aligned_sizeID; ID += step_local)
                           {
                             if (ID < sizeID)
                             {
                               const unsigned int instID = startID + ID;
                               unsigned int count = 0;
                               if (geometry_desc[instID]->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_INSTANCE)
                               {
                                 _ze_raytracing_geometry_instance_ext_desc_t *geom = (_ze_raytracing_geometry_instance_ext_desc_t *)geometry_desc[instID];
                                 gpu::AABB3f bounds;
                                 if (isValidInstance(*geom,bounds)) count = 1;
                               }
                               if (count)
                                 gpu::atomic_add_local(&active_counter,count);
                             }
                           }                             

                           item.barrier(sycl::access::fence_space::local_space);

                           const unsigned int flag = (unsigned int)1<<31;            
                           const unsigned int mask = ~flag;
            
                           if (localID == 0)
                           {
                             sycl::atomic_ref<unsigned int,sycl::memory_order::relaxed, sycl::memory_scope::device,sycl::access::address_space::global_space> scratch_mem_counter(scratch_mem[groupID]);
                             scratch_mem_counter.store(active_counter | flag);              
                           }
            
                           item.barrier(sycl::access::fence_space::global_and_local);

                           // =======================================            
                           // wait until earlier WGs finished as well
                           // =======================================

                           if (localID < groupID)
                           {
                             sycl::atomic_ref<unsigned int, sycl::memory_order::acq_rel, sycl::memory_scope::device,sycl::access::address_space::global_space> global_state(scratch_mem[localID]);
                             unsigned int c = 0;
                             while( ((c = global_state.load()) & flag) == 0 );
                             if (c) gpu::atomic_add_local(&global_count_prefix_sum,(unsigned int)c & mask);
                           }
            
                           item.barrier(sycl::access::fence_space::local_space);

                           unsigned int total_offset = 0;
                           gpu::AABB3f bounds;
                           for (unsigned int ID = localID; ID < aligned_sizeID; ID += step_local)
                           {
                             item.barrier(sycl::access::fence_space::local_space);

                             unsigned int count = 0;
                             if (ID < sizeID)
                             {
                               const unsigned int instID = startID + ID;
                               if (geometry_desc[instID]->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_INSTANCE)
                               {
                                 _ze_raytracing_geometry_instance_ext_desc_t *geom = (_ze_raytracing_geometry_instance_ext_desc_t *)geometry_desc[instID];                                                                  
                                 if (isValidInstance(*geom,bounds)) count = 1;
                               }
                             }

                             unsigned int total_reduction = 0;
                             const unsigned int p_sum = global_count_prefix_sum + total_offset + prefixSumWorkgroup(count,CREATE_INSTANCES_WG_SIZE,counts.get_pointer(),counts_prefix_sum.get_pointer(),item,total_reduction);
                             total_offset += total_reduction;
                             
                             if (ID < sizeID)
                             {
                               const unsigned int instID = startID + ID;
                               if (geometry_desc[instID]->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_INSTANCE && count == 1)
                               {
                                 BVH2Ploc node;                                                                                           
                                 node.initLeaf(instID,0,bounds);                               
                                 node.store(&bvh2[prim_type_offset + p_sum]);                                                                
                               }                               
                             }

                             if (groupID == numGroups-1 && localID == 0)
                               *host_device_tasks = global_count_prefix_sum + (scratch_mem[groupID] & mask);
                           }                                                      
                         });
		  
      });
    gpu::waitOnEventAndCatchException(queue_event);
    if (unlikely(verbose)) iteration_time += gpu::getDeviceExecutionTiming(queue_event);
    const unsigned int numInstances = *host_device_tasks;
    return numInstances;
  }
  
   __forceinline bool buildBounds(const _ze_raytracing_geometry_aabbs_fptr_ext_desc_t* geom, unsigned int primID, BBox3fa& bbox, void* buildUserPtr)
  {
    if (primID >= geom->primCount) return false;
    if (geom->getBounds == nullptr) return false;

    BBox3f bounds;
    (geom->getBounds)(primID,1,geom->geomUserPtr,buildUserPtr,(_ze_raytracing_aabb_ext_t*)&bounds);
    if (unlikely(!isvalid(bounds.lower))) return false;
    if (unlikely(!isvalid(bounds.upper))) return false;
    if (unlikely(bounds.empty())) return false;
    
    bbox = (BBox3f&) bounds;
    return true;
  }
  
   unsigned int createProcedurals_initPLOCPrimRefs(sycl::queue &gpu_queue, const _ze_raytracing_geometry_ext_desc_t **const geometry_desc, const unsigned int numGeoms, unsigned int *scratch_mem, const unsigned int MAX_WGS, BVH2Ploc *const bvh2, const unsigned int prim_type_offset, unsigned int *host_device_tasks, double &iteration_time, const bool verbose)    
  {
    //FIXME: GPU version
    
    unsigned int ID = 0;
    for (unsigned int userGeomID=0;userGeomID<numGeoms;userGeomID++)
    {
      if (unlikely(geometry_desc[userGeomID] == nullptr)) continue;
      if (geometry_desc[userGeomID]->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_AABBS_FPTR)
      {
        _ze_raytracing_geometry_instance_ext_desc_t *geom = (_ze_raytracing_geometry_instance_ext_desc_t *)geometry_desc[userGeomID];
        if (geom->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_AABBS_FPTR)
        {
          _ze_raytracing_geometry_aabbs_fptr_ext_desc_t *procedural = (_ze_raytracing_geometry_aabbs_fptr_ext_desc_t *)geom;
          for (unsigned int i=0;i<procedural->primCount;i++)
          {
            BBox3fa procedural_bounds;
            
            if (!buildBounds(procedural,i,procedural_bounds,procedural->geomUserPtr)) continue;

            gpu::AABB3f bounds = gpu::AABB3f(procedural_bounds.lower.x,procedural_bounds.lower.y,procedural_bounds.lower.z,
                                             procedural_bounds.upper.x,procedural_bounds.upper.y,procedural_bounds.upper.z);
            
            BVH2Ploc node;                             
            node.initLeaf(userGeomID,i,bounds);                               
            node.store(&bvh2[prim_type_offset + ID]);
            ID++;              
          }          
        }
      }
    }
    return ID;
  }

  // ===================================================================================================================================================================================
  // =========================================================================== DISTANCE FUNCTION =====================================================================================
  // ===================================================================================================================================================================================

   float distanceFct(const gpu::AABB3f &bounds0,const gpu::AABB3f &bounds1)
  {
    const gpu::AABB3f bounds = gpu::merge(bounds0,bounds1);
    return bounds.halfArea();
  }
  

  // ====================================================================================================================================================================================
  // ================================================================================= SETUP ============================================================================================
  // ====================================================================================================================================================================================
  

   void computeCentroidGeometryBounds(sycl::queue &gpu_queue, gpu::AABB3f *geometryBounds, gpu::AABB3f *centroidBounds, const BVH2Ploc *const bvh2, const unsigned int numPrimitives, double &iteration_time, const bool verbose)
  {
    const unsigned int wgSize = LARGE_WG_SIZE;
    const sycl::nd_range<1> nd_range1(gpu::alignTo(numPrimitives,wgSize),sycl::range<1>(wgSize));          
    sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
        sycl::local_accessor< gpu::AABB3f, 0> _local_geometry_aabb(cgh);
        sycl::local_accessor< gpu::AABB3f, 0> _local_centroid_aabb(cgh);
                                                       
        cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(16)
                         {
                           const unsigned int localID        = item.get_local_id(0);
                           const unsigned int subgroupLocalID = get_sub_group_local_id();
                           const unsigned int ID = item.get_global_id(0);                                                                                                               
                           gpu::AABB3f *local_geometry_aabb = _local_geometry_aabb.get_pointer();
                           gpu::AABB3f *local_centroid_aabb = _local_centroid_aabb.get_pointer();
                                                                                    
                           gpu::AABB3f geometry_aabb;
                           gpu::AABB3f centroid_aabb;
                           geometry_aabb.init();
                           centroid_aabb.init();

                           if (ID < numPrimitives)
                           {                             
                             const gpu::AABB3f aabb_geom = bvh2[ID].bounds;
                             const gpu::AABB3f aabb_centroid(aabb_geom.centroid2()); 
                             geometry_aabb.extend(aabb_geom);
                             centroid_aabb.extend(aabb_centroid);		      
                                                                                    
                             if (localID == 0)
                             {
                               local_geometry_aabb->init();
                               local_centroid_aabb->init();                                                                                      
                             }
                           }
                           item.barrier(sycl::access::fence_space::local_space);

                           geometry_aabb = geometry_aabb.sub_group_reduce(); 
                           centroid_aabb = centroid_aabb.sub_group_reduce(); 

                           if (subgroupLocalID == 0) 
                           {
                             geometry_aabb.atomic_merge_local(*local_geometry_aabb);
                             centroid_aabb.atomic_merge_local(*local_centroid_aabb);
                           }
                           
                           item.barrier(sycl::access::fence_space::local_space);

                           if (localID == 0)
                           {
                             local_geometry_aabb->atomic_merge_global(*geometryBounds);
                             local_centroid_aabb->atomic_merge_global(*centroidBounds);
                           }
                         });
		  
      });
    gpu::waitOnEventAndCatchException(queue_event);
    if (unlikely(verbose)) iteration_time += gpu::getDeviceExecutionTiming(queue_event);
  }
  
  template<typename type>
    __forceinline void computeMortonCodes64Bit(sycl::queue &gpu_queue, const gpu::AABB3f *const _centroidBounds, type *const mc0, const BVH2Ploc *const bvh2, const unsigned int numPrimitives, const unsigned int shift, const uint64_t mask, double &iteration_time, const bool verbose)    
  {
    const unsigned int wgSize = 16;
    const sycl::nd_range<1> nd_range1(gpu::alignTo(numPrimitives,wgSize),sycl::range<1>(wgSize));              
    sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
        cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(32)
                         {
                           const unsigned int globalID     = item.get_global_id(0);                                                                    
                           if (globalID < numPrimitives)
                           {
                             const gpu::AABB3f centroidBounds(*_centroidBounds);
                             const unsigned int i = globalID;

                             const float3 lower = centroidBounds.lower();                                                                      
                             const unsigned int   grid_size   = 1 << type::GRID_SHIFT;
                             const float3 grid_base(lower.x(),lower.y(),lower.z());
                             const float3 grid_extend(centroidBounds.maxDiagDim());                             
                             const float3 grid_scale = cselect( int3(grid_extend != 0.0f), (grid_size * 0.99f)/grid_extend, float3(0.0f)); 

                             /* calculate and store morton code */
                             const gpu::AABB3f bounds3f = bvh2[i].bounds;
                             const float3 centroid = bounds3f.centroid2();
                             const float3 gridpos_f = (centroid-grid_base)*grid_scale;                                                                      
                             const sycl::uint3 gridpos = gridpos_f.convert<unsigned int,sycl::rounding_mode::rtz>();                             
                             const uint64_t code = (gpu::bitInterleave3D_64bits(gridpos)>>shift) & mask;
                             mc0[i] = type(code,i); 
                           }
                         });
		  
      });
    gpu::waitOnEventAndCatchException(queue_event);
    if (unlikely(verbose)) iteration_time += gpu::getDeviceExecutionTiming(queue_event);      
  }

  template<typename type>
    __forceinline void computeMortonCodes64Bit_SaveMSBBits(sycl::queue &gpu_queue, const gpu::AABB3f *const _centroidBounds, type *const mc0, const BVH2Ploc *const bvh2, unsigned int *const high, const unsigned int numPrimitives, double &iteration_time, const bool verbose)    
  {
    const unsigned int wgSize = 16; //256;
    const sycl::nd_range<1> nd_range1(gpu::alignTo(numPrimitives,wgSize),sycl::range<1>(wgSize));              
    sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
        cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(32)
                         {
                           const unsigned int globalID     = item.get_global_id(0);                                                                    
                           if (globalID < numPrimitives)
                           {
                             const gpu::AABB3f centroidBounds(*_centroidBounds);
                             const unsigned int i = globalID;
                             const float3 lower = centroidBounds.lower();                                                                      
                             const unsigned int   grid_size   = 1 << type::GRID_SHIFT;
                             const float3 grid_base(lower.x(),lower.y(),lower.z());
                             const float3 grid_extend(centroidBounds.maxDiagDim());                             
                             const float3 grid_scale = cselect( int3(grid_extend != 0.0f), (grid_size * 0.99f)/grid_extend, float3(0.0f));
                             /* calculate and store morton code */
                             const gpu::AABB3f bounds3f = bvh2[i].bounds;
                             const float3 centroid = bounds3f.centroid2();
                             const float3 gridpos_f = (centroid-grid_base)*grid_scale;                                                                      
                             const sycl::uint3 gridpos = gridpos_f.convert<unsigned int,sycl::rounding_mode::rtz>();
                             const uint64_t mask = (((uint64_t)1 << 32)-1);
                             const uint64_t code = gpu::bitInterleave3D_64bits(gridpos);
                             high[i] = code >> 32;
                             mc0[i]  = type(code & mask,i); 
                           }
                         });
		  
      });
    gpu::waitOnEventAndCatchException(queue_event);
    if (unlikely(verbose)) iteration_time += gpu::getDeviceExecutionTiming(queue_event);      
  }
  
  template<typename type>
    __forceinline sycl::event restoreMSBBits(sycl::queue &gpu_queue, type *const mc0, unsigned int *const high, const unsigned int numPrimitives, sycl::event &input_event, const bool verbose)    
  {
    const unsigned int wgSize = 16; 
    const sycl::nd_range<1> nd_range1(gpu::alignTo(numPrimitives,wgSize),sycl::range<1>(wgSize));              
    sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
        cgh.depends_on(input_event);        
        cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(32)
                         {
                           const unsigned int globalID     = item.get_global_id(0);                                                                    
                           if (globalID < numPrimitives)
                           {
                             const unsigned int index = mc0[globalID].getIndex();
                             const uint64_t code = high[index];                             
                             mc0[globalID] = type(code,index);                                        
                           }
                         });
		  
      });
    return queue_event;
  }
  

  template<typename type>  
    __forceinline void initClusters(sycl::queue &gpu_queue, type *const mc0, const BVH2Ploc *const bvh2, unsigned int *const cluster_index, BVH2SubTreeState *const bvh2_subtree_size, const unsigned int numPrimitives, double &iteration_time, const bool verbose)    
  {
    static const unsigned int INIT_CLUSTERS_WG_SIZE = 256;    
    const sycl::nd_range<1> nd_range1(sycl::range<1>(gpu::alignTo(numPrimitives,INIT_CLUSTERS_WG_SIZE)),sycl::range<1>(INIT_CLUSTERS_WG_SIZE)); 
    sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
        cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(16)
                         {
                           const unsigned int globalID     = item.get_global_id(0);
                           if (globalID < numPrimitives)
                           {
                             const unsigned int index = mc0[globalID].getIndex();
                             bvh2_subtree_size[globalID] = BVH2SubTreeState(1,1);
                             cluster_index[globalID] = index;
                           }
                         });
                                                       
      });
    gpu::waitOnEventAndCatchException(queue_event);
    if (unlikely(verbose)) iteration_time += gpu::getDeviceExecutionTiming(queue_event);      
  }


  // ====================================================================================================================================================================================
  // ====================================================================================================================================================================================
  // ====================================================================================================================================================================================
  

  __forceinline  unsigned int encodeRelativeOffset(const int localID, const int neighbor)
  {
    const int sOffset = neighbor - localID;
    const unsigned int uOffset = sycl::abs(sOffset)-1;
    return (uOffset<<1) | ((unsigned int)sOffset>>31);
  }

  __forceinline  unsigned int encodeRelativeOffsetFast(const unsigned int ID, const unsigned int neighbor) // === neighbor must be larger than ID ===
  {
    const unsigned int uOffset = neighbor - ID - 1;
    return uOffset<<1;
  }  

  __forceinline int decodeRelativeOffset(const int localID, const unsigned int offset, const unsigned int ID)
  {
    const unsigned int off = (offset>>1)+1;
    return localID + (((offset^ID) % 2 == 0) ? (int)off : -(int)off);
  }


  __forceinline void findNN(const unsigned int localID, const unsigned int ID, const unsigned int local_window_size, const gpu::AABB3f *const cached_bounds, unsigned int *const cached_neighbor, const unsigned int SEARCH_RADIUS_SHIFT,const bool forceNeighborMerge=false)
  {
    /* ---------------------------------------------------------- */                                                       
    /* --- compute nearest neighbor and store result into SLM --- */
    /* ---------------------------------------------------------- */
    const unsigned int SEARCH_RADIUS =    (unsigned int)1<<SEARCH_RADIUS_SHIFT;            
    unsigned int encode_mask   = ~(((unsigned int)1<<(SEARCH_RADIUS_SHIFT+1))-1);

    /* --- only look at exponent if we need to force a merge --- */
    if (forceNeighborMerge)
      encode_mask =~(((unsigned int)1<<(24))-1);
    
    
    unsigned int min_area_index = -1;
    const gpu::AABB3f bounds0 = cached_bounds[localID];              
    for (unsigned int r=1;r<=SEARCH_RADIUS && (localID + r < local_window_size) ;r++)
    {
      const gpu::AABB3f bounds1 = cached_bounds[localID+r];
      const float new_area = distanceFct(bounds0,bounds1);
      unsigned int new_area_i = ((gpu::as_uint(new_area) << 1) & encode_mask);
#if EQUAL_DISTANCES_WORKAROUND == 1
      const unsigned int encode0 = encodeRelativeOffsetFast(localID  ,localID+r);                
      const unsigned int new_area_index0 = new_area_i | encode0 | (ID&1); 
      const unsigned int new_area_index1 = new_area_i | encode0 | (((ID+r)&1)^1); 
#else
      const unsigned int encode0 = encodeRelativeOffset(localID  ,localID+r);                
      const unsigned int encode1 = encodeRelativeOffset(localID+r,localID);   // faster            
      const unsigned int new_area_index0 = new_area_i | encode0;
      const unsigned int new_area_index1 = new_area_i | encode1;            
#endif
      min_area_index = min(min_area_index,new_area_index0);                  
      gpu::atomic_min_local(&cached_neighbor[localID+r],new_area_index1);                  
    }
    gpu::atomic_min_local(&cached_neighbor[localID],min_area_index);

  }


  __forceinline unsigned int getNewClusterIndexCreateBVH2Node(const unsigned int localID, const unsigned int ID, const unsigned int maxID, const unsigned int local_window_start, const unsigned int *const cluster_index_source, const gpu::AABB3f *const cached_bounds, const unsigned int *const cached_neighbor,const unsigned int *const cached_clusterID, BVH2Ploc *const bvh2, unsigned int *const bvh2_index_allocator, BVH2SubTreeState *const bvh2_subtree_size, const unsigned int SEARCH_RADIUS_SHIFT)
  {
    const unsigned int decode_mask =  (((unsigned int)1<<(SEARCH_RADIUS_SHIFT+1))-1);
              
    unsigned int new_cluster_index = -1;
    if (ID < maxID)
    {
      new_cluster_index = cluster_index_source[ID]; // prevents partial writes later 
      //new_cluster_index = cached_clusterID[ID-local_window_start]; //cluster_index_source[ID];
#if EQUAL_DISTANCES_WORKAROUND == 1          
      unsigned int n_i     = decodeRelativeOffset(ID -local_window_start,cached_neighbor[ID -local_window_start] & decode_mask,  ID) + local_window_start;
      unsigned int n_i_n_i = decodeRelativeOffset(n_i-local_window_start,cached_neighbor[n_i-local_window_start] & decode_mask, n_i) + local_window_start;
#else
      unsigned int n_i     = decodeRelativeOffset(ID -local_window_start,cached_neighbor[ID -local_window_start] & decode_mask,  0) + local_window_start;
      unsigned int n_i_n_i = decodeRelativeOffset(n_i-local_window_start,cached_neighbor[n_i-local_window_start] & decode_mask,  0) + local_window_start;          
#endif          
      const gpu::AABB3f bounds = cached_bounds[ID - local_window_start];
      
      if (ID == n_i_n_i)  
      {
        if (ID < n_i)
        {
          const unsigned int leftIndex  = cached_clusterID[ID-local_window_start]; //cluster_index_source[ID];
          const unsigned int rightIndex = cached_clusterID[n_i-local_window_start]; //cluster_index_source[n_i];
          const gpu::AABB3f &leftBounds  = bounds;
          const gpu::AABB3f &rightBounds = cached_bounds[n_i-local_window_start];

          /* --- reduce per subgroup to lower pressure on global atomic counter --- */                                                         
          sycl::atomic_ref<unsigned int, sycl::memory_order::relaxed, sycl::memory_scope::device,sycl::access::address_space::global_space> bvh2_counter(*bvh2_index_allocator);
          const unsigned int bvh2_index = gpu::atomic_add_global_sub_group_shared(bvh2_counter,1);

          /* --- store new BVH2 node --- */
          bvh2[bvh2_index].init(leftIndex,rightIndex,gpu::merge(leftBounds,rightBounds),bvh2_subtree_size[leftIndex],bvh2_subtree_size[rightIndex]);                                                         
          bvh2_subtree_size[bvh2_index] = BVH2SubTreeState(bvh2_subtree_size[leftIndex],bvh2_subtree_size[rightIndex]);
          new_cluster_index = bvh2_index;
        }
        else
          new_cluster_index = -1; /* --- second item of pair with the larger index disables the slot --- */
      }
    }
    return new_cluster_index;
  }
  
  void iteratePLOC (sycl::queue &gpu_queue, PLOCGlobals *const globals, BVH2Ploc *const bvh2, unsigned int *const cluster_index_source, unsigned int *const cluster_index_dest, BVH2SubTreeState *const bvh2_subtree_size, unsigned int *const scratch_mem, const unsigned int numPrims, const unsigned int NN_SEARCH_WG_NUM, unsigned int *host_device_tasks, const unsigned int SEARCH_RADIUS_SHIFT, double &iteration_time, const bool forceNeighborMerge, const bool verbose)    
  {
    static const unsigned int NN_SEARCH_SUB_GROUP_WIDTH = 16;
    static const unsigned int NN_SEARCH_WG_SIZE         = LARGE_WG_SIZE;
    unsigned int *const bvh2_index_allocator            = &globals->bvh2_index_allocator;
    
    sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
        sycl::local_accessor< gpu::AABB3f, 1> cached_bounds  (sycl::range<1>(NN_SEARCH_WG_SIZE),cgh);
        sycl::local_accessor< unsigned int       , 1> cached_neighbor(sycl::range<1>(NN_SEARCH_WG_SIZE),cgh);
        sycl::local_accessor< unsigned int       , 1> cached_clusterID(sycl::range<1>(NN_SEARCH_WG_SIZE),cgh);        
        sycl::local_accessor< unsigned int       , 1> counts(sycl::range<1>((NN_SEARCH_WG_SIZE/NN_SEARCH_SUB_GROUP_WIDTH)),cgh);
        sycl::local_accessor< unsigned int       , 1> counts_prefix_sum(sycl::range<1>((NN_SEARCH_WG_SIZE/NN_SEARCH_SUB_GROUP_WIDTH)),cgh);        
        sycl::local_accessor< unsigned int      ,  0> _wgID(cgh);
        sycl::local_accessor< unsigned int      ,  0> _global_count_prefix_sum(cgh);
                
        const sycl::nd_range<1> nd_range(sycl::range<1>(NN_SEARCH_WG_NUM*NN_SEARCH_WG_SIZE),sycl::range<1>(NN_SEARCH_WG_SIZE));		  
        cgh.parallel_for(nd_range,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(NN_SEARCH_SUB_GROUP_WIDTH) {
            const unsigned int localID        = item.get_local_id(0);
            const unsigned int localSize       = item.get_local_range().size();            
            const unsigned int SEARCH_RADIUS   = (unsigned int)1<<SEARCH_RADIUS_SHIFT;            
            const unsigned int WORKING_WG_SIZE = NN_SEARCH_WG_SIZE - 4*SEARCH_RADIUS; /* reducing working group set size to LARGE_WG_SIZE - 4 * radius to avoid loops */
            

            unsigned int &wgID = *_wgID.get_pointer();
            unsigned int &global_count_prefix_sum = *_global_count_prefix_sum.get_pointer();

            if (localID == 0)
              wgID = gpu::atomic_add_global(&globals->wgID,(unsigned int)1);

            item.barrier(sycl::access::fence_space::local_space);
            
            const unsigned int groupID        = wgID;                                                             
            const unsigned int startID        = ((size_t)groupID + 0)*(size_t)numPrims / NN_SEARCH_WG_NUM;
            const unsigned int endID          = ((size_t)groupID + 1)*(size_t)numPrims / NN_SEARCH_WG_NUM;
            const unsigned int sizeID         = endID-startID;
            const unsigned int aligned_sizeID = gpu::alignTo(sizeID,WORKING_WG_SIZE);
            
            
            unsigned int total_offset = 0;                                                     
            for (unsigned int t=0;t<aligned_sizeID;t+=WORKING_WG_SIZE)
            {
              /* -------------------------------------------------------- */                                                       
              /* --- copy AABBs from cluster representatives into SLM --- */
              /* -------------------------------------------------------- */
                                                       
              const int local_window_start = max((int)(startID+t                )-2*(int)SEARCH_RADIUS  ,(int)0);
              const int local_window_end   = min((int)(startID+t+WORKING_WG_SIZE)+2*(int)SEARCH_RADIUS+1,(int)numPrims);
              const int local_window_size = local_window_end - local_window_start;
              const unsigned int ID = startID + localID + t;
              const unsigned int maxID = min(startID + t + WORKING_WG_SIZE,endID);

              const unsigned int clusterID = cluster_index_source[min(local_window_start+(int)localID,local_window_end-1)];
              cached_bounds[localID] = bvh2[clusterID].bounds;
              cached_clusterID[localID] = clusterID;
              cached_neighbor[localID] = -1;
              
              item.barrier(sycl::access::fence_space::local_space);

              /* ---------------------------------------------------------- */                                                       
              /* --- compute nearest neighbor and store result into SLM --- */
              /* ---------------------------------------------------------- */

              findNN(localID,ID,local_window_size,cached_bounds.get_pointer(),cached_neighbor.get_pointer(),SEARCH_RADIUS_SHIFT,forceNeighborMerge);

              item.barrier(sycl::access::fence_space::local_space);
              
              /* ---------------------------------------------------------- */                                                       
              /* --- merge valid nearest neighbors and create bvh2 node --- */
              /* ---------------------------------------------------------- */

              const unsigned int new_cluster_index = getNewClusterIndexCreateBVH2Node(localID,ID,maxID,local_window_start,cluster_index_source,
                                                                              cached_bounds.get_pointer(),cached_neighbor.get_pointer(),cached_clusterID.get_pointer(),
                                                                              bvh2,bvh2_index_allocator,bvh2_subtree_size,
                                                                              SEARCH_RADIUS_SHIFT);

              const unsigned int flag = new_cluster_index != -1 ? 1 : 0;
              const unsigned int ps = ID < maxID ? flag : 0;
              unsigned int total_reduction = 0;
              const unsigned int p_sum = startID + total_offset + prefixSumWorkgroup(ps,NN_SEARCH_WG_SIZE,counts.get_pointer(),counts_prefix_sum.get_pointer(),item,total_reduction);    

              /* --- store cluster representative into destination array --- */                                                                 
              if (ID < maxID)
                if (new_cluster_index != -1)
                  cluster_index_dest[p_sum] = new_cluster_index;
          
              total_offset += total_reduction;                                          
            }

            /* ----------------------------------------------------------------------------------------- */                                                       
            /* --- store number of valid cluster representatives into scratch mem and set valid flag --- */
            /* ----------------------------------------------------------------------------------------- */

            const unsigned int flag = (unsigned int)1<<31;            
            const unsigned int mask = ~flag;
            
            if (localID == 0)
            {
              sycl::atomic_ref<unsigned int,sycl::memory_order::relaxed, sycl::memory_scope::device,sycl::access::address_space::global_space> scratch_mem_counter(scratch_mem[groupID]);
              scratch_mem_counter.store(total_offset | flag);              
            }

            global_count_prefix_sum = 0;            
            
            item.barrier(sycl::access::fence_space::global_and_local);

            // =======================================            
            // wait until earlier WGs finished as well
            // =======================================

            if (localID < groupID /*NN_SEARCH_WG_NUM */)
            {
              sycl::atomic_ref<unsigned int, sycl::memory_order::acq_rel, sycl::memory_scope::device,sycl::access::address_space::global_space> global_state(scratch_mem[localID]);
              unsigned int c = 0;
              while( ((c = global_state.load()) & flag) == 0 );
              if (c)
                gpu::atomic_add_local(&global_count_prefix_sum,(unsigned int)c & mask);
            }
            
            item.barrier(sycl::access::fence_space::local_space);

            /* ---------------------------------------------------- */                                                       
            /* --- prefix sum over per WG counts in scratch mem --- */
            /* ---------------------------------------------------- */
            const unsigned int active_count = total_offset;
            const unsigned int global_offset = global_count_prefix_sum;            
              
            for (unsigned int t=localID;t<active_count;t+=localSize)
              cluster_index_source[global_offset + t] = cluster_index_dest[startID + t];                                                               

            /* -------------------------------------------------- */                                                       
            /* --- update number of clusters after compaction --- */
            /* -------------------------------------------------- */
                                         
            if (localID == 0 && groupID == NN_SEARCH_WG_NUM-1) // need to be the last group as only this one waits until all previous are done              
              *host_device_tasks = global_offset + active_count;
            
            /* -------------------------------- */                                                       
            /* --- last WG does the cleanup --- */
            /* -------------------------------- */

            if (localID == 0)
            {
              const unsigned int syncID = gpu::atomic_add_global(&globals->sync,(unsigned int)1);
              if (syncID == NN_SEARCH_WG_NUM-1)
              {
                /* --- reset atomics --- */
                globals->wgID = 0;
                globals->sync = 0;
                /* --- reset scratch_mem --- */
                for (unsigned int i=0;i<NN_SEARCH_WG_NUM;i++) 
                  scratch_mem[i] = 0;                
              }
            }            
                                                     
          });		  
      });
    gpu::waitOnEventAndCatchException(queue_event);
    if (unlikely(verbose)) iteration_time += gpu::getDeviceExecutionTiming(queue_event);
  }
  
  // ====================================================================================================================================================================================
  // ====================================================================================================================================================================================
  // ====================================================================================================================================================================================
  
  
  __forceinline unsigned int wgBuild(sycl::nd_item<1> &item,
                             unsigned int *const bvh2_index_allocator,
                             const unsigned int startID,
                             const unsigned int endID,
                             BVH2Ploc *const bvh2,
                             unsigned int *const global_cluster_index_source,
                             unsigned int *const global_cluster_index_dest,
                             BVH2SubTreeState *const bvh2_subtree_size,
                             gpu::AABB3f *const cached_bounds,
                             unsigned int *const cached_neighbor,
                             unsigned int *const cached_clusterID,                             
                             unsigned int *const counts,
                             unsigned int *const counts_prefix_sum,                             
                             unsigned int &active_counter,
                             const unsigned int BOTTOM_UP_THRESHOLD,
                             const unsigned int SEARCH_RADIUS_SHIFT,
                             const unsigned int SINGLE_WG_SIZE = LARGE_WG_SIZE)
  {
    const unsigned int localID         = item.get_local_id(0);
    const unsigned int localSize       = item.get_local_range().size();
    const unsigned int SEARCH_RADIUS   = (unsigned int)1<<SEARCH_RADIUS_SHIFT;
    const unsigned int WORKING_WG_SIZE = SINGLE_WG_SIZE - 4*SEARCH_RADIUS; 
    
    unsigned int *const cluster_index_source = &global_cluster_index_source[startID];
    unsigned int *const cluster_index_dest   = &global_cluster_index_dest[startID];

    float ratio = 100.0f;
    
    unsigned int numPrims = endID-startID; 
    while(numPrims>BOTTOM_UP_THRESHOLD)
    {
      const unsigned int aligned_numPrims = gpu::alignTo(numPrims,WORKING_WG_SIZE);

      unsigned int total_offset = 0;
      for (unsigned int t=0;t<aligned_numPrims;t+=WORKING_WG_SIZE)
      {
        /* -------------------------------------------------------- */                                                       
        /* --- copy AABBs from cluster representatives into SLM --- */
        /* -------------------------------------------------------- */
            
        const int local_window_start = max((int)(t                )-2*(int)SEARCH_RADIUS  ,(int)0);
        const int local_window_end   = min((int)(t+WORKING_WG_SIZE)+2*(int)SEARCH_RADIUS+1,(int)numPrims);
        const int local_window_size = local_window_end - local_window_start;
        
        const unsigned int ID    = localID + t;                                                             
        const unsigned int maxID = min(t + WORKING_WG_SIZE,numPrims);                                                         

        /* --- fill the SLM bounds cache --- */
        
        const unsigned int clusterID = cluster_index_source[min(local_window_start+(int)localID,local_window_end-1)];
        cached_bounds[localID] = bvh2[clusterID].bounds;
        cached_clusterID[localID] = clusterID;        
        cached_neighbor[localID] = -1;              
        

        item.barrier(sycl::access::fence_space::local_space);
        

        /* ---------------------------------------------------------- */                                                       
        /* --- compute nearest neighbor and store result into SLM --- */
        /* ---------------------------------------------------------- */
        findNN(localID,ID,local_window_size,cached_bounds,cached_neighbor,SEARCH_RADIUS_SHIFT,ratio < TOP_LEVEL_RATIO);
        
        item.barrier(sycl::access::fence_space::local_space);
        
        /* ---------------------------------------------------------- */                                                       
        /* --- merge valid nearest neighbors and create bvh2 node --- */
        /* ---------------------------------------------------------- */

        const unsigned int new_cluster_index = getNewClusterIndexCreateBVH2Node(localID,ID,maxID,local_window_start,cluster_index_source,
                                                                        cached_bounds,cached_neighbor,cached_clusterID,
                                                                        bvh2,bvh2_index_allocator,bvh2_subtree_size,
                                                                        SEARCH_RADIUS_SHIFT);
        
        const unsigned int flag = new_cluster_index != -1 ? 1 : 0;
        const unsigned int ps = ID < maxID ? flag : 0;

        unsigned int total_reduction = 0;
        const unsigned int p_sum = total_offset + prefixSumWorkgroup(ps,SINGLE_WG_SIZE,counts,counts_prefix_sum,item,total_reduction);

        /* --- store cluster representative into destination array --- */                                                                 
        if (ID < maxID)
          if (new_cluster_index != -1)
            cluster_index_dest[p_sum] = new_cluster_index;
          
        total_offset += total_reduction;              
      }

      item.barrier(sycl::access::fence_space::global_and_local);

      /*-- copy elements back from dest to source -- */
      
      for (unsigned int t=localID;t<total_offset;t+=localSize)
        cluster_index_source[t] = cluster_index_dest[t];      

      item.barrier(sycl::access::fence_space::local_space);

      const unsigned int new_numPrims = total_offset;
      
      ratio = (float)(numPrims-new_numPrims) / numPrims * 100.0f;

      numPrims = total_offset;
    }
    return numPrims; /* return number of remaining cluster reps */
  }

  // ====================================================================================================================================================================================
  // ====================================================================================================================================================================================
  // ====================================================================================================================================================================================
  
  void singleWGBuild(sycl::queue &gpu_queue, PLOCGlobals *const globals, BVH2Ploc *const bvh2, unsigned int *const cluster_index_source, unsigned int *const cluster_index_dest, BVH2SubTreeState *const bvh2_subtree_size, const unsigned int numPrimitives, const unsigned int SEARCH_RADIUS_SHIFT, double &iteration_time, const bool verbose)
  {
    static const unsigned int SINGLE_WG_SUB_GROUP_WIDTH = 16;
    static const unsigned int SINGLE_WG_SIZE = LARGE_WG_SIZE;
    unsigned int *const bvh2_index_allocator = &globals->bvh2_index_allocator;
    
    sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
        const sycl::nd_range<1> nd_range(SINGLE_WG_SIZE,sycl::range<1>(SINGLE_WG_SIZE));

        /* local variables */
        sycl::local_accessor< gpu::AABB3f, 1> cached_bounds  (sycl::range<1>(SINGLE_WG_SIZE),cgh);
        sycl::local_accessor< unsigned int       , 1> cached_neighbor(sycl::range<1>(SINGLE_WG_SIZE),cgh);
        sycl::local_accessor< unsigned int       , 1> cached_clusterID(sycl::range<1>(SINGLE_WG_SIZE),cgh);        
        sycl::local_accessor< unsigned int       , 1> counts(sycl::range<1>((SINGLE_WG_SIZE/SINGLE_WG_SUB_GROUP_WIDTH)),cgh);
        sycl::local_accessor< unsigned int       , 1> counts_prefix_sum(sycl::range<1>((SINGLE_WG_SIZE/SINGLE_WG_SUB_GROUP_WIDTH)),cgh);
        sycl::local_accessor< unsigned int       , 0> _active_counter(cgh);
                                             
        cgh.parallel_for(nd_range,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(SINGLE_WG_SUB_GROUP_WIDTH) {
            unsigned int &active_counter = *_active_counter.get_pointer();
            wgBuild(item, bvh2_index_allocator, 0, numPrimitives, bvh2, cluster_index_source, cluster_index_dest, bvh2_subtree_size, cached_bounds.get_pointer(), cached_neighbor.get_pointer(), cached_clusterID.get_pointer(), counts.get_pointer(),  counts_prefix_sum.get_pointer(), active_counter, 1, SEARCH_RADIUS_SHIFT, SINGLE_WG_SIZE);

            const unsigned int localID        = item.get_local_id(0);                                                                 
            if (localID == 0) globals->rootIndex = globals->bvh2_index_allocator-1;
          });		  
      });            
    gpu::waitOnEventAndCatchException(queue_event);
    if (unlikely(verbose)) iteration_time += gpu::getDeviceExecutionTiming(queue_event);      
  }


  // =========================================================================================================================================================================
  // ====================================================================== Rebalance BVH2 ===================================================================================
  // =========================================================================================================================================================================
  
  __forceinline void rebalanceBVH2(BVH2Ploc *bvh2, unsigned int root, const unsigned int numPrimitives,const BVH2SubTreeState *const bvh2_subtree_size, unsigned int *const inner, unsigned int *const leaves, const unsigned int maxEntries)
  {
    unsigned int numLeaves = 0;
    unsigned int numInner = 1;
    inner[0] = root;

    unsigned int start = 0;
    while(start<numInner && numInner < maxEntries)
    {
      const unsigned int num = numInner-start;
      unsigned int plus = 0;
      for (unsigned int i=0;i<num;i++,plus++)
      {
        const unsigned int index = inner[start+i];
        const unsigned int left  = bvh2[index].leftIndex();          
        if (BVH2Ploc::isFatLeaf(bvh2[index].left,numPrimitives))
          leaves[numLeaves++] = left;
        else
          inner[numInner++] = left;

        const unsigned int right = bvh2[index].rightIndex();
        if (BVH2Ploc::isFatLeaf(bvh2[index].right,numPrimitives))
          leaves[numLeaves++] = right;
        else
          inner[numInner++] = right;

        if (numInner >= maxEntries) break;
      }
      start+=plus;
    }

    while(numInner >= numLeaves)
      leaves[numLeaves++] = inner[--numInner];
    
    unsigned int active = numLeaves;
    while(active > 1)
    {
      unsigned int new_active = 0;
      for (unsigned int i=0;i<active;i+=2)
        if (i+1 < active)
        {
          const unsigned int innerID = inner[--numInner];
          const unsigned int leftIndex  = leaves[i+0];
          const unsigned int rightIndex = leaves[i+1];
          const gpu::AABB3f &leftBounds  = bvh2[leftIndex].bounds;
          const gpu::AABB3f &rightBounds = bvh2[rightIndex].bounds;          
          bvh2[innerID].init(leftIndex,rightIndex,gpu::merge(leftBounds,rightBounds),bvh2_subtree_size[leftIndex],bvh2_subtree_size[rightIndex]);                                                         
          //bvh2_subtree_size[innerID] = BVH2SubTreeState(bvh2_subtree_size[leftIndex],bvh2_subtree_size[rightIndex]);
          leaves[new_active++] = innerID;
        }
        else
          leaves[new_active++] = leaves[i];
      active = new_active;      
    }    
  }
  
  __forceinline void rebalanceBVH2(sycl::queue &gpu_queue, BVH2Ploc *const bvh2, const BVH2SubTreeState *const bvh2_subtree_size, const unsigned int numPrimitives, double &iteration_time, const bool verbose)    
  {
    static const unsigned int REBALANCE_BVH2_WG_SIZE = 16;
    static const unsigned int MAX_NUM_REBALANCE_NODES = 256;
    
    const sycl::nd_range<1> nd_range1(gpu::alignTo(numPrimitives,REBALANCE_BVH2_WG_SIZE),sycl::range<1>(REBALANCE_BVH2_WG_SIZE));
    sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
        sycl::local_accessor< unsigned int, 1> _inner(sycl::range<1>((MAX_NUM_REBALANCE_NODES)),cgh);
        sycl::local_accessor< unsigned int, 1> _leaves(sycl::range<1>((MAX_NUM_REBALANCE_NODES)),cgh);                
        
        cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(16)
                         {
                           const unsigned int ID = item.get_global_id(0);
                           if (ID < numPrimitives)
                           {
                             const unsigned int global_root = numPrimitives+ID;
                             unsigned int *inner  = _inner.get_pointer();
                             unsigned int *leaves = _leaves.get_pointer();
                             unsigned int mask = sub_group_ballot(bvh2_subtree_size[global_root].isMarked());
                             while (mask)
                             {
                               const unsigned int index = sycl::ctz(mask);
                               mask &= mask-1;
                               const unsigned int root = sub_group_broadcast(global_root,index);
                               const unsigned int leftIndex = bvh2[root].leftIndex();
                               const unsigned int rightIndex = bvh2[root].rightIndex();
                               if (bvh2_subtree_size[leftIndex].depth >= REBALANCE_BVH2_MINIMUM_DEPTH)                             
                                 rebalanceBVH2(bvh2,leftIndex,numPrimitives,bvh2_subtree_size,inner,leaves,MAX_NUM_REBALANCE_NODES);
                               if (bvh2_subtree_size[rightIndex].depth >= REBALANCE_BVH2_MINIMUM_DEPTH)                             
                                 rebalanceBVH2(bvh2,rightIndex,numPrimitives,bvh2_subtree_size,inner,leaves,MAX_NUM_REBALANCE_NODES);                             
                             }
                           }
                         });
                                                       
      });
    gpu::waitOnEventAndCatchException(queue_event);
    if (unlikely(verbose)) iteration_time += gpu::getDeviceExecutionTiming(queue_event);
  }
  

  // ===================================================================================================================================================================================
  // ====================================================================== BVH2 -> QBVH6 conversion ===================================================================================
  // ===================================================================================================================================================================================


  __forceinline void getLeafIndices(const unsigned int first_index, const BVH2Ploc *const bvh_nodes, unsigned int *const dest, unsigned int &indexID, const unsigned int numPrimitives)
  {
    dest[0] = first_index;
    unsigned int old_indexID = 0;
    indexID = 1;
    while(old_indexID != indexID)
    {
      old_indexID = indexID;
      for (unsigned int i=0;i<old_indexID;i++)
        if (!BVH2Ploc::isLeaf(dest[i],numPrimitives))
        {
          const unsigned int left = bvh_nodes[BVH2Ploc::getIndex(dest[i])].left;
          const unsigned int right = bvh_nodes[BVH2Ploc::getIndex(dest[i])].right;
          dest[i]         = left;
          dest[indexID++] = right;
        }
    }
  }  

  __forceinline void writeNode(void *_dest, const unsigned int relative_block_offset, const gpu::AABB3f &parent_bounds, const unsigned int numChildren, unsigned int indices[BVH_BRANCHING_FACTOR], const BVH2Ploc *const bvh2, const unsigned int numPrimitives, const NodeType type, const GeometryTypeRanges &geometryTypeRanges)
  {
    unsigned int *dest = (unsigned int*)_dest;    
    //dest = (unsigned int*) __builtin_assume_aligned(dest,16);
    
    const float _ulp = std::numeric_limits<float>::epsilon();
    const float up = 1.0f + float(_ulp);  
    const gpu::AABB3f conservative_bounds = parent_bounds.conservativeBounds();
    const float3 len = conservative_bounds.size() * up;
      
    int _exp_x; float mant_x = frexp(len.x(), &_exp_x); _exp_x += (mant_x > 255.0f / 256.0f);
    int _exp_y; float mant_y = frexp(len.y(), &_exp_y); _exp_y += (mant_y > 255.0f / 256.0f);
    int _exp_z; float mant_z = frexp(len.z(), &_exp_z); _exp_z += (mant_z > 255.0f / 256.0f);
    _exp_x = max(-128,_exp_x); // enlarge too tight bounds
    _exp_y = max(-128,_exp_y);
    _exp_z = max(-128,_exp_z);

    const float3 lower(conservative_bounds.lower_x,conservative_bounds.lower_y,conservative_bounds.lower_z);
    
    dest[0]  = gpu::as_uint(lower.x());
    dest[1]  = gpu::as_uint(lower.y());
    dest[2]  = gpu::as_uint(lower.z());
    dest[3]  = relative_block_offset; 

    uint8_t tmp[48];
    
    tmp[0]         = type; // type
    tmp[1]         = 0;    // pad 
    tmp[2]         = _exp_x; assert(_exp_x >= -128 && _exp_x <= 127); 
    tmp[3]         = _exp_y; assert(_exp_y >= -128 && _exp_y <= 127);
    tmp[4]         = _exp_z; assert(_exp_z >= -128 && _exp_z <= 127);
    tmp[5]         = 0xff; //instanceMode ? 1 : 0xff;
    
#pragma nounroll
    for (unsigned int i=0;i<BVH_BRANCHING_FACTOR;i++)
    {
      const unsigned int index = BVH2Ploc::getIndex(indices[sycl::min(i,numChildren-1)]);
      // === default is invalid ===
      uint8_t lower_x = 0x80;
      uint8_t lower_y = 0x80;
      uint8_t lower_z = 0x80;    
      uint8_t upper_x = 0x00;
      uint8_t upper_y = 0x00;
      uint8_t upper_z = 0x00;
      uint8_t data    = 0x00;      
      // === determine leaf type ===
      const bool isLeaf = index < numPrimitives; // && !forceFatLeaves;      
      const bool isInstance   = geometryTypeRanges.isInstance(index);  
      const bool isProcedural = geometryTypeRanges.isProcedural(index);      
      const unsigned int numBlocks    = isInstance ? 2 : 1;
      NodeType leaf_type = NODE_TYPE_QUAD;
      leaf_type = isInstance ? NODE_TYPE_INSTANCE   : leaf_type;
      leaf_type = isProcedural ? NODE_TYPE_PROCEDURAL : leaf_type;      
      data = (i<numChildren) ? numBlocks : 0;
      data |= (isLeaf ? leaf_type : NODE_TYPE_INTERNAL) << 2;
      const gpu::AABB3f childBounds = bvh2[index].bounds; //.conservativeBounds();
      // === bounds valid ? ====
      const unsigned int equal_dims = childBounds.numEqualDims();
      const bool write = (i<numChildren) && equal_dims <= 1;
      // === quantize bounds ===
      const gpu::AABB3f  qbounds    = QBVHNodeN::quantize_bounds(lower, _exp_x, _exp_y, _exp_z, childBounds);
      // === updated discretized bounds ===
      lower_x = write ? (uint8_t)qbounds.lower_x : lower_x;
      lower_y = write ? (uint8_t)qbounds.lower_y : lower_y;
      lower_z = write ? (uint8_t)qbounds.lower_z : lower_z;
      upper_x = write ? (uint8_t)qbounds.upper_x : upper_x;
      upper_y = write ? (uint8_t)qbounds.upper_y : upper_y;
      upper_z = write ? (uint8_t)qbounds.upper_z : upper_z;
      // === init child in node ===
      tmp[ 6+i] = data;
      tmp[12+i] = lower_x;
      tmp[18+i] = upper_x;      
      tmp[24+i] = lower_y;
      tmp[30+i] = upper_y;      
      tmp[36+i] = lower_z;
      tmp[42+i] = upper_z;      
    }
    // === write out second part of 64 bytes node ===
    for (unsigned int i=0;i<12;i++)
      dest[4+i] = tmp[i*4+0] | (tmp[i*4+1]<<8) | (tmp[i*4+2]<<16) | (tmp[i*4+3]<<24);
  }

  __forceinline unsigned int getNumLeaves(const unsigned int first_index, const BVH2Ploc *const bvh_nodes, const unsigned int numPrimitives)
  {
    unsigned int dest[BVH_BRANCHING_FACTOR];
    dest[0] = BVH2Ploc::getIndex(first_index);
    unsigned int old_indexID = 0;
    unsigned int indexID = 1;
    while(old_indexID != indexID)
    {
      old_indexID = indexID;
      for (unsigned int i=0;i<old_indexID;i++)
        if (!BVH2Ploc::isLeaf(dest[i],numPrimitives))
        {
          const unsigned int left = bvh_nodes[BVH2Ploc::getIndex(dest[i])].left;
          const unsigned int right = bvh_nodes[BVH2Ploc::getIndex(dest[i])].right;
          dest[i]         = left;
          dest[indexID++] = right;
        }
    }
    return indexID;
  }  
    
  __forceinline unsigned int openBVH2MaxAreaSortChildren(const unsigned int index, unsigned int indices[BVH_BRANCHING_FACTOR], const BVH2Ploc *const bvh2, const unsigned int numPrimitives)
  {
    float areas[BVH_BRANCHING_FACTOR];
                                                                          
    const unsigned int _left  = bvh2[BVH2Ploc::getIndex(index)].left;
    const unsigned int _right = bvh2[BVH2Ploc::getIndex(index)].right;
                                                                          
    indices[0] = _left;
    indices[1] = _right;
    areas[0]  = (!BVH2Ploc::isFatLeaf( _left,numPrimitives)) ? bvh2[BVH2Ploc::getIndex( _left)].bounds.area() : neg_inf;    
    areas[1]  = (!BVH2Ploc::isFatLeaf(_right,numPrimitives)) ? bvh2[BVH2Ploc::getIndex(_right)].bounds.area() : neg_inf; 

    unsigned int numChildren = 2;    
#if 1   
    while (numChildren < BVH_BRANCHING_FACTOR)
    {      
      /*! find best child to split */
      float bestArea = areas[0];
      unsigned int bestChild = 0;
      for (unsigned int i=1;i<numChildren;i++)
        if (areas[i] > bestArea)
        {
          bestArea = areas[i];
          bestChild = i;
        }
      
      if (areas[bestChild] < 0.0f) break;
      
      const unsigned int bestNodeID = indices[bestChild];      
      const unsigned int left  = bvh2[BVH2Ploc::getIndex(bestNodeID)].left;
      const unsigned int right = bvh2[BVH2Ploc::getIndex(bestNodeID)].right;

      areas[bestChild]     = (!BVH2Ploc::isFatLeaf(left,numPrimitives)) ? bvh2[BVH2Ploc::getIndex(left)].bounds.area() : neg_inf; 
      areas[numChildren]   = (!BVH2Ploc::isFatLeaf(right,numPrimitives)) ? bvh2[BVH2Ploc::getIndex(right)].bounds.area() : neg_inf; 
      indices[bestChild]   = left;      
      indices[numChildren] = right;                                                                            
      numChildren++;      
    }
#else
    while(numChildren < BVH_BRANCHING_FACTOR)
    {
      const unsigned int cur_numChildren = numChildren;      
      for (unsigned int i=0;i<cur_numChildren && numChildren < BVH_BRANCHING_FACTOR;i++)
      {
        if (!BVH2Ploc::isFatLeaf(indices[i],numPrimitives))
        {
          const unsigned int left  = bvh2[BVH2Ploc::getIndex(indices[i])].left;
          const unsigned int right = bvh2[BVH2Ploc::getIndex(indices[i])].right;          
          indices[i]   = left;      
          indices[numChildren] = right;                                                                            
          numChildren++;                
        }
      }
      if (cur_numChildren == numChildren) break;
    }
#endif    
    
    for (unsigned int i=0;i<numChildren;i++)
      areas[i] = fabs(areas[i]);

    for (unsigned int m=0; m<numChildren-1; m++)
      for (unsigned int n=m+1; n<numChildren; n++)
        if (areas[m] < areas[n])
        {
          std::swap(areas[m],areas[n]);
          std::swap(indices[m],indices[n]);
        }

    return numChildren;
  }  

  __forceinline void write(const QuadLeaf &q, float16 *out) 
  {
    out[0].s0() = gpu::as_float(q.header[0]);      
    out[0].s1() = gpu::as_float(q.header[1]);
    out[0].s2() = gpu::as_float(q.header[2]);
    out[0].s3() = gpu::as_float(q.header[3]);
    out[0].s4() = q.v0.x;
    out[0].s5() = q.v0.y;
    out[0].s6() = q.v0.z;
    out[0].s7() = q.v1.x;
    out[0].s8() = q.v1.y;
    out[0].s9() = q.v1.z;
    out[0].sA() = q.v2.x;
    out[0].sB() = q.v2.y;
    out[0].sC() = q.v2.z;
    out[0].sD() = q.v3.x;
    out[0].sE() = q.v3.y;
    out[0].sF() = q.v3.z;      
  }

  // =============================================================================================================================================
  // =============================================================================================================================================
  // =============================================================================================================================================


   bool convertBVH2toQBVH6(sycl::queue &gpu_queue, PLOCGlobals *globals, unsigned int *host_device_tasks, const _ze_raytracing_geometry_ext_desc_t **const geometries, QBVH6 *qbvh, const BVH2Ploc *const bvh2, LeafGenerationData *leafGenData, const unsigned int numPrimitives, const bool instanceMode, const GeometryTypeRanges &geometryTypeRanges, float& conversion_device_time, const bool verbose)
  {
    static const unsigned int STOP_THRESHOLD = 1296;    
    double total_time = 0.0f;    
    conversion_device_time = 0.0f;
    
    const bool forceFatLeaves = numPrimitives <= BVH_BRANCHING_FACTOR;
    
    host_device_tasks[0] = 0;
    host_device_tasks[1] = 0;

    /* ---- Phase I: single WG generates enough work for the breadth-first phase --- */
    {
      const unsigned int wgSize = LARGE_WG_SIZE;
      const sycl::nd_range<1> nd_range1(wgSize,sycl::range<1>(wgSize));                    
      sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
          sycl::local_accessor< unsigned int      ,  0> _node_mem_allocator_cur(cgh);                                                   
          cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(16)      
                           {
                             const unsigned int localID     = item.get_local_id(0);
                             const unsigned int localSize   = item.get_local_range().size();                                                                      
                             unsigned int &node_mem_allocator_cur = *_node_mem_allocator_cur.get_pointer();

                             const unsigned int node_start = 2;
                             const unsigned int node_end   = 3;
                                                                      
                             if (localID == 0)
                             {
                               // === init globals ===
                               globals->node_mem_allocator_start = node_start;
                               globals->node_mem_allocator_cur   = node_end;
                               globals->qbvh_base_pointer        = (char*)qbvh;
                               // === init intial state ===
                               TmpNodeState *root_state = (TmpNodeState*)((char*)qbvh + 64 * node_start);
                               unsigned int rootIndex = globals->rootIndex;
                               // === make fat leaf if #prims < threshold ===
                               if (numPrimitives <= FATLEAF_THRESHOLD)
                                 rootIndex = BVH2Ploc::makeFatLeaf(rootIndex,numPrimitives);                                                                        
                               root_state->init( rootIndex );                                                                        
                               node_mem_allocator_cur = node_end;
                             }

                             item.barrier(sycl::access::fence_space::global_and_local);

                             unsigned int startBlockID = node_start;
                             unsigned int endBlockID   = node_mem_allocator_cur;

                             while(1)
                             {                                                                        
                               item.barrier(sycl::access::fence_space::local_space);
                               
                               if (startBlockID == endBlockID || endBlockID-startBlockID>STOP_THRESHOLD) break;
                               
                               for (unsigned int innerID=startBlockID+localID;innerID<endBlockID;innerID+=localSize)
                               {
                                 TmpNodeState *state = (TmpNodeState *)globals->nodeBlockPtr(innerID);
                                 const unsigned int header = state->header;                                                                        
                                 const unsigned int index  = state->bvh2_index;
                                 char* curAddr = (char*)state;

                                 if (header == 0x7fffffff)
                                 {
                                   if (!BVH2Ploc::isLeaf(index,numPrimitives))
                                   {                                                                              
                                     if (!BVH2Ploc::isFatLeaf(index,numPrimitives))
                                     {
                                       unsigned int indices[BVH_BRANCHING_FACTOR];                                                                                
                                       const unsigned int numChildren = openBVH2MaxAreaSortChildren(index,indices,bvh2,numPrimitives);
                                       unsigned int numBlocks = 0;
                                       for (unsigned int i=0;i<numChildren;i++)
                                         numBlocks += geometryTypeRanges.isInstance(BVH2Ploc::getIndex(indices[i])) ? 2 : 1; 
                                                                                    
                                       const unsigned int allocID = gpu::atomic_add_local(&node_mem_allocator_cur,numBlocks);
                                                                                
                                       char* childAddr = (char*)globals->qbvh_base_pointer + 64 * allocID;
                                       writeNode(curAddr,allocID-innerID,bvh2[BVH2Ploc::getIndex(index)].bounds,numChildren,indices,bvh2,numPrimitives,NODE_TYPE_MIXED,geometryTypeRanges);
                                       unsigned int offset = 0;
                                       for (unsigned int j=0;j<numChildren;j++)
                                       {
                                         TmpNodeState *childState = (TmpNodeState *)(childAddr + offset*64);
                                         childState->init(indices[j]);                                                                                  
                                         const bool isInstance = geometryTypeRanges.isInstance(BVH2Ploc::getIndex(indices[j]));
                                         // === invalidate header for second cache line in instance case ===
                                         if (isInstance)
                                           *(unsigned int*)(childAddr + offset*64 + 64) = 0; 
                                         offset += isInstance ? 2 : 1;                                                                                  
                                       }
                                     }
                                   }
                                 }
                               }

                               item.barrier(sycl::access::fence_space::global_and_local);
                                                                        
                               startBlockID = endBlockID;
                               endBlockID = node_mem_allocator_cur;
                             }
                             // write out local node allocator to globals 
                             if (localID == 0)
                             {
                               startBlockID = globals->node_mem_allocator_start;
                               globals->range_start = startBlockID;
                               globals->range_end   = endBlockID;                                                                        
                               globals->node_mem_allocator_cur = node_mem_allocator_cur;
                               host_device_tasks[0] = endBlockID - startBlockID;
                               host_device_tasks[1] = endBlockID - globals->node_mem_allocator_start;
                               if (unlikely(globals->node_mem_allocator_cur > globals->leaf_mem_allocator_start))
                                 host_device_tasks[0] = -1;                                 
                             }
                                                                      
                           });
        });
      gpu::waitOnEventAndCatchException(queue_event);
      if (unlikely(verbose)) total_time += gpu::getDeviceExecutionTiming(queue_event);
    }

    if (unlikely(host_device_tasks[0] == -1)) return false;

    /* ---- Phase II: full breadth-first phase until only fat leaves or single leaves remain--- */

    struct __aligned(64) LocalNodeData {
      unsigned int v[16];
    };
    while(1)
    {      
      const unsigned int blocks = host_device_tasks[0];
      if (blocks == 0 || blocks == -1) break;
      
      const unsigned int wgSize = 256;
      const sycl::nd_range<1> nd_range1(gpu::alignTo(blocks,wgSize),sycl::range<1>(wgSize));              
      sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
          sycl::local_accessor< LocalNodeData, 1> _localNodeData(sycl::range<1>(wgSize),cgh);                        
          cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(16)      
                           {
                             const unsigned int localID   = item.get_local_id(0);                                                                      
                             const unsigned int globalID  = item.get_global_id(0);
                             const unsigned int numGroups   = item.get_group_range(0);                             
                             const unsigned int startBlockID = globals->range_start; 
                             const unsigned int endBlockID   = globals->range_end;                              
                             const unsigned int innerID   = startBlockID + globalID;
                             char* curAddr = nullptr;
                             bool valid = false;

                             LocalNodeData *localNodeData = _localNodeData.get_pointer();
                                                                      
                             if (innerID < endBlockID)
                             {
                               TmpNodeState *state = (TmpNodeState *)globals->nodeBlockPtr(innerID);
                               const unsigned int header = state->header;                                                                        
                               const unsigned int index  = state->bvh2_index;
                               curAddr = (char*)state;
                               if (header == 0x7fffffff)
                               {
                                 if (!BVH2Ploc::isLeaf(index,numPrimitives))
                                 {                                                                            
                                   if (!BVH2Ploc::isFatLeaf(index,numPrimitives))
                                   {                                                                              
                                     unsigned int indices[BVH_BRANCHING_FACTOR];
                                     const unsigned int numChildren = openBVH2MaxAreaSortChildren(index,indices,bvh2,numPrimitives);
                                     unsigned int numBlocks = 0;
                                     for (unsigned int i=0;i<numChildren;i++)
                                       numBlocks += geometryTypeRanges.isInstance(BVH2Ploc::getIndex(indices[i])) ? 2 : 1; 
                                                                              
                                     const unsigned int childBlockID = globals->atomic_add_sub_group_varying_allocNodeBlocks(numBlocks);
                                     char *const childAddr = globals->nodeBlockPtr(childBlockID);
                                                                              
                                     valid = true;
                                     writeNode(localNodeData[localID].v,childBlockID-innerID,bvh2[BVH2Ploc::getIndex(index)].bounds,numChildren,indices,bvh2,numPrimitives,NODE_TYPE_MIXED,geometryTypeRanges);
                                     unsigned int offset = 0;
                                     for (unsigned int j=0;j<numChildren;j++)
                                     {
                                       TmpNodeState *childState = (TmpNodeState *)(childAddr + offset*64);
                                       childState->init(indices[j]);
                                       const bool isInstance = geometryTypeRanges.isInstance(BVH2Ploc::getIndex(indices[j]));
                                       // === invalid header for second cache line in instance case ===
                                       if (isInstance)
                                         *(unsigned int*)(childAddr + offset*64 + 64) = 0; 
                                       offset += isInstance ? 2 : 1;                                                                                 
                                     }
                                   }
                                 }
                               }
                             }

                             item.barrier(sycl::access::fence_space::local_space);

                                                                      
                             const unsigned int subgroupLocalID = get_sub_group_local_id();
                             unsigned int mask = sub_group_ballot(valid);
                             while(mask)
                             {
                               const unsigned int index = sycl::ctz(mask);
                               mask &= mask-1;
                               unsigned int ID = sub_group_broadcast(localID,index);
                               unsigned int* dest = sub_group_broadcast((unsigned int*)curAddr,index);
                               const unsigned int v = localNodeData[ID].v[subgroupLocalID];                                                                        
                               sub_group_store(dest,v);
                             }
                                                                      
                             /* -------------------------------- */                                                       
                             /* --- last WG does the cleanup --- */
                             /* -------------------------------- */

                             if (localID == 0)
                             {
                               const unsigned int syncID = gpu::atomic_add_global(&globals->sync,(unsigned int)1);
                               if (syncID == numGroups-1)
                               {
                                 /* --- reset atomics --- */
                                 globals->sync = 0;
                                 const unsigned int new_startBlockID = globals->range_end;
                                 const unsigned int new_endBlockID   = globals->node_mem_allocator_cur;                                 
                                 globals->range_start = new_startBlockID;
                                 globals->range_end   = new_endBlockID;
                                 host_device_tasks[0] = new_endBlockID - new_startBlockID;
                                 host_device_tasks[1] = new_endBlockID - globals->node_mem_allocator_start;
                                 if (unlikely(globals->node_mem_allocator_cur > globals->leaf_mem_allocator_start))
                                   host_device_tasks[0] = -1;                                                                  
                               }
                             }
                             
                           });
		  
        });
      gpu::waitOnEventAndCatchException(queue_event);
      if (unlikely(verbose)) total_time += gpu::getDeviceExecutionTiming(queue_event);
    }

    if (unlikely(host_device_tasks[0] == -1)) return false;
    
    /* ---- Phase III: fill in mixed leafs and generate inner node for fatleaves plus storing primID, geomID pairs for final phase --- */
    const unsigned int blocks = host_device_tasks[1];
    if (blocks)
    {
      const unsigned int wgSize = 256;
      const sycl::nd_range<1> nd_range1(gpu::alignTo(blocks,wgSize),sycl::range<1>(wgSize));              
      sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
          sycl::local_accessor< unsigned int  ,  0> _local_numBlocks(cgh);
          sycl::local_accessor< unsigned int  ,  0> _local_numLeaves(cgh);                                                   
          sycl::local_accessor< unsigned int  ,  0> _global_blockID(cgh);
          sycl::local_accessor< unsigned int  ,  0> _global_numLeafID(cgh);                                                   
          sycl::local_accessor< LeafGenerationData, 1> _local_leafGenData(sycl::range<1>(wgSize*BVH_BRANCHING_FACTOR),cgh);
          sycl::local_accessor< unsigned int, 1> _local_indices(sycl::range<1>(wgSize*BVH_BRANCHING_FACTOR),cgh);
          cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(16)      
                           {
                             const unsigned int localID   = item.get_local_id(0);                             
                             const unsigned int globalID  = item.get_global_id(0);
                             const unsigned int numGroups   = item.get_group_range(0);
                             const unsigned int localSize   = item.get_local_range().size();                                                                      
                                                                      
                             const unsigned int startBlockID = globals->node_mem_allocator_start;
                             const unsigned int endBlockID   = globals->node_mem_allocator_cur;                             
                             const unsigned int innerID      = startBlockID + globalID;

                             unsigned int &local_numBlocks   = *_local_numBlocks.get_pointer();
                             unsigned int &local_numLeaves   = *_local_numLeaves.get_pointer();                                                                      
                             unsigned int &global_blockID    = *_global_blockID.get_pointer();
                             unsigned int &global_leafID     = *_global_numLeafID.get_pointer();
                                                                      
                             LeafGenerationData* local_leafGenData = _local_leafGenData.get_pointer();

                             unsigned int *indices = _local_indices.get_pointer() + BVH_BRANCHING_FACTOR * localID;
                                                                      
                             if (localID == 0)
                             {
                               local_numBlocks = 0;
                               local_numLeaves = 0;
                             }
                                                                      
                             item.barrier(sycl::access::fence_space::local_space);
                                                                      
                             char* curAddr      = nullptr;
                             unsigned int numChildren   = 0;
                             bool isFatLeaf     = false;
                             unsigned int local_blockID = 0;
                             unsigned int local_leafID  = 0;
                             unsigned int index         = 0;
                                                                      
                             if (innerID < endBlockID)                                                                        
                             {
                               TmpNodeState *state = (TmpNodeState *)globals->nodeBlockPtr(innerID);
                               index   = state->bvh2_index;

                                                                        
                               curAddr = (char*)state;
                               if (state->header == 0x7fffffff) // not processed yet
                               {
                                 isFatLeaf = !BVH2Ploc::isLeaf(index,numPrimitives) || forceFatLeaves;
                                 unsigned int numBlocks = 0;
                                 if (isFatLeaf) 
                                 {
                                   numChildren = 0;
                                   getLeafIndices(index,bvh2,indices,numChildren,numPrimitives);
                                   for (unsigned int i=0;i<numChildren;i++)
                                     numBlocks += geometryTypeRanges.isInstance(BVH2Ploc::getIndex(indices[i])) ? 2 : 1;
                                 }
                                 else
                                 {
                                   numChildren = 1;
                                   numBlocks = 0; // === already been allocated in inner node ===
                                   indices[0] = index;
                                 }
                                 local_blockID = gpu::atomic_add_local(&local_numBlocks,numBlocks);
                                 local_leafID  = gpu::atomic_add_local(&local_numLeaves,numChildren);
                               }
                             }

                                                                      
                             item.barrier(sycl::access::fence_space::local_space);
                                                                      
                             const unsigned int numBlocks = local_numBlocks;
                             const unsigned int numLeaves = local_numLeaves;                                                                                                                                              
                             if (localID == 0)
                             {
                               global_blockID = gpu::atomic_add_global(&globals->leaf_mem_allocator_cur,numBlocks);
                               global_leafID  = gpu::atomic_add_global(&globals->numLeaves,numLeaves);
                             }
                                                                      
                             item.barrier(sycl::access::fence_space::local_space);

                             const unsigned int blockID = global_blockID + local_blockID;
                             const unsigned int leafID  = global_leafID; //  + local_leafID;
                                                                      
                                                                      
                             if (isFatLeaf)
                               writeNode(curAddr,blockID-innerID,bvh2[BVH2Ploc::getIndex(index)].bounds,numChildren,indices,bvh2,numPrimitives,NODE_TYPE_MIXED,geometryTypeRanges);
                                                                                                                                            
                             /* --- write to SLM frist --- */
                                                                      
                             const unsigned int local_leafDataID = local_leafID;
                             unsigned int node_blockID = 0;
                             for (unsigned int j=0;j<numChildren;j++)
                             {
                               const unsigned int index_j = BVH2Ploc::getIndex(indices[j]);
                               const unsigned int geomID = bvh2[index_j].left;
                               const unsigned int primID = bvh2[index_j].right;
                               const unsigned int bID = isFatLeaf ? (blockID + node_blockID) : innerID;
                               const bool isInstance   = geometryTypeRanges.isInstance(index_j);
                               local_leafGenData[local_leafDataID+j].blockID = bID;
                               local_leafGenData[local_leafDataID+j].primID = primID;
                               local_leafGenData[local_leafDataID+j].geomID = geomID;
                               node_blockID += isInstance  ? 2 : 1;
                             }
                                                                                                                                           
                             item.barrier(sycl::access::fence_space::local_space);

                             /* --- write out all local entries to global memory --- */
                                                                      
                             for (unsigned int i=localID;i<numLeaves;i+=localSize) 
                               leafGenData[leafID+i] = local_leafGenData[i]; 
                                                                      
                             if (localID == 0)
                             {                                                                        
                               const unsigned int syncID = gpu::atomic_add_global(&globals->sync,(unsigned int)1);
                               if (syncID == numGroups-1)
                               {
                                 /* --- reset atomics --- */
                                 globals->sync = 0;
                                 host_device_tasks[0] = globals->numLeaves;
                                 if (unlikely(globals->numLeaves > numPrimitives))
                                   host_device_tasks[0] = -1;
                               }
                             }
                             
                           });
		  
        });
      gpu::waitOnEventAndCatchException(queue_event);
      if (unlikely(verbose)) total_time += gpu::getDeviceExecutionTiming(queue_event);
    }    
    if (unlikely(host_device_tasks[0] == -1)) return false;
    
    /* ---- Phase IV: for each primID, geomID pair generate corresponding leaf data --- */
    const unsigned int leaves = host_device_tasks[0]; 
    
    if (leaves)
    {
      const unsigned int wgSize = 256;
      const sycl::nd_range<1> nd_range1(gpu::alignTo(leaves,wgSize),sycl::range<1>(wgSize));              
      sycl::event queue_event = gpu_queue.submit([&](sycl::handler &cgh) {
          cgh.parallel_for(nd_range1,[=](sycl::nd_item<1> item) EMBREE_SYCL_SIMD(16)      
                           {
                             const unsigned int globalID = item.get_global_id(0);
                             //const unsigned int localID  = item.get_local_id(0);
                             //bool valid = false;
                             QuadLeafData*qleaf = nullptr;
                                                                      
                             if (globalID < leaves)                                                                        
                             {
                               qleaf = (QuadLeafData *)globals->nodeBlockPtr(leafGenData[globalID].blockID);
                               const unsigned int geomID = leafGenData[globalID].geomID & GEOMID_MASK;
                               const _ze_raytracing_geometry_ext_desc_t *const geometryDesc = geometries[geomID];                                 

                               if (geometryDesc->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_TRIANGLES)
                               {
                                 
                                 // ====================                           
                                 // === TriangleMesh ===
                                 // ====================                                                                        
                                 //valid = true;                                   
                                 const unsigned int primID0 = leafGenData[globalID].primID;
                                 const unsigned int primID1 = primID0 + (leafGenData[globalID].geomID >> PAIR_OFFSET_SHIFT);                                   
                                  _ze_raytracing_geometry_triangles_ext_desc_t* triMesh = ( _ze_raytracing_geometry_triangles_ext_desc_t*)geometryDesc;
                                 const _ze_raytracing_triangle_indices_uint32_ext_t &tri = getTriangleDesc(*triMesh,primID0);
                                 const Vec3f p0 = getVec3f(*triMesh,tri.v0);
                                 const Vec3f p1 = getVec3f(*triMesh,tri.v1);
                                 const Vec3f p2 = getVec3f(*triMesh,tri.v2);                                       
                                 Vec3f p3 = p2;
                                 unsigned int lb0 = 0,lb1 = 0, lb2 = 0;
                                 
                                 /* handle paired triangle */
                                 if (primID0 != primID1)
                                 {
                                   const _ze_raytracing_triangle_indices_uint32_ext_t &tri1 = getTriangleDesc(*triMesh,primID1);          
                                   const unsigned int p3_index = try_pair_triangles(uint3(tri.v0,tri.v1,tri.v2),uint3(tri1.v0,tri1.v1,tri1.v2),lb0,lb1,lb2);
                                   p3 = getVec3f(*triMesh,((unsigned int*)&tri1)[p3_index]); // FIXME: might cause tri1 to store to mem first
                                 }
                                 *qleaf = QuadLeafData( p0,p1,p2,p3, lb0,lb1,lb2, 0, geomID, primID0, primID1, (GeometryFlags)triMesh->geometryFlags, triMesh->geometryMask);
                               }
                               else if (geometryDesc->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_QUADS)
                               {
                                 // ================                           
                                 // === QuadMesh ===
                                 // ================
                                 //valid = true;                                                                    
                                 const unsigned int primID0 = leafGenData[globalID].primID;                                   
                                 _ze_raytracing_geometry_quads_ext_desc_t* quadMesh = (_ze_raytracing_geometry_quads_ext_desc_t*)geometryDesc;                             
                                 const _ze_raytracing_quad_indices_uint32_ext_t &quad = getQuadDesc(*quadMesh,primID0);
                                 const Vec3f p0 = getVec3f(*quadMesh,quad.v0);
                                 const Vec3f p1 = getVec3f(*quadMesh,quad.v1);
                                 const Vec3f p2 = getVec3f(*quadMesh,quad.v2);                                       
                                 const Vec3f p3 = getVec3f(*quadMesh,quad.v3);                                                                          
                                 *qleaf = QuadLeafData( p0,p1,p3,p2, 3,2,1, 0, geomID, primID0, primID0, (GeometryFlags)quadMesh->geometryFlags, quadMesh->geometryMask);
                               }
                             
                               else if (geometryDesc->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_INSTANCE)
                               {
                                 // ================                           
                                 // === Instance ===
                                 // ================                                 
                                 const unsigned int instID = leafGenData[globalID].geomID;
                                 const _ze_raytracing_geometry_instance_ext_desc_t* instance = (const _ze_raytracing_geometry_instance_ext_desc_t*)geometryDesc;                                 
                                 InstancePrimitive *dest = (InstancePrimitive *)qleaf;         
                                 const AffineSpace3fa local2world = getTransform(instance);
                                 const uint64_t root = (uint64_t)instance->accel + QBVH6_HEADER_OFFSET;
                                 *dest = InstancePrimitive(local2world,root,instance->instanceUserID,instID,mask32_to_mask8(instance->geometryMask));
                               }
                               else if (geometryDesc->geometryType == ZE_RAYTRACING_GEOMETRY_TYPE_EXT_AABBS_FPTR)
                               {
                                 // ==================                           
                                 // === Procedural ===
                                 // ==================                                
                                 const unsigned int primID0 = leafGenData[globalID].primID;                                 
                                 const _ze_raytracing_geometry_aabbs_fptr_ext_desc_t* geom = (const _ze_raytracing_geometry_aabbs_fptr_ext_desc_t*)geometryDesc;                                 
                                 const unsigned int mask32 = mask32_to_mask8(geom->geometryMask);
                                 ProceduralLeaf *dest = (ProceduralLeaf *)qleaf;
                                 PrimLeafDesc leafDesc(0,geomID,GeometryFlags::NONE /*(GeometryFlags)geom->geometryFlags*/,mask32,PrimLeafDesc::TYPE_OPACITY_CULLING_ENABLED);
                                 *dest = ProceduralLeaf(leafDesc,primID0,true);
                               }
                             }

                           /* ================================== */                                                                      
                           /* === write out to global memory === */
                           /* ================================== */
                                                                      
                           /* const unsigned int subgroupLocalID = get_sub_group_local_id(); */
                           /* unsigned int mask = sub_group_ballot(valid); */
                           /* while(mask) */
                           /* { */
                           /*   const unsigned int index = sycl::ctz(mask); */
                           /*   mask &= mask-1; */
                           /*   unsigned int ID = sub_group_broadcast(localID,index); */
                           /*   unsigned int* dest = sub_group_broadcast((unsigned int*)qleaf,index); */
                           /*   unsigned int* source = (unsigned int*)&localLeaf[ID]; */
                           /*   const unsigned int v = source[subgroupLocalID]; */
                           /*   sub_group_store(dest,v); */
                           /* } */
                             
                           });
		  
        });
      gpu::waitOnEventAndCatchException(queue_event);
      if (unlikely(verbose)) total_time += gpu::getDeviceExecutionTiming(queue_event);     
    }    
    
    conversion_device_time = (float)total_time;
    return true;    
  }
  
    
}

#endif
