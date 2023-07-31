// ======================================================================== //
// Copyright 2022-2023 Ingo Wald                                            //
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

#include "cukd/common.h"
#include "cukd/helpers.h"
#include "cukd/fcp.h"

namespace cukd {

  /*! list that stores the respectively k nearest candidates during
      knn traversal, in this case using a simple linear sorted list of
      k elements. Inserting into this list costs O(k), but can
      probably be unrolled and kept in registers (unlike the heap
      based variant below), and should thus be faster for small k than
      using the heav varaitn - but it'll be painfully slow for larger
      k */
  template<int k>
  struct FixedCandidateList
  {
    inline __device__ float returnValue() const { return maxRadius2(); }
    inline __device__ float processCandidate(int candPrimID, float candDist2)
    { push(candDist2,candPrimID); return maxRadius2(); }
    inline __device__ float initialCullDist2() const { return maxRadius2(); }


    inline __device__ uint64_t encode(float f, int i)
    {
      return (uint64_t(__float_as_uint(f)) << 32) | uint32_t(i);
    }
    
    inline __device__ FixedCandidateList(float cutOffRadius)
    {
#pragma unroll
      for (int i=0;i<k;i++)
        entry[i] = encode(cutOffRadius*cutOffRadius,-1);
    }

    inline __device__ void push(float dist, int pointID)
    {
      uint64_t v = encode(dist,pointID);
#pragma unroll
      for (int i=0;i<k;i++) {
        uint64_t vmax = max(entry[i],v);
        uint64_t vmin = min(entry[i],v);
        entry[i] = vmin;
        v = vmax;
      }
    }

    inline __device__ float get_dist2(int i) const { return decode_dist2(entry[i]); }
    inline __device__ int get_pointID(int i) const { return decode_pointID(entry[i]); }
    
    inline __device__ float maxRadius2() const
    { return decode_dist2(entry[k-1]); }
    
    inline __device__ float decode_dist2(uint64_t v) const
    { return __uint_as_float(v >> 32); }
    inline __device__ int decode_pointID(uint64_t v) const
    { return int(v); }

    uint64_t entry[k];
    enum { num_k = k };
  };

  template<int k>
  struct HeapCandidateList
  {
    // to make the traverseal lambdas happy:
    inline __device__ float returnValue() const { return maxRadius2(); }
    inline __device__ float processCandidate(int candPrimID, float candDist2)
    { push(candDist2,candPrimID); return maxRadius2(); }
    inline __device__ float initialCullDist2() const { return maxRadius2(); }
    
    inline __device__ float get_dist2(int i) const { return decode_dist2(entry[i]); }
    inline __device__ int get_pointID(int i) const { return decode_pointID(entry[i]); }


    inline __device__ uint64_t encode(float f, int i)
    {
      return (uint64_t(__float_as_uint(f)) << 32) | uint32_t(i);
    }
    
    inline __device__ HeapCandidateList(float cutOffRadius)
    {
#pragma unroll
      for (int i=0;i<k;i++)
        entry[i] = encode(cutOffRadius*cutOffRadius,-1);
    }

    inline __device__ void push(float dist, int pointID)
    {
      uint64_t e = encode(dist,pointID);
      if (e >= entry[0]) return;

      int pos = 0;
      while (true) {
        uint64_t largestChildValue = uint64_t(-1);
        int firstChild = 2*pos+1;
        int largestChild = k;
        if (firstChild < k) {
          largestChild = firstChild;
          largestChildValue = entry[firstChild];
        }
        
        int secondChild = firstChild+1;
        if (secondChild < k && entry[secondChild] > largestChildValue) {
          largestChild = secondChild;
          largestChildValue = entry[secondChild];
        }

        if (largestChild == k || largestChildValue < e) {
          entry[pos] = e;
          break;
        } else {
          entry[pos] = largestChildValue;
          pos = largestChild;
        }
      }
    }
    
    inline __device__ float maxRadius2() const
    { return decode_dist2(entry[0]); }
    
    inline __device__ float decode_dist2(uint64_t v) const
    { return __uint_as_float(v >> 32); }
    inline __device__ int decode_pointID(uint64_t v) const
    { return int(v); }

    uint64_t entry[k];
    enum { num_k = k };
  };

} // ::cukd






#if CUKD_IMPROVED_TRAVERSAL
# if CUKD_STACK_FREE
// stack-free, improved traversal
#  include "traverse-sf-imp.h"
namespace cukd {
  template<typename CandidateList,
           typename node_t,
           typename node_traits=default_node_traits<node_t>>
  inline __device__
  float knn(CandidateList &result,
            typename node_traits::point_t queryPoint,
            const box_t<typename node_traits::point_t> worldBounds,
            const node_t *d_nodes,
            int N)
  {
    traverse_sf_imp<CandidateList,node_t,node_traits>
      (result,queryPoint,worldBounds,d_nodes,N);
    return result.returnValue();
  }
} // :: cukd

# else
// stack-free, improved traversal
#  include "traverse-cct.h"
namespace cukd {
  template<typename CandidateList,
           typename node_t,
           typename node_traits=default_node_traits<node_t>>
  inline __device__
  float knn(CandidateList &result,
            typename node_traits::point_t queryPoint,
            const box_t<typename node_traits::point_t> worldBounds,
            const node_t *d_nodes,
            int N)
  {
    traverse_cct<CandidateList,node_t,node_traits>
      (result,queryPoint,worldBounds,d_nodes,N);
    return result.returnValue();
  }
} // :: cukd

# endif
#else
# if CUKD_STACK_FREE
// stack-free, regular traversal
#  include "traverse-stack-free.h"
namespace cukd {
  template<typename CandidateList,
           typename node_t,
           typename node_traits=default_node_traits<node_t>>
  inline __device__
  float knn(CandidateList &result,
            typename node_traits::point_t queryPoint,
            const node_t *d_nodes,
            int N)
  {
    traverse_stack_free<CandidateList,node_t,node_traits>
      (result,queryPoint,d_nodes,N);
    return result.returnValue();
  }
} // :: cukd
# else
// default stack-based traversal
#  include "traverse-default-stack-based.h"
namespace cukd {
  template<typename CandidateList,
           typename node_t,
           typename node_traits=default_node_traits<node_t>>
  inline __device__
  float knn(CandidateList &result,
            typename node_traits::point_t queryPoint,
            const node_t *d_nodes,
            int N)
  {
    traverse_default<CandidateList,node_t,node_traits>
      (result,queryPoint,d_nodes,N);
    return result.returnValue();
  }
  
} // :: cukd

# endif
#endif



namespace cukd {
#if CUKD_IMPROVED_TRAVERSAL
  template<typename CandidateList,
           typename data_t,
           typename node_traits=default_node_traits<data_t>>
  inline __device__
  float knn(CandidateList &result,
            const SpatialKDTree<data_t,node_traits> &tree,
            typename node_traits::point_t queryPoint)
  {
    using node_t     = typename SpatialKDTree<data_t,node_traits>::Node;
    using point_t    = typename node_traits::point_t;
    using scalar_t   = typename scalar_type_of<point_t>::type;
    enum { num_dims  = num_dims_of<point_t>::value };
    
    scalar_t cullDist = result.initialCullDist2();

    /* can do at most 2**30 points... */
    struct StackEntry {
      int   nodeID;
      point_t closestCorner;
    };
    StackEntry stackBase[30];
    StackEntry *stackPtr = stackBase;

    /*! current node in the tree we're traversing */
    int nodeID = 0;
    point_t closestPointOnSubtreeBounds = project(tree.bounds,queryPoint);
    if (sqrDistance(queryPoint,closestPointOnSubtreeBounds) > cullDist)
      return result.returnValue();
    node_t node;
    while (true) {
      while (true) {
        CUKD_STATS(if (cukd::g_traversalStats) ::atomicAdd(cukd::g_traversalStats,1));
        node = tree.nodes[nodeID];
        if (node.count)
          // this is a leaf...
          break;
        const auto query_coord = get_coord(queryPoint,node.dim);
        const bool leftIsClose = query_coord < node.pos;
        const int  lChild = node.offset+0;
        const int  rChild = node.offset+1;

        const int closeChild = leftIsClose?lChild:rChild;
        const int farChild   = leftIsClose?rChild:lChild;

        auto farSideCorner = closestPointOnSubtreeBounds;
        get_coord(farSideCorner,node.dim) = node.pos;
        
        if (sqrDistance(farSideCorner,queryPoint) < cullDist) {
          stackPtr->closestCorner = farSideCorner;
          stackPtr->nodeID  = farChild;
          ++stackPtr;
        }
        nodeID = closeChild;
      }

      for (int i=0;i<node.count;i++) {
        int primID = tree.primIDs[node.offset+i];
        CUKD_STATS(if (cukd::g_traversalStats) ::atomicAdd(cukd::g_traversalStats,1));
        const auto sqrDist = sqrDistance(node_traits::get_point(tree.data[primID]),queryPoint);
        cullDist = result.processCandidate(primID,sqrDist);
      }
      
      while (true) {
        if (stackPtr == stackBase) 
          return result.returnValue();
        --stackPtr;
        closestPointOnSubtreeBounds = stackPtr->closestCorner;
        if (sqrDistance(closestPointOnSubtreeBounds,queryPoint) >= cullDist)
          continue;
        nodeID = stackPtr->nodeID;
        break;
      }
    }
  }
#else
  template<typename CandidateList,
           typename data_t,
           typename node_traits=default_node_traits<data_t>>
  inline __device__
  float knn(CandidateList &result,
            const SpatialKDTree<data_t,node_traits> &tree,
            typename node_traits::point_t queryPoint)
  {
    using node_t     = typename SpatialKDTree<data_t,node_traits>::Node;
    using point_t    = typename node_traits::point_t;
    using scalar_t   = typename scalar_type_of<point_t>::type;
    enum { num_dims  = num_dims_of<point_t>::value };
    
    scalar_t cullDist = result.initialCullDist2();

    /* can do at most 2**30 points... */
    struct StackEntry {
      int   nodeID;
      float sqrDist;
    };
    StackEntry stackBase[30];
    StackEntry *stackPtr = stackBase;

    /*! current node in the tree we're traversing */
    int nodeID = 0;
    node_t node;
    while (true) {
      while (true) {
        CUKD_STATS(if (cukd::g_traversalStats) ::atomicAdd(cukd::g_traversalStats,1));
        node = tree.nodes[nodeID];
        if (node.count)
          // this is a leaf...
          break;
        const auto query_coord = get_coord(queryPoint,node.dim);
        const bool leftIsClose = query_coord < node.pos;
        const int  lChild = node.offset+0;
        const int  rChild = node.offset+1;

        const int closeChild = leftIsClose?lChild:rChild;
        const int farChild   = leftIsClose?rChild:lChild;
        
        const float sqrDistToPlane = sqr(query_coord - node.pos);
        if (sqrDistToPlane < cullDist) {
          stackPtr->nodeID  = farChild;
          stackPtr->sqrDist = sqrDistToPlane;
          ++stackPtr;
        }
        nodeID = closeChild;
      }

      for (int i=0;i<node.count;i++) {
        int primID = tree.primIDs[node.offset+i];
        CUKD_STATS(if (cukd::g_traversalStats) ::atomicAdd(cukd::g_traversalStats,1));
        const auto sqrDist = sqrDistance(node_traits::get_point(tree.data[primID]),queryPoint);
        cullDist = result.processCandidate(primID,sqrDist);
      }
      
      while (true) {
        if (stackPtr == stackBase) 
          return result.returnValue();
        --stackPtr;
        if (stackPtr->sqrDist >= cullDist)
          continue;
        nodeID = stackPtr->nodeID;
        break;
      }
    }
  }
#endif
} // :: cukd
