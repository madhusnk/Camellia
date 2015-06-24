#include "Solution.h"
#include "RHS.h"

#include "MeshUtilities.h"
#include "MeshFactory.h"

#include <Teuchos_GlobalMPISession.hpp>

#include "Teuchos_CommandLineProcessor.hpp"
#include "Teuchos_ParameterList.hpp"
#include "Teuchos_TimeMonitor.hpp"
#include "Amesos_config.h"

#include "EpetraExt_ConfigDefs.h"
#ifdef HAVE_EPETRAEXT_HDF5
#include "HDF5Exporter.h"
#endif

#include "BF.h"
#include "Function.h"
#include "RefinementStrategy.h"
#include "GMGSolver.h"
#include "SpaceTimeConvectionDiffusionFormulation.h"
#include "SpatiallyFilteredFunction.h"
#include "ExpFunction.h"

using namespace Camellia;

int main(int argc, char *argv[])
{

#ifdef HAVE_MPI
  Teuchos::GlobalMPISession mpiSession(&argc, &argv,0);

  Epetra_MpiComm Comm(MPI_COMM_WORLD);
#else
  Epetra_SerialComm Comm;
#endif

  int commRank = Teuchos::GlobalMPISession::getRank();

  Comm.Barrier(); // set breakpoint here to allow debugger attachment to other MPI processes than the one you automatically attached to.

  Teuchos::CommandLineProcessor cmdp(false,true); // false: don't throw exceptions; true: do return errors for unrecognized options

  // problem parameters:
  int spaceDim = 2;
  double epsilon = 1;
  int numRefs = 1;
  int p = 2, delta_p = 2;
  int numXElems = 1;
  bool useConformingTraces = false;
  string solverChoice = "KLU";
  double solverTolerance = 1e-8;
  int maxLinearIterations = 10000;
  bool computeL2Error = false;
  bool exportSolution = false;
  string norm = "Graph";
  string outputDir = ".";
  string tag="";
  cmdp.setOption("spaceDim", &spaceDim, "spatial dimension");
  cmdp.setOption("polyOrder",&p,"polynomial order for field variable u");
  cmdp.setOption("delta_p", &delta_p, "test space polynomial order enrichment");
  cmdp.setOption("numRefs",&numRefs,"number of refinements");
  cmdp.setOption("numXElems",&numXElems,"number of elements in x direction");
  cmdp.setOption("epsilon", &epsilon, "epsilon");
  cmdp.setOption("norm", &norm, "norm");
  cmdp.setOption("conformingTraces", "nonconformingTraces", &useConformingTraces, "use conforming traces");
  cmdp.setOption("solver", &solverChoice, "KLU, SuperLU, MUMPS, GMG-Direct, GMG-ILU, GMG-IC");
  cmdp.setOption("solverTolerance", &solverTolerance, "iterative solver tolerance");
  cmdp.setOption("maxLinearIterations", &maxLinearIterations, "maximum number of iterations for linear solver");
  cmdp.setOption("outputDir", &outputDir, "output directory");
  cmdp.setOption("computeL2Error", "skipL2Error", &computeL2Error, "compute L2 error");
  cmdp.setOption("exportSolution", "skipExport", &exportSolution, "export solution to HDF5");
  cmdp.setOption("tag", &tag, "output tag");

  if (cmdp.parse(argc,argv) != Teuchos::CommandLineProcessor::PARSE_SUCCESSFUL)
  {
#ifdef HAVE_MPI
    MPI_Finalize();
#endif
    return -1;
  }

  // Exact solution
  // FunctionPtr uExact1D = Teuchos::rcp(new ExactU1D(epsilon));
  // FunctionPtr uExact2D = Teuchos::rcp(new ExactU2D(epsilon));
  double l = 1;
  double k = 1;
  double lambda1 = (-1+sqrt(1-4*epsilon*l))/(-2*epsilon);
  double lambda2 = (-1-sqrt(1-4*epsilon*l))/(-2*epsilon);
  FunctionPtr explt = Teuchos::rcp(new Exp_at(-l));
  FunctionPtr explambda1x = Teuchos::rcp(new Exp_ax(lambda1));
  FunctionPtr explambda2x = Teuchos::rcp(new Exp_ax(lambda2));
  FunctionPtr u_exact = explt*(explambda1x-explambda2x);
  if (spaceDim == 2)
    u_exact = u_exact*Function::yn(1);
  FunctionPtr sigma_exact = epsilon*u_exact->grad();

  FunctionPtr beta;
  FunctionPtr beta_x = Function::constant(1);
  FunctionPtr beta_y = Function::constant(0);
  FunctionPtr beta_z = Function::constant(0);
  // if (spaceDim == 1)
  //   beta = beta_x;
  // else if (spaceDim == 2)
  //   beta = Function::vectorize(beta_x, beta_y);
  // else if (spaceDim == 3)
    beta = Function::vectorize(beta_x, beta_y, beta_z);

  SpaceTimeConvectionDiffusionFormulation form(spaceDim, epsilon, beta, useConformingTraces);

  map<int, FunctionPtr> exactMap;
  exactMap[form.u()->ID()] = u_exact;
  exactMap[form.sigma(1)->ID()] = sigma_exact->x();
  if (spaceDim == 2)
    exactMap[form.sigma(2)->ID()] = sigma_exact->y();
  exactMap[form.tc()->ID()] = form.tc()->termTraced()->evaluate(exactMap);
  exactMap[form.uhat()->ID()] = form.uhat()->termTraced()->evaluate(exactMap);

  // Build mesh
  vector<double> x0;// = vector<double>(spaceDim,-1);
  x0.push_back(-1);
  x0.push_back(-.5);
  double width = 1.0;
  vector<double> dimensions;
  vector<int> elementCounts;
  for (int d=0; d<spaceDim; d++)
  {
    dimensions.push_back(width);
    elementCounts.push_back(numXElems);
  }
  MeshTopologyPtr spatialMeshTopo = MeshFactory::rectilinearMeshTopology(dimensions, elementCounts, x0);
  double t0 = 0.0, t1 = 1.0;
  MeshTopologyPtr spaceTimeMeshTopo = MeshFactory::spaceTimeMeshTopology(spatialMeshTopo, t0, t1);

  FunctionPtr zero = Function::zero();
  FunctionPtr one = Function::constant(1);

  LinearTermPtr forcingTerm = Teuchos::null;
  form.initializeSolution(spaceTimeMeshTopo, p+1, delta_p, norm, forcingTerm);

  MeshPtr mesh = form.solution()->mesh();
  MeshPtr k0Mesh = Teuchos::rcp( new Mesh (spaceTimeMeshTopo->deepCopy(), form.bf(), 1, delta_p) );
  mesh->registerObserver(k0Mesh);

  // Set up boundary conditions
  BCPtr bc = form.solution()->bc();
  VarPtr uhat = form.uhat();
  VarPtr tc = form.tc();
  SpatialFilterPtr initTime = SpatialFilter::matchingT(0);
  if (spaceDim == 1)
  {
    SpatialFilterPtr leftX  = SpatialFilter::matchingX(x0[0]);
    SpatialFilterPtr rightX = SpatialFilter::matchingX(x0[0]+width);
    bc->addDirichlet(tc,   leftX,    exactMap[form.tc()->ID()]);
    bc->addDirichlet(uhat, rightX,   exactMap[form.uhat()->ID()]);
    bc->addDirichlet(tc,   initTime, exactMap[form.tc()->ID()]);
  }
  else if (spaceDim == 2)
  {
    SpatialFilterPtr leftX  = SpatialFilter::matchingX(x0[0]);
    SpatialFilterPtr rightX = SpatialFilter::matchingX(x0[0]+width);
    SpatialFilterPtr leftY  = SpatialFilter::matchingY(x0[1]);
    SpatialFilterPtr rightY = SpatialFilter::matchingY(x0[1]+width);
    bc->addDirichlet(tc,   leftX,    exactMap[form.tc()->ID()]);
    bc->addDirichlet(uhat, rightX,   exactMap[form.uhat()->ID()]);
    bc->addDirichlet(tc, leftY,    exactMap[form.tc()->ID()]);
    bc->addDirichlet(tc, rightY,   exactMap[form.tc()->ID()]);
    bc->addDirichlet(tc,   initTime, exactMap[form.tc()->ID()]);
  }

  // Set up solution
  SolutionPtr soln = form.solution();

  double threshold = 0.20;
  RefinementStrategy refStrategy(soln, threshold);

  ostringstream solnName;
  solnName << "spacetimeConfusion" << spaceDim << "D_" << norm << "_" << epsilon << "_p" << p << "_" << solverChoice;
  if (tag != "")
    solnName << "_" << tag;
  Teuchos::RCP<HDF5Exporter> exporter;
  if (exportSolution)
    exporter = Teuchos::rcp(new HDF5Exporter(mesh,solnName.str(), outputDir));

  Teuchos::RCP<Time> solverTime = Teuchos::TimeMonitor::getNewCounter("Solve Time");

  if (commRank == 0)
    Solver::printAvailableSolversReport();
  map<string, SolverPtr> solvers;
  solvers["KLU"] = Solver::getSolver(Solver::KLU, true);
#if defined(HAVE_AMESOS_SUPERLUDIST) || defined(HAVE_AMESOS2_SUPERLUDIST)
  solvers["SuperLUDist"] = Solver::getSolver(Solver::SuperLUDist, true);
#endif
#ifdef HAVE_AMESOS_MUMPS
  solvers["MUMPS"] = Solver::getSolver(Solver::MUMPS, true);
#endif
  bool useStaticCondensation = false;
  int azOutput = 20; // print residual every 20 CG iterations

  string dataFileLocation;
  if (exportSolution)
    dataFileLocation = outputDir+"/"+solnName.str()+"/"+solnName.str()+".txt";
  else
    dataFileLocation = outputDir+"/"+solnName.str()+".txt";
  ofstream dataFile(dataFileLocation);
  dataFile << "ref\t " << "elements\t " << "dofs\t " << "energy\t " << "l2\t " << "solvetime\t" << "iterations\t " << endl;
  for (int refIndex=0; refIndex <= numRefs; refIndex++)
  {
    solverTime->start(true);
    Teuchos::RCP<GMGSolver> gmgSolver;
    if (solverChoice[0] == 'G')
    {
      gmgSolver = Teuchos::rcp( new GMGSolver(soln, k0Mesh, maxLinearIterations, solverTolerance, Solver::getDirectSolver(true), useStaticCondensation));
      gmgSolver->setAztecOutput(azOutput);
      if (solverChoice == "GMG-Direct")
        gmgSolver->gmgOperator().setSchwarzFactorizationType(GMGOperator::Direct);
      if (solverChoice == "GMG-ILU")
        gmgSolver->gmgOperator().setSchwarzFactorizationType(GMGOperator::ILU);
      if (solverChoice == "GMG-IC")
        gmgSolver->gmgOperator().setSchwarzFactorizationType(GMGOperator::IC);
      soln->solve(gmgSolver);
    }
    else
      soln->condensedSolve(solvers[solverChoice]);
    double solveTime = solverTime->stop();

    double energyError = soln->energyErrorTotal();
    double l2Error = 0;
    if (computeL2Error)
    {
      FunctionPtr u_soln, sigma1_soln, sigma2_soln,
                  u_diff, sigma1_diff, sigma2_diff,
                  u_sqr, sigma1_sqr, sigma2_sqr;
      u_soln = Function::solution(form.u(), soln);
      sigma1_soln = Function::solution(form.sigma(1), soln);
      if (spaceDim == 2)
        sigma2_soln = Function::solution(form.sigma(2), soln);
      u_diff = u_soln - u_exact;
      sigma1_diff = sigma1_soln - sigma_exact->x();
      if (spaceDim == 2)
        sigma2_diff = sigma2_soln - sigma_exact->y();
      u_sqr = u_diff*u_diff;
      sigma1_sqr = sigma1_diff*sigma1_diff;
      if (spaceDim == 2)
        sigma2_sqr = sigma2_diff*sigma2_diff;
      double u_l2, sigma1_l2, sigma2_l2;
      u_l2 = u_sqr->integrate(mesh, 5);
      sigma1_l2 = sigma1_sqr->integrate(mesh, 5);
      if (spaceDim == 2)
        sigma2_l2 = sigma2_sqr->integrate(mesh, 5);
      else
        sigma2_l2 = 0;
      l2Error = sqrt(u_l2+sigma1_l2+sigma2_l2);
    }
    if (commRank == 0)
    {
      int iterationCount;
      if (solverChoice[0] == 'G')
        iterationCount = gmgSolver->iterationCount();
      else
        iterationCount = 0;
      cout << "Refinement: " << refIndex
        << " \tElements: " << mesh->numActiveElements()
        << " \tDOFs: " << mesh->numGlobalDofs()
        << " \tEnergy Error: " << energyError
        << " \tL2 Error: " << l2Error
        << " \tSolve Time: " << solveTime
        << " \tIteration Count: " << iterationCount
        << endl;
      dataFile << refIndex
        << " " << mesh->numActiveElements()
        << " " << mesh->numGlobalDofs()
        << " " << energyError
        << " " << l2Error
        << " " << solveTime
        << " " << iterationCount
        << endl;
    }

    if (exportSolution)
      exporter->exportSolution(soln, refIndex);

    if (refIndex != numRefs)
      refStrategy.refine();
  }
  dataFile.close();

  return 0;
}