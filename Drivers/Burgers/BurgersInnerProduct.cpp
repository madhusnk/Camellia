//
//  BurgersInnerProduct.cpp
//  Camellia
//
//  Created by Nathan Roberts on 2/21/12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

#include "BurgersInnerProduct.h"

#include "BurgersBilinearForm.h"
#include "Mesh.h"
#include "BasisCache.h" // for Jacobian/cell measure computation

// Teuchos includes
#include "Teuchos_RCP.hpp"

#include "DPGInnerProduct.h"

/*
 Implements Burgers inner product for L2 stability in u
 */

BurgersInnerProduct::BurgersInnerProduct(Teuchos::RCP< BurgersBilinearForm > bfs, Teuchos::RCP<Mesh> mesh) : DPGInnerProduct((Teuchos::RCP< BurgersBilinearForm>) bfs) {
  _burgersBilinearForm=bfs; // redundant, but no way around it.
  _mesh=mesh; // for h-scaling
} 

void BurgersInnerProduct::operators(int testID1, int testID2, 
                                    vector<IntrepidExtendedTypes::EOperatorExtended> &testOp1,
                                    vector<IntrepidExtendedTypes::EOperatorExtended> &testOp2){
  testOp1.clear();
  testOp2.clear();
  
  if (testID1 == testID2) {
    
    if (BurgersBilinearForm::TAU==testID1) {
      
      // L2 portion of tau
      testOp1.push_back(IntrepidExtendedTypes::OPERATOR_VALUE);
      testOp2.push_back(IntrepidExtendedTypes::OPERATOR_VALUE);
      
      // div portion of tau
      testOp1.push_back(IntrepidExtendedTypes::OPERATOR_DIV);
      testOp2.push_back(IntrepidExtendedTypes::OPERATOR_DIV);
      
    } else if (BurgersBilinearForm::V==testID1) {
      
      // L2 portion of v (should be scaled by epsilon/h later)
      testOp1.push_back(IntrepidExtendedTypes::OPERATOR_VALUE);
      testOp2.push_back(IntrepidExtendedTypes::OPERATOR_VALUE);
      
      // grad portion of v (should be scaled by epsilon later);
      testOp1.push_back(IntrepidExtendedTypes::OPERATOR_GRAD);
      testOp2.push_back(IntrepidExtendedTypes::OPERATOR_GRAD);
      
      // grad dotted with beta for v (applied in next routine)
      testOp1.push_back(IntrepidExtendedTypes::OPERATOR_GRAD);
      testOp2.push_back(IntrepidExtendedTypes::OPERATOR_GRAD);
      
    }
  }
  
}

void BurgersInnerProduct::applyInnerProductData(FieldContainer<double> &testValues1,
                                                FieldContainer<double> &testValues2,
                                                int testID1, int testID2, int operatorIndex,
                                                Teuchos::RCP<BasisCache> basisCache) {
  
  FieldContainer<double> physicalPoints = basisCache->getPhysicalCubaturePoints();
  
  if (testID1==testID2){
    
    double epsilon = _burgersBilinearForm->getEpsilon();
    int spaceDim = physicalPoints.dimension(2);
    
    //      cout << "Beta = " << beta_x << ", " << beta_y << ", and epsilon = " << epsilon << endl;
    
    if (testID1==BurgersBilinearForm::V ) {
      
      if ((operatorIndex==0)) { // if the term is c*||v|| 
        
        int numCells = testValues1.dimension(0);
        int basisCardinality = testValues1.dimension(1);
        int numPoints = testValues1.dimension(2);
        
        for (int cellIndex=0; cellIndex<numCells; cellIndex++) {              
          
          //////////////////// 
          
          // compute measure of current cell, scale epsilon term by this 
          FieldContainer<double> physicalPointForCell(1,spaceDim);
          physicalPointForCell(0,0) = physicalPoints(cellIndex,0,0); // assuming that all all pts corresponding to 1 cell index are the same...
          physicalPointForCell(0,1) = physicalPoints(cellIndex,0,1);
          vector<ElementPtr> elemVector = _mesh->elementsForPoints(physicalPointForCell);
          TEST_FOR_EXCEPTION(elemVector.size()>1, std::invalid_argument,
                             "More than one element returned for a single pt!");
          ElementPtr elem = elemVector[0];
          
          FieldContainer<double> allPhysicalNodesForType = _mesh->physicalCellNodes(elem->elementType());
          
          // create basisCache
          int cubDegree = elem->elementType()->testOrderPtr->maxBasisDegree();
          BasisCache basisCache = BasisCache(allPhysicalNodesForType, *(elem->elementType()->cellTopoPtr), cubDegree);
          FieldContainer<double> cellMeasures = basisCache.getCellMeasures();
          
          double scaling = epsilon;
          if (operatorIndex==0){ // scale the L2 component of v 
            scaling = min(epsilon/cellMeasures(cellIndex),1.0);
            //	      cout << "L2 coeff for cell " << elem->cellID() << " is " << epsilon/scaling << endl;
          } 
          
          //////////////////// 
          
          for (int basisOrdinal=0; basisOrdinal<basisCardinality; basisOrdinal++) {
            for (int ptIndex=0; ptIndex<numPoints; ptIndex++) {
              double x = physicalPoints(cellIndex,ptIndex,0);
              double y = physicalPoints(cellIndex,ptIndex,1);
              testValues1(cellIndex,basisOrdinal,ptIndex) = testValues1(cellIndex,basisOrdinal,ptIndex)*scaling;
            }
          }
        }
      } else if (operatorIndex==1) {
        
        _bilinearForm->multiplyFCByWeight(testValues1,epsilon);
        //      _bilinearForm->multiplyFCByWeight(testValues2,1.0);
        
      } else if (operatorIndex==2) { // if it's the beta dot grad term
        
        int numCells = testValues1.dimension(0);
        int basisCardinality = testValues1.dimension(1);
        int numPoints = testValues1.dimension(2);
        //	  cout << "dimensions are " << numCells <<","<<basisCardinality<<","<<numPoints<<","<<spaceDim<< endl;
        
        TEST_FOR_EXCEPTION(spaceDim != 2, std::invalid_argument,
                           "BurgersBilinearForm only supports 2 dimensions right now.");
        
        // because we change dimensions of the values, by dotting with beta, 
        // we'll need to copy the values and resize the original container
        FieldContainer<double> testValuesCopy1 = testValues1;
        FieldContainer<double> testValuesCopy2 = testValues2;
        testValues1.resize(numCells,basisCardinality,numPoints);
        testValues2.resize(numCells,basisCardinality,numPoints);
        FieldContainer<double> beta = _burgersBilinearForm->getBeta(physicalPoints);
        for (int cellIndex=0; cellIndex<numCells; cellIndex++) {                        
          for (int basisOrdinal=0; basisOrdinal<basisCardinality; basisOrdinal++) {
            for (int ptIndex=0; ptIndex<numPoints; ptIndex++) {
              double x = physicalPoints(cellIndex,ptIndex,0);
              double y = physicalPoints(cellIndex,ptIndex,1);
              double weight = getWeight(x,y);
              //		double beta_x = _burgersBilinearForm->getBeta(x,y)[0];
              //		double beta_y = _burgersBilinearForm->getBeta(x,y)[1];
              double beta_x = beta(cellIndex,ptIndex,0);
              double beta_y = beta(cellIndex,ptIndex,1);
              
              testValues1(cellIndex,basisOrdinal,ptIndex)  = beta_x * testValuesCopy1(cellIndex,basisOrdinal,ptIndex,0) * weight 
              + beta_y * testValuesCopy1(cellIndex,basisOrdinal,ptIndex,1) * weight;
              testValues2(cellIndex,basisOrdinal,ptIndex)  = beta_x * testValuesCopy2(cellIndex,basisOrdinal,ptIndex,0)
              + beta_y * testValuesCopy2(cellIndex,basisOrdinal,ptIndex,1);
            }
          }
        }
      }
    } else if (testID1==BurgersBilinearForm::TAU){ // L2 portion of Tau
      
      if (operatorIndex==0) {
        int numCells = testValues1.dimension(0);
        int basisCardinality = testValues1.dimension(1);
        int numPoints = testValues1.dimension(2);
        int spaceDim = testValues1.dimension(3);
        
        for (int cellIndex=0; cellIndex<numCells; cellIndex++) {
          for (int basisOrdinal=0; basisOrdinal<basisCardinality; basisOrdinal++) {
            for (int ptIndex=0; ptIndex<numPoints; ptIndex++) {
              double x = physicalPoints(cellIndex,ptIndex,0);
              double y = physicalPoints(cellIndex,ptIndex,1);
              for (int dimIndex=0; dimIndex<spaceDim; dimIndex++){
                double weight = getWeight(x,y);
                testValues1(cellIndex,basisOrdinal,ptIndex,dimIndex) = testValues1(cellIndex,basisOrdinal,ptIndex,dimIndex)*weight;
              }
            }
          }
        }
      } else if (operatorIndex==1) { // div portion of TAU
        
        int numCells = testValues1.dimension(0);
        int basisCardinality = testValues1.dimension(1);
        int numPoints = testValues1.dimension(2);
        
        for (int cellIndex=0; cellIndex<numCells; cellIndex++) {
          for (int basisOrdinal=0; basisOrdinal<basisCardinality; basisOrdinal++) {
            for (int ptIndex=0; ptIndex<numPoints; ptIndex++) {
              double x = physicalPoints(cellIndex,ptIndex,0);
              double y = physicalPoints(cellIndex,ptIndex,1);
              double weight = getWeight(x,y);
              testValues1(cellIndex,basisOrdinal,ptIndex) = testValues1(cellIndex,basisOrdinal,ptIndex)*weight;
            }
          }
        }
      }
    }       
  }
}

// get weight that biases the outflow over the inflow (for math stability purposes)
double BurgersInnerProduct::getWeight(double x,double y){
  
  //    return _burgersBilinearForm->getEpsilon() + x*(1.0-x)*y; // 0 at x = 0, x = 1, y = 0;
  return 1.0; // for confection
}
