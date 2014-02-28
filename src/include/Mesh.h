#ifndef DPG_MESH
#define DPG_MESH

// @HEADER
//
// Original version Copyright © 2011 Sandia Corporation. All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice, this list of
// conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of
// conditions and the following disclaimer in the documentation and/or other materials
// provided with the distribution.
// 3. The name of the author may not be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
// OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Nate Roberts (nate@nateroberts.com).
//
// @HEADER

/*
 *  Mesh.h
 *
 *  Created by Nathan Roberts on 6/27/11.
 *
 */

// Intrepid includes
#include "Intrepid_FieldContainer.hpp"

// Epetra includes
#include <Epetra_Map.h>
#ifdef HAVE_MPI
#include "Epetra_MpiComm.h"
#else
#include "Epetra_SerialComm.h"
#endif

#include "Epetra_Vector.h"

#include "ElementType.h"
#include "ElementTypeFactory.h"
#include "Element.h"
#include "Boundary.h"
#include "BilinearForm.h"
#include "DofOrderingFactory.h"
#include "RefinementPattern.h"
#include "MeshPartitionPolicy.h"

#include "RefinementObserver.h"

#include "Function.h"
#include "ParametricCurve.h"

#include "MeshGeometry.h"

#include "MeshTopology.h"

#include "IndexType.h"

class Mesh;
typedef Teuchos::RCP<Mesh> MeshPtr;

class Solution;
class MeshTransformationFunction;
class MeshPartitionPolicy;

#include "DofInterpreter.h"
#include "GDAMaximumRule2D.h"

class Mesh : public RefinementObserver, public DofInterpreter {
  MeshTopologyPtr _meshTopology;
  
  Teuchos::RCP<GlobalDofAssignment> _gda;
  
//  Teuchos::RCP<GDAMaximumRule2D> _maximumRule2D;
  
  int _pToAddToTest;
  bool _enforceMBFluxContinuity; // default to false (the historical value)
  bool _usePatchBasis; // use MultiBasis if this is false.
  bool _useConformingTraces; // if true, enforces vertex trace continuity
  
  BilinearFormPtr _bilinearForm;
  // for now, just a uniform mesh, with a rectangular boundary and elements.
  Boundary _boundary;

  //set< pair<int,int> > _edges;
  map< pair<GlobalIndexType,GlobalIndexType>, vector< pair<GlobalIndexType, GlobalIndexType> > > _edgeToCellIDs; //keys are (vertexIndex1, vertexIndex2)
                                                                  //values are (cellID, sideIndex)
                                                                  //( will need to do something else in 3D )
  vector< vector<int> > _cellSideParitiesForCellID;

  // keep track of upgrades to the sides of cells since the last rebuild:
  // (used to remap solution coefficients)
  map< GlobalIndexType, pair< ElementTypePtr, ElementTypePtr > > _cellSideUpgrades; // cellID --> (oldType, newType)

//  map< pair<GlobalIndexType,IndexType>, pair<GlobalIndexType,IndexType> > _dofPairingIndex; // key/values are (cellID,localDofIndex)
  // note that the FieldContainer for cellSideParities has dimensions (numCellsForType,numSidesForType),
  // and that the values are 1.0 or -1.0.  These are weights to account for the fact that fluxes are defined in
  // terms of an outward normal, and thus one cell's idea about the flux is the negative of its neighbor's.
  // We decide parity by cellID: the neighbor with the lower cellID gets +1, the higher gets -1.

//  // call buildTypeLookups to rebuild the elementType data structures:
//  vector< map< ElementType*, vector<GlobalIndexType> > > _cellIDsForElementType;
//  map< ElementType*, map<GlobalIndexType, GlobalIndexType> > _globalCellIndexToCellID;
//  vector< vector< ElementTypePtr > > _elementTypesForPartition;
//  vector< ElementTypePtr > _elementTypes;
//  map<GlobalIndexType, PartitionIndexType> _partitionForCellID;
//  map<GlobalIndexType, PartitionIndexType> _partitionForGlobalDofIndex;
//  map<GlobalIndexType, PartitionIndexType> _partitionLocalIndexForGlobalDofIndex;
//  vector< map< ElementType*, FieldContainer<double> > > _partitionedPhysicalCellNodesForElementType;
//  vector< map< ElementType*, FieldContainer<double> > > _partitionedCellSideParitiesForElementType;
//  map< ElementType*, FieldContainer<double> > _physicalCellNodesForElementType; // for uniform mesh, just a single entry..
//  vector< set<GlobalIndexType> > _partitionedGlobalDofIndices;

  vector< Teuchos::RCP<RefinementObserver> > _registeredObservers; // meshes that should be modified upon refinement (must differ from this only in bilinearForm; must have identical geometry & cellIDs)

  map<IndexType, GlobalIndexType> getGlobalVertexIDs(const FieldContainer<double> &vertexCoordinates);

  ElementPtr _nullPtr;

  ElementPtr addElement(const vector<IndexType> & vertexIndices, ElementTypePtr elemType);
  void addChildren(ElementPtr parent, vector< vector<IndexType> > &children,
                   vector< vector< pair< IndexType, IndexType> > > &childrenForSide);

  FieldContainer<double> physicalCellNodes( Teuchos::RCP< ElementType > elemTypePtr, vector<GlobalIndexType> &cellIDs );
  
  void setElementType(GlobalIndexType cellID, ElementTypePtr newType, bool sideUpgradeOnly);

  void setNeighbor(ElementPtr elemPtr, unsigned elemSide, ElementPtr neighborPtr, unsigned neighborSide);

  GlobalIndexType getVertexIndex(double x, double y, double tol=1e-14);
  
  void verticesForCells(FieldContainer<double>& vertices, vector<GlobalIndexType> &cellIDs);

  static map<int,int> _emptyIntIntMap; // just defined here to implement a default argument to constructor (there's got to be a better way)
public:
  // legacy (max rule 2D) constructor:
  Mesh(const vector<vector<double> > &vertices, vector< vector<IndexType> > &elementVertices,
       Teuchos::RCP< BilinearForm > bilinearForm, int H1Order, int pToAddTest, bool useConformingTraces = true,
       map<int,int> trialOrderEnhancements=_emptyIntIntMap, map<int,int> testOrderEnhancements=_emptyIntIntMap);
  
  // new constructor (min rule, n-D):
  Mesh(MeshTopologyPtr meshTopology, BilinearFormPtr bilinearForm, int H1Order, int pToAddTest,
       map<int,int> trialOrderEnhancements=_emptyIntIntMap, map<int,int> testOrderEnhancements=_emptyIntIntMap);
  
  // deprecated static constructors (use MeshFactory methods instead):
  static Teuchos::RCP<Mesh> readMsh(string filePath, Teuchos::RCP< BilinearForm > bilinearForm, int H1Order, int pToAdd);

  static Teuchos::RCP<Mesh> readTriangle(string filePath, Teuchos::RCP< BilinearForm > bilinearForm, int H1Order, int pToAdd);

  static Teuchos::RCP<Mesh> buildQuadMesh(const FieldContainer<double> &quadBoundaryPoints,
                                          int horizontalElements, int verticalElements,
                                          Teuchos::RCP< BilinearForm > bilinearForm,
                                          int H1Order, int pTest, bool triangulate=false, bool useConformingTraces=true,
                                          map<int,int> trialOrderEnhancements=_emptyIntIntMap,
                                          map<int,int> testOrderEnhancements=_emptyIntIntMap);
  static Teuchos::RCP<Mesh> buildQuadMeshHybrid(const FieldContainer<double> &quadBoundaryPoints,
                                                int horizontalElements, int verticalElements,
                                                Teuchos::RCP< BilinearForm > bilinearForm,
                                                int H1Order, int pTest, bool useConformingTraces=true);
  static void quadMeshCellIDs(FieldContainer<int> &cellIDs,
                              int horizontalElements, int verticalElements,
                              bool useTriangles);

  GlobalIndexType activeCellOffset();

  bool cellContainsPoint(GlobalIndexType cellID, vector<double> &point);
  
  FieldContainer<double> cellSideParities( ElementTypePtr elemTypePtr);
  FieldContainer<double> cellSideParitiesForCell( GlobalIndexType cellID );

  Teuchos::RCP<BilinearForm> bilinearForm();
  void setBilinearForm( Teuchos::RCP<BilinearForm>);

  vector<ElementPtr> elementsForPoints(const FieldContainer<double> &physicalPoints);

  vector< Teuchos::RCP< ElementType > > elementTypes(PartitionIndexType partitionNumber=-1); // returns *all* elementTypes by default

  Boundary &boundary();

  GlobalIndexType cellID(ElementTypePtr elemTypePtr, IndexType cellIndex, PartitionIndexType partitionNumber=-1);

  vector< GlobalIndexType > cellIDsOfType(ElementTypePtr elemType); // for current MPI node.
  vector< GlobalIndexType > cellIDsOfType(int partitionNumber, ElementTypePtr elemTypePtr);
  vector< GlobalIndexType > cellIDsOfTypeGlobal(ElementTypePtr elemTypePtr);

  int cellPolyOrder(GlobalIndexType cellID);

  void enforceOneIrregularity();
//  void enforceOneIrregularity(vector< Teuchos::RCP<Solution> > solutions);

  vector<double> getCellCentroid(GlobalIndexType cellID);

  // commented out because unused
  //Epetra_Map getCellIDPartitionMap(int rank, Epetra_Comm* Comm);

  ElementPtr getElement(GlobalIndexType cellID);
  ElementTypePtr getElementType(GlobalIndexType cellID);

  const map< pair<GlobalIndexType,IndexType> , GlobalIndexType>& getLocalToGlobalMap();
  //  map< int, pair<int,int> > getGlobalToLocalMap();

  GlobalIndexType globalDofIndex(GlobalIndexType cellID, IndexType localDofIndex);
  set<GlobalIndexType> globalDofIndicesForPartition(PartitionIndexType partitionNumber);
  
  GlobalDofAssignmentPtr globalDofAssignment();

  set<GlobalIndexType> getActiveCellIDs();
  vector< ElementPtr > activeElements();  // deprecated -- use getActiveElement instead
  ElementPtr ancestralNeighborForSide(ElementPtr elem, int sideOrdinal, int &elemSideOrdinalInNeighbor);

  GlobalIndexType numEdgeToCellIDEntries(){
    return _edgeToCellIDs.size();
  }

  vector< ElementPtr > elementsOfType(PartitionIndexType partitionNumber, ElementTypePtr elemTypePtr);
  vector< ElementPtr > elementsOfTypeGlobal(ElementTypePtr elemTypePtr); // may want to deprecate in favor of cellIDsOfTypeGlobal()

  vector< ElementPtr > elementsInPartition(PartitionIndexType partitionNumber = -1);

  int getDimension(); // spatial dimension of the mesh
  DofOrderingFactory & getDofOrderingFactory();

  ElementTypeFactory & getElementTypeFactory();
//  void getMultiBasisOrdering(DofOrderingPtr &originalNonParentOrdering,
//                             ElementPtr parent, int sideIndex, int parentSideIndexInNeighbor,
//                             ElementPtr nonParent);
//
//  void getPatchBasisOrdering(DofOrderingPtr &originalChildOrdering, ElementPtr child, int sideIndex);
  FunctionPtr getTransformationFunction(); // will be NULL for meshes without edge curves defined

  void hRefine(const set<GlobalIndexType> &cellIDs, Teuchos::RCP<RefinementPattern> refPattern);

  void hRefine(const vector<GlobalIndexType> &cellIDs, Teuchos::RCP<RefinementPattern> refPattern);
  void hUnrefine(const set<GlobalIndexType> &cellIDs);
  
  void interpretGlobalData(GlobalIndexType cellID, FieldContainer<double> &localData, const Epetra_Vector &globalData, bool accumulate=true);
  void interpretLocalBasisData(GlobalIndexType cellID, int varID, int sideOrdinal, const FieldContainer<double> &basisDofs,
                                       FieldContainer<double> &globalDofs, FieldContainer<GlobalIndexType> &globalDofIndices);
  void interpretLocalData(GlobalIndexType cellID, const FieldContainer<double> &localData,
                          FieldContainer<double> &globalData, FieldContainer<GlobalIndexType> &globalDofIndices, bool accumulate=true);
  
  // for the case where we want to reproject the previous mesh solution onto the new one:
//  void hRefine(vector<GlobalIndexType> cellIDs, Teuchos::RCP<RefinementPattern> refPattern, vector< Teuchos::RCP<Solution> > solutions);

//  void matchNeighbor(const ElementPtr &elem, int sideIndex);

//  void maxMinPolyOrder(int &maxPolyOrder, int &minPolyOrder, ElementPtr elem, int sideIndex);

//  map< int, BasisPtr > multiBasisUpgradeMap(ElementPtr parent, int sideIndex, int bigNeighborPolyOrder = -1);

//  static int neighborChildPermutation(int childIndex, int numChildrenInSide);
//  static int neighborDofPermutation(int dofIndex, int numDofsForSide);

  GlobalIndexType numActiveElements();

  GlobalIndexType numFluxDofs();
  GlobalIndexType numFieldDofs();

  GlobalIndexType numGlobalDofs();

  GlobalIndexType numElements();

  GlobalIndexType numElementsOfType( Teuchos::RCP< ElementType > elemTypePtr );

  GlobalIndexType numInitialElements();

  int parityForSide(GlobalIndexType cellID, int sideOrdinal);

  PartitionIndexType partitionForCellID(GlobalIndexType cellID);
  PartitionIndexType partitionForGlobalDofIndex( GlobalIndexType globalDofIndex );
  PartitionIndexType partitionLocalIndexForGlobalDofIndex( GlobalIndexType globalDofIndex );

  FieldContainer<double> physicalCellNodes( ElementTypePtr elemType);
  FieldContainer<double> physicalCellNodesForCell(GlobalIndexType cellID);
  FieldContainer<double> physicalCellNodesGlobal( ElementTypePtr elemType );

  void pRefine(const vector<GlobalIndexType> &cellIDsForPRefinements);
  void pRefine(const vector<GlobalIndexType> &cellIDsForPRefinements, int pToAdd);
  void pRefine(const set<GlobalIndexType> &cellIDsForPRefinements);
  void pRefine(const set<GlobalIndexType> &cellIDsForPRefinements, int pToAdd); // added by jesse
  void printLocalToGlobalMap(); // for debugging
  void printVertices(); // for debugging

  void rebuildLookups();

  void registerObserver(Teuchos::RCP<RefinementObserver> observer);

  void registerSolution(Teuchos::RCP<Solution> solution);

  int condensedRowSizeUpperBound();
  int rowSizeUpperBound(); // accounts for multiplicity, but isn't a tight bound

  void setEdgeToCurveMap(const map< pair<GlobalIndexType, GlobalIndexType>, ParametricCurvePtr > &edgeToCurveMap);
  void setEnforceMultiBasisFluxContinuity( bool value );

  vector< ParametricCurvePtr > parametricEdgesForCell(GlobalIndexType cellID, bool neglectCurves=false);

  void setPartitionPolicy(  Teuchos::RCP< MeshPartitionPolicy > partitionPolicy );

  void setUsePatchBasis( bool value );
  bool usePatchBasis();

  MeshTopologyPtr getTopology();
  
  vector<unsigned> vertexIndicesForCell(GlobalIndexType cellID);
  FieldContainer<double> vertexCoordinates(GlobalIndexType vertexIndex);

  void verticesForCell(FieldContainer<double>& vertices, GlobalIndexType cellID);
  void verticesForElementType(FieldContainer<double>& vertices, ElementTypePtr elemTypePtr);
  void verticesForSide(FieldContainer<double>& vertices, GlobalIndexType cellID, int sideOrdinal);

  void unregisterObserver(RefinementObserver* observer);
  void unregisterObserver(Teuchos::RCP<RefinementObserver> observer);
  void unregisterSolution(Teuchos::RCP<Solution> solution);

  void writeMeshPartitionsToFile(const string & fileName);

  double getCellMeasure(GlobalIndexType cellID);
  double getCellXSize(GlobalIndexType cellID);
  double getCellYSize(GlobalIndexType cellID);
  vector<double> getCellOrientation(GlobalIndexType cellID);
};

#endif
