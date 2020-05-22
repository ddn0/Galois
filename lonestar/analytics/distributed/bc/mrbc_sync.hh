/*
 * This file belongs to the Galois project, a C++ library for exploiting
 * parallelism. The code is being released under the terms of the 3-Clause BSD
 * License (a copy is located in LICENSE.txt at the top-level directory).
 *
 * Copyright (C) 2018, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 */

#include "galois/runtime/SyncStructures.h"

////////////////////////////////////////////////////////////////////////////////
// APSP synchronization
////////////////////////////////////////////////////////////////////////////////

struct APSPReduce {
  using ValTy = galois::TupleOfThree<uint32_t, uint32_t, ShortPathType>;

  template <typename Graph>
  static ValTy extract(uint32_t node_id, Graph& g) {
    auto& sourceData = g.template getDataIndex<NODE_DATA_SOURCE_DATA>(node_id);
    auto& roundIndexToSend =
        g.template getDataIndex<NODE_DATA_ROUND_INDEX_TO_SEND>(node_id);

    uint32_t indexToGet = roundIndexToSend;

    uint32_t a;
    uint32_t b;
    ShortPathType c;

    a = indexToGet;
    if (indexToGet != infinity) {
      // get min distance and # shortest paths
      b = sourceData[indexToGet].minDistance;
      c = sourceData[indexToGet].shortPathCount;
    } else {
      // no-op
      b = infinity;
      c = 0;
    }

    return ValTy(a, b, c);
  }

  static bool extract_batch(unsigned, uint8_t*, size_t*, DataCommMode*) {
    return false;
  }

  static bool extract_batch(unsigned, uint8_t*) { return false; }

  static bool extract_reset_batch(unsigned, uint8_t*, size_t*, DataCommMode*) {
    return false;
  }

  static bool extract_reset_batch(unsigned, uint8_t*) { return false; }

  template <typename Graph>
  static bool reduce(uint32_t node_id, Graph& g, ValTy y) {
    auto& sourceData = g.template getDataIndex<NODE_DATA_SOURCE_DATA>(node_id);
    auto& roundIndexToSend =
        g.template getDataIndex<NODE_DATA_ROUND_INDEX_TO_SEND>(node_id);
    auto& dTree = g.template getDataIndex<NODE_DATA_D_TREE>(node_id);

    uint32_t rIndex = y.first;

    if (rIndex != infinity) {
      uint32_t rDistance      = y.second;
      ShortPathType rNumPaths = y.third;

      // do updates based on received numbers
      uint32_t old = galois::min(sourceData[rIndex].minDistance, rDistance);

      // reset shortest paths if min dist changed (i.e. don't add to it)
      if (old > rDistance) {
        dTree.setDistance(rIndex, old, rDistance);
        assert(rNumPaths != 0);
        sourceData[rIndex].shortPathCount = rNumPaths;
      } else if (old == rDistance) {
        // add to short path
        sourceData[rIndex].shortPathCount += rNumPaths;
      }

      // if received distance is smaller than current candidate for sending,
      // send it out instead (if tie breaker wins i.e. lower in position)
      if (roundIndexToSend == infinity ||
          (sourceData[rIndex].minDistance <
           sourceData[roundIndexToSend].minDistance)) {
        roundIndexToSend = rIndex;
      } else if (sourceData[rIndex].minDistance ==
                 sourceData[roundIndexToSend].minDistance) {
        if (rIndex < roundIndexToSend) {
          roundIndexToSend = rIndex;
        }
      }

      // return true: if it received a message for some node, then that
      // node on a mirror needs to get the most updated value (i.e. value on
      // master)
      return true;
    }

    return false;
  }

  static bool reduce_batch(unsigned, uint8_t*, DataCommMode) { return false; }

  static bool reduce_mirror_batch(unsigned, uint8_t*, DataCommMode) {
    return false;
  }

  // reset the number of shortest paths (the master will now have it)
  template <typename Graph>
  static void reset(uint32_t node_id, Graph& g) {
    auto& sourceData = g.template getDataIndex<NODE_DATA_SOURCE_DATA>(node_id);
    auto& roundIndexToSend =
        g.template getDataIndex<NODE_DATA_ROUND_INDEX_TO_SEND>(node_id);

    if (roundIndexToSend != infinity) {
      sourceData[roundIndexToSend].shortPathCount = 0;
    }
  }

  template <typename Graph>
  static void setVal(uint32_t node_id, Graph& g, ValTy y) {
    auto& sourceData = g.template getDataIndex<NODE_DATA_SOURCE_DATA>(node_id);
    auto& roundIndexToSend =
        g.template getDataIndex<NODE_DATA_ROUND_INDEX_TO_SEND>(node_id);
    auto& dTree = g.template getDataIndex<NODE_DATA_D_TREE>(node_id);

    uint32_t rIndex = y.first;
    if (rIndex != infinity) {
      uint32_t rDistance      = y.second;
      ShortPathType rNumPaths = y.third;

      // values from master are canonical ones for this round
      roundIndexToSend                  = rIndex;
      uint32_t oldDistance              = sourceData[rIndex].minDistance;
      sourceData[rIndex].minDistance    = rDistance;
      sourceData[rIndex].shortPathCount = rNumPaths;
      dTree.setDistance(rIndex, oldDistance, rDistance);
    }
  }

  static bool setVal_batch(unsigned, uint8_t*, DataCommMode) { return false; }
};

////////////////////////////////////////////////////////////////////////////////

struct DependencyReduce {
  using ValTy = galois::Pair<uint32_t, float>;

  template <typename Graph>
  static ValTy extract(uint32_t node_id, Graph& g) {
    auto& sourceData = g.template getDataIndex<NODE_DATA_SOURCE_DATA>(node_id);
    auto& roundIndexToSend =
        g.template getDataIndex<NODE_DATA_ROUND_INDEX_TO_SEND>(node_id);

    uint32_t indexToGet = roundIndexToSend;
    float thing;
    if (indexToGet != infinity) {
      thing = sourceData[indexToGet].dependencyValue;
    } else {
      thing = 0;
    }

    return ValTy(indexToGet, thing);
  }

  static bool extract_batch(unsigned, uint8_t*, size_t*, DataCommMode*) {
    return false;
  }

  static bool extract_batch(unsigned, uint8_t*) { return false; }

  static bool extract_reset_batch(unsigned, uint8_t*, size_t*, DataCommMode*) {
    return false;
  }

  static bool extract_reset_batch(unsigned, uint8_t*) { return false; }

  template <typename Graph>
  static bool reduce(uint32_t node_id, Graph& g, ValTy y) {
    auto& sourceData = g.template getDataIndex<NODE_DATA_SOURCE_DATA>(node_id);
    auto& roundIndexToSend =
        g.template getDataIndex<NODE_DATA_ROUND_INDEX_TO_SEND>(node_id);

    uint32_t rIndex = y.first;

    if (rIndex != infinity) {
      if (roundIndexToSend != rIndex) {
        galois::gError(roundIndexToSend, " ", rIndex);
      }
      assert(roundIndexToSend == rIndex);

      float rToAdd = y.second;
      galois::atomicAdd(sourceData[rIndex].dependencyValue, rToAdd);
      return true;
    }

    return false;
  }

  static bool reduce_batch(unsigned, uint8_t*, DataCommMode) { return false; }

  static bool reduce_mirror_batch(unsigned, uint8_t*, DataCommMode) {
    return false;
  }

  // reset the number of shortest paths (the master will now have it)
  template <typename Graph>
  static void reset(uint32_t node_id, Graph& g) {
    auto& sourceData = g.template getDataIndex<NODE_DATA_SOURCE_DATA>(node_id);
    auto& roundIndexToSend =
        g.template getDataIndex<NODE_DATA_ROUND_INDEX_TO_SEND>(node_id);

    if (roundIndexToSend != infinity) {
      sourceData[roundIndexToSend].dependencyValue = 0;
    }
  }

  template <typename Graph>
  static void setVal(uint32_t node_id, Graph& g, ValTy y) {
    auto& sourceData = g.template getDataIndex<NODE_DATA_SOURCE_DATA>(node_id);

    uint32_t rIndex = y.first;
    if (rIndex != infinity) {
      float rDep = y.second;
      assert(g.template getDataIndex<NODE_DATA_ROUND_INDEX_TO_SEND>(node_id) == rIndex);
      sourceData[rIndex].dependencyValue = rDep;
    }
  }

  static bool setVal_batch(unsigned, uint8_t*, DataCommMode) { return false; }
};

////////////////////////////////////////////////////////////////////////////////
// Bitsets
////////////////////////////////////////////////////////////////////////////////

GALOIS_SYNC_STRUCTURE_BITSET(minDistances);
GALOIS_SYNC_STRUCTURE_BITSET(dependency);
