//
//  GDAMinimumRule.h
//  Camellia-debug
//
//  Created by Nate Roberts on 1/21/14.
//
//

#ifndef __Camellia_debug__GDAMinimumRule__
#define __Camellia_debug__GDAMinimumRule__

#include <iostream>

#include "GlobalDofAssignment.h"

class GDAMinimumRule : public GlobalDofAssignment {
public:
  GDAMinimumRule(MeshTopologyPtr meshTopology, VarFactory varFactory, DofOrderingFactoryPtr dofOrderingFactory, MeshPartitionPolicyPtr partitionPolicy,
                 unsigned initialH1OrderTrial, unsigned testOrderEnhancement);
  
  void didHRefine(const set<int> &parentCellIDs);
  void didPRefine(const set<int> &cellIDs, int deltaP);
  void didHUnrefine(const set<int> &parentCellIDs);
  
  void didChangePartitionPolicy();
  
  ElementTypePtr elementType(unsigned cellID);
  unsigned globalDofCount();
  unsigned localDofCount(); // local to the MPI node
  
  void rebuildLookups();
};

#endif /* defined(__Camellia_debug__GDAMinimumRule__) */
