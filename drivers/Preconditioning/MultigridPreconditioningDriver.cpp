#include "CamelliaDebugUtility.h"
#include "ConvectionDiffusionFormulation.h"
#include "ExpFunction.h"
#include "GMGOperator.h"
#include "GMGSolver.h"
#include "GnuPlotUtil.h"
#include "HDF5Exporter.h"
#include "MeshFactory.h"
#include "PoissonFormulation.h"
#include "PreviousSolutionFunction.h"
#include "RefinementStrategy.h"
#include "Solver.h"
#include "StokesVGPFormulation.h"
#include "TrigFunctions.h"
#include "TypeDefs.h"

#include "AztecOO.h"

#include "Epetra_Operator_to_Epetra_Matrix.h"
#include "EpetraExt_MatrixMatrix.h"
#include "EpetraExt_RowMatrixOut.h"
#include "EpetraExt_MultiVectorOut.h"

#include "Ifpack_AdditiveSchwarz.h"
#include "Ifpack_Amesos.h"
#include "Ifpack_IC.h"
#include "Ifpack_ILU.h"

#include <Teuchos_GlobalMPISession.hpp>

using namespace Camellia;
using namespace Intrepid;
using namespace std;

enum ProblemChoice
{
  Poisson,
  ConvectionDiffusion,
  Stokes,
  NavierStokes
};

void initializeSolutionAndCoarseMesh(SolutionPtr &solution, vector<MeshPtr> &meshesCoarseToFine, IPPtr &graphNorm, ProblemChoice problemChoice,
                                     int spaceDim, bool conformingTraces, bool useStaticCondensation, int numCells, int k, int delta_k,
                                     int rootMeshNumCells, bool useZeroMeanConstraints = false)
{
  BFPtr bf;
  BCPtr bc;
  RHSPtr rhs;
  MeshPtr mesh;
  
  double width = 1.0; // in each dimension
  vector<double> x0(spaceDim,0); // origin is the default
  
  VarPtr p; // pressure
  
  if (problemChoice == Poisson)
  {
    PoissonFormulation formulation(spaceDim, conformingTraces);
    
    bf = formulation.bf();
    
    rhs = RHS::rhs();
    FunctionPtr f = Function::constant(1.0);
    
    VarPtr q = formulation.q();
    rhs->addTerm( f * q );
    
    bc = BC::bc();
    SpatialFilterPtr boundary = SpatialFilter::allSpace();
    VarPtr phi_hat = formulation.phi_hat();
    bc->addDirichlet(phi_hat, boundary, Function::zero());
  }
  else if (problemChoice == ConvectionDiffusion)
  {
    double epsilon = 1e-2;
    FunctionPtr beta;
    FunctionPtr beta_x = Function::constant(1);
    FunctionPtr beta_y = Function::constant(2);
    FunctionPtr beta_z = Function::constant(3);
    if (spaceDim == 1)
      beta = beta_x;
    else if (spaceDim == 2)
      beta = Function::vectorize(beta_x, beta_y);
    else if (spaceDim == 3)
      beta = Function::vectorize(beta_x, beta_y, beta_z);
    ConvectionDiffusionFormulation formulation(spaceDim, conformingTraces, beta, epsilon);
    
    bf = formulation.bf();
    
    rhs = RHS::rhs();
    FunctionPtr f = Function::constant(1.0);
    
    VarPtr v = formulation.v();
    rhs->addTerm( f * v );
    
    bc = BC::bc();
    VarPtr uhat = formulation.uhat();
    VarPtr tc = formulation.tc();
    SpatialFilterPtr inflowX = SpatialFilter::matchingX(-1);
    SpatialFilterPtr inflowY = SpatialFilter::matchingY(-1);
    SpatialFilterPtr inflowZ = SpatialFilter::matchingZ(-1);
    SpatialFilterPtr outflowX = SpatialFilter::matchingX(1);
    SpatialFilterPtr outflowY = SpatialFilter::matchingY(1);
    SpatialFilterPtr outflowZ = SpatialFilter::matchingZ(1);
    FunctionPtr zero = Function::zero();
    FunctionPtr one = Function::constant(1);
    FunctionPtr x = Function::xn(1);
    FunctionPtr y = Function::yn(1);
    FunctionPtr z = Function::zn(1);
    if (spaceDim == 1)
    {
      bc->addDirichlet(tc, inflowX, -one);
      bc->addDirichlet(uhat, outflowX, zero);
    }
    if (spaceDim == 2)
    {
      bc->addDirichlet(tc, inflowX, -1*.5*(one-y));
      bc->addDirichlet(uhat, outflowX, zero);
      bc->addDirichlet(tc, inflowY, -2*.5*(one-x));
      bc->addDirichlet(uhat, outflowY, zero);
    }
    if (spaceDim == 3)
    {
      bc->addDirichlet(tc, inflowX, -1*.25*(one-y)*(one-z));
      bc->addDirichlet(uhat, outflowX, zero);
      bc->addDirichlet(tc, inflowY, -2*.25*(one-x)*(one-z));
      bc->addDirichlet(uhat, outflowY, zero);
      bc->addDirichlet(tc, inflowZ, -3*.25*(one-x)*(one-y));
      bc->addDirichlet(uhat, outflowZ, zero);
    }
  }
  else if (problemChoice == Stokes)
  {
    
    StokesVGPFormulation formulation(spaceDim, conformingTraces);
    
    p = formulation.p();
    
    bf = formulation.bf();
    graphNorm = bf->graphNorm();
    
    rhs = RHS::rhs();
    
    FunctionPtr cos_y = Teuchos::rcp( new Cos_y );
    FunctionPtr sin_y = Teuchos::rcp( new Sin_y );
    FunctionPtr exp_x = Teuchos::rcp( new Exp_x );
    FunctionPtr exp_z = Teuchos::rcp( new Exp_z );
    
    FunctionPtr x = Function::xn(1);
    FunctionPtr y = Function::yn(1);
    FunctionPtr z = Function::zn(1);
    
    FunctionPtr u1_exact, u2_exact, u3_exact, p_exact;
    
    if (spaceDim == 2)
    {
      // this one was in the Cockburn Kanschat LDG Stokes paper
      u1_exact = - exp_x * ( y * cos_y + sin_y );
      u2_exact = exp_x * y * sin_y;
      p_exact = 2.0 * exp_x * sin_y;
    }
    else
    {
      // this one is inspired by the 2D one
      u1_exact = - exp_x * ( y * cos_y + sin_y );
      u2_exact = exp_x * y * sin_y + exp_z * y * cos_y;
      u3_exact = - exp_z * (cos_y - y * sin_y);
      p_exact = 2.0 * exp_x * sin_y + 2.0 * exp_z * cos_y;
      // DEBUGGING:
      //      u1_exact = Function::zero();
      //      u2_exact = Function::zero();
      //      u3_exact = x;
      //      p_exact = Function::zero();
    }
    
    // to ensure zero mean for p, need the domain carefully defined:
    x0 = vector<double>(spaceDim,-1.0);
    
    width = 2.0;
    
    bc = BC::bc();
    // our usual way of adding in the zero mean constraint results in a negative eigenvalue
    // therefore, for now, we use a single-point BC
    //    bc->addZeroMeanConstraint(formulation.p());
    SpatialFilterPtr boundary = SpatialFilter::allSpace();
    bc->addDirichlet(formulation.u_hat(1), boundary, u1_exact);
    bc->addDirichlet(formulation.u_hat(2), boundary, u2_exact);
    if (spaceDim==3) bc->addDirichlet(formulation.u_hat(3), boundary, u3_exact);
    
    double mu = 1.0;
    
    FunctionPtr f1, f2, f3;
    if (spaceDim==2)
    {
      f1 = -p_exact->dx() + mu * (u1_exact->dx()->dx() + u1_exact->dy()->dy());
      f2 = -p_exact->dy() + mu * (u2_exact->dx()->dx() + u2_exact->dy()->dy());
    }
    else
    {
      f1 = -p_exact->dx() + mu * (u1_exact->dx()->dx() + u1_exact->dy()->dy() + u1_exact->dz()->dz());
      f2 = -p_exact->dy() + mu * (u2_exact->dx()->dx() + u2_exact->dy()->dy() + u2_exact->dz()->dz());
      f3 = -p_exact->dz() + mu * (u3_exact->dx()->dx() + u3_exact->dy()->dy() + u3_exact->dz()->dz());
    }
    
    VarPtr v1 = formulation.v(1);
    VarPtr v2 = formulation.v(2);
    
    VarPtr v3;
    if (spaceDim==3) v3 = formulation.v(3);
    
    RHSPtr rhs = RHS::rhs();
    if (spaceDim==2)
      rhs->addTerm(f1 * v1 + f2 * v2);
    else
      rhs->addTerm(f1 * v1 + f2 * v2 + f3 * v3);
  }
  
  int H1Order = k + 1;
  
  BFPtr bilinearForm = bf;
  
  vector<double> dimensions;
  vector<int> elementCounts;
  for (int d=0; d<spaceDim; d++)
  {
    dimensions.push_back(width);
    elementCounts.push_back(rootMeshNumCells);
  }
  mesh = MeshFactory::rectilinearMesh(bf, dimensions, elementCounts, H1Order, delta_k, x0);
  
  // now that we have mesh, add pressure constraint for Stokes (imposing zero at origin--want to aim for center of mesh)
  if ((problemChoice == Stokes) || (problemChoice==NavierStokes))
  {
    if (!useZeroMeanConstraints)
    {
      vector<double> origin(spaceDim,0);
      IndexType vertexIndex;
      
      if (!mesh->getTopology()->getVertexIndex(origin, vertexIndex))
      {
        TEUCHOS_TEST_FOR_EXCEPTION(true, std::invalid_argument, "origin vertex not found");
      }
      bc->addSinglePointBC(p->ID(), 0, vertexIndex);
    }
    else
    {
      bc->addZeroMeanConstraint(p);
    }
  }
  
  int H1Order_coarse = 0 + 1;
  MeshPtr k0Mesh = MeshFactory::rectilinearMesh(bf, dimensions, elementCounts, H1Order_coarse, delta_k, x0);

  
  // get a sample cell topology:
  CellTopoPtr cellTopo = k0Mesh->getTopology()->getCell(0)->topology();
  RefinementPatternPtr refPattern = RefinementPattern::regularRefinementPattern(cellTopo);
  
  meshesCoarseToFine.clear();
  meshesCoarseToFine.push_back(k0Mesh);
  
  int meshWidthCells = rootMeshNumCells;
  while (meshWidthCells < numCells)
  {
    k0Mesh = k0Mesh->deepCopy();
    
    set<IndexType> activeCellIDs = mesh->getActiveCellIDs(); // should match between coarseMesh and mesh
    mesh->hRefine(activeCellIDs, refPattern);
    k0Mesh->hRefine(activeCellIDs, refPattern);

    meshesCoarseToFine.push_back(k0Mesh);
    meshWidthCells *= 2;
  }
  meshesCoarseToFine.push_back(mesh);
  
  if (meshWidthCells != numCells)
  {
    int rank = Teuchos::GlobalMPISession::getRank();
    if (rank == 0)
    {
      cout << "Warning: may have overrefined mesh; mesh has width " << meshWidthCells << ", not " << numCells << endl;
    }
  }
  
  graphNorm = bf->graphNorm();
  
  solution = Solution::solution(mesh, bc, rhs, graphNorm);
  solution->setUseCondensedSolve(useStaticCondensation);
  solution->setZMCsAsGlobalLagrange(false); // fine grid solution shouldn't impose ZMCs (should be handled in coarse grid solve)
}

long long approximateMemoryCostsForMeshTopologies(vector<MeshPtr> meshes)
{
  map<MeshTopology*, long long> meshTopologyCosts; // pointer as key ensures we only count each MeshTopology once, even if they are shared
  for (MeshPtr mesh : meshes)
  {
    MeshTopologyPtr meshTopo = mesh->getTopology();
    long long memoryCost = meshTopo->approximateMemoryFootprint();
    meshTopologyCosts[meshTopo.get()] = memoryCost;
  }
  long long memoryCostTotal = 0;
  for (auto entry : meshTopologyCosts)
  {
    memoryCostTotal += entry.second;
  }
  return memoryCostTotal;
}

int main(int argc, char *argv[])
{
  Teuchos::GlobalMPISession mpiSession(&argc, &argv, 0);
  int rank = Teuchos::GlobalMPISession::getRank();
  int numProcs = Teuchos::GlobalMPISession::getNProc();

#ifdef HAVE_MPI
  Epetra_MpiComm Comm(MPI_COMM_WORLD);
  //cout << "rank: " << rank << " of " << numProcs << endl;
#else
  Epetra_SerialComm Comm;
#endif

  Comm.Barrier(); // set breakpoint here to allow debugger attachment to other MPI processes than the one you automatically attached to.

  Teuchos::CommandLineProcessor cmdp(false,true); // false: don't throw exceptions; true: do return errors for unrecognized options

  int k = 1; // poly order for field variables
  int delta_k = 1;   // test space enrichment

  bool conformingTraces = false;

  int numCells = -1;
  int numCellsRootMesh = 2;
  int spaceDim = 1;
  bool useCondensedSolve = false;

  double cgTol = 1e-10;
  int cgMaxIterations = 2000;

  bool reportTimings = false;
  bool useZeroMeanConstraints = false;

  string problemChoiceString = "Poisson";
  string coarseSolverChoiceString = "KLU";

  cmdp.setOption("problem",&problemChoiceString,"problem choice: Poisson, ConvectionDiffusion, Stokes, Navier-Stokes");

  cmdp.setOption("polyOrder",&k,"polynomial order for field variable u");
  cmdp.setOption("delta_k", &delta_k, "test space polynomial order enrichment");

  cmdp.setOption("coarseSolver", &coarseSolverChoiceString, "coarse solver choice: KLU, MUMPS, SuperLUDist, SimpleML");

  cmdp.setOption("useCondensedSolve", "useStandardSolve", &useCondensedSolve);

  cmdp.setOption("spaceDim", &spaceDim, "space dimensions (1, 2, or 3)");

  cmdp.setOption("maxIterations", &cgMaxIterations, "maximum number of CG iterations");
  cmdp.setOption("cgTol", &cgTol, "CG convergence tolerance");

  cmdp.setOption("reportTimings", "dontReportTimings", &reportTimings, "Report timings in Solution");

  cmdp.setOption("useZeroMeanConstraint", "usePointConstraint", &useZeroMeanConstraints, "Use a zero-mean constraint for the pressure (otherwise, use a vertex constraint at the origin)");

  if (cmdp.parse(argc,argv) != Teuchos::CommandLineProcessor::PARSE_SUCCESSFUL)
  {
#ifdef HAVE_MPI
    MPI_Finalize();
#endif
    return -1;
  }

  ProblemChoice problemChoice;

  if (problemChoiceString == "Poisson")
  {
    problemChoice = Poisson;
  }
  else if (problemChoiceString == "ConvectionDiffusion")
  {
    problemChoice = ConvectionDiffusion;
  }
  else if (problemChoiceString == "Stokes")
  {
    problemChoice = Stokes;
  }
  else if (problemChoiceString == "Navier-Stokes")
  {
    if (rank==0) cout << "Navier-Stokes not yet supported by this driver!\n";
#ifdef HAVE_MPI
    MPI_Finalize();
#endif
    return -1;
  }
  else
  {
    if (rank==0) cout << "Problem choice not recognized.\n";
#ifdef HAVE_MPI
    MPI_Finalize();
#endif
    return -1;
  }

  if (rank==0)
  {
    cout << "Solving " << spaceDim << "D " << problemChoiceString << " problem on " << numProcs << " MPI ranks.  Initializing meshes...\n";
  }
  
  Epetra_Time timer(Comm);

  SolutionPtr solution;
  MeshPtr coarseMesh;
  IPPtr ip;

  if (numCells == -1)
  {
    numCells = (int)ceil(pow(numProcs,1.0/(double)spaceDim));
  }
  
  vector<MeshPtr> meshesCoarseToFine;
  initializeSolutionAndCoarseMesh(solution, meshesCoarseToFine, ip, problemChoice, spaceDim, conformingTraces, useCondensedSolve,
                                  numCells, k, delta_k, numCellsRootMesh, useZeroMeanConstraints);
  
  double meshInitializationTime = timer.ElapsedTime();

  int numDofs = solution->mesh()->numGlobalDofs();
  int numElements = solution->mesh()->numActiveElements();
  
  long long approximateMemoryCostInBytes = approximateMemoryCostsForMeshTopologies(meshesCoarseToFine);
  double memoryCostInMB = approximateMemoryCostInBytes / (1024.0 * 1024.0);
  
  if (rank==0)
  {
    cout << setprecision(2);
    cout << "Mesh initialization completed in " << meshInitializationTime << " seconds.  Fine mesh has " << numDofs;
    cout << " global degrees of freedom on " << numElements << " elements.\n";
    cout << "Approximate (within a factor of 2 or so) memory cost for all mesh topologies: " << memoryCostInMB << " MB.\n";
  }
  
  timer.ResetStartTime();
  bool reuseFactorization = true;
  SolverPtr coarseSolver = Solver::getDirectSolver(reuseFactorization);
  Teuchos::RCP<GMGSolver> gmgSolver = Teuchos::rcp(new GMGSolver(solution, meshesCoarseToFine, cgMaxIterations, cgTol, coarseSolver));
  gmgSolver->setAztecOutput(10);
  
  double gmgSolverInitializationTime = timer.ElapsedTime();
  if (rank==0)
  {
    cout << "GMGSolver initialized in " << gmgSolverInitializationTime << " seconds.\n";
  }

  timer.ResetStartTime();
  solution->solve(gmgSolver);

  double solveTime = timer.ElapsedTime();
  
  if (rank==0)
  {
    cout << "Solve completed in " << solveTime << " seconds.\n";
    cout << "Finest GMGOperator, timing report:\n";
  }
  gmgSolver->gmgOperator()->reportTimings(StatisticChoice::MAX);
  
  return 0;
}