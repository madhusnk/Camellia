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
#include "SpaceTimeIncompressibleFormulation.h"
#include "SpatiallyFilteredFunction.h"
#include "ExpFunction.h"
#include "TrigFunctions.h"

using namespace Camellia;

class IncompressibleProblem
{
  protected:
    FunctionPtr _u1_exact;
    FunctionPtr _u2_exact;
    FunctionPtr _sigma1_exact;
    FunctionPtr _sigma2_exact;
    vector<double> _x0;
    vector<double> _dimensions;
    vector<int> _elementCounts;
    double _t0;
    double _t1;
  public:
    LinearTermPtr forcingTerm = Teuchos::null;
    virtual MeshTopologyPtr meshTopology() = 0;
    virtual void setBCs(SpaceTimeIncompressibleFormulationPtr form) = 0;
    virtual double computeL2Error(SpaceTimeIncompressibleFormulationPtr form, SolutionPtr solutionBackground) = 0;
};

class AnalyticalIncompressibleProblem : public IncompressibleProblem
{
  public:
    virtual MeshTopologyPtr meshTopology()
    {
      MeshTopologyPtr spatialMeshTopo = MeshFactory::rectilinearMeshTopology(_dimensions, _elementCounts, _x0);
      MeshTopologyPtr spaceTimeMeshTopo = MeshFactory::spaceTimeMeshTopology(spatialMeshTopo, _t0, _t1);
      return spaceTimeMeshTopo;
    }

    virtual void setBCs(SpaceTimeIncompressibleFormulationPtr form)
    {
      map<int, FunctionPtr> exactMap;
      exactMap[form->u(1)->ID()] = _u1_exact;
      exactMap[form->u(2)->ID()] = _u2_exact;
      exactMap[form->sigma(1,1)->ID()] = _sigma1_exact->x();
      exactMap[form->sigma(1,2)->ID()] = _sigma1_exact->y();
      exactMap[form->sigma(2,1)->ID()] = _sigma2_exact->x();
      exactMap[form->sigma(2,2)->ID()] = _sigma2_exact->y();
      exactMap[form->uhat(1)->ID()] = form->uhat(1)->termTraced()->evaluate(exactMap);
      exactMap[form->uhat(2)->ID()] = form->uhat(2)->termTraced()->evaluate(exactMap);

      BCPtr bc = form->solutionUpdate()->bc();
      SpatialFilterPtr initTime = SpatialFilter::matchingT(_t0);
      SpatialFilterPtr leftX  = SpatialFilter::matchingX(_x0[0]);
      SpatialFilterPtr rightX = SpatialFilter::matchingX(_x0[0]+_dimensions[0]);
      SpatialFilterPtr leftY  = SpatialFilter::matchingY(_x0[1]);
      SpatialFilterPtr rightY = SpatialFilter::matchingY(_x0[1]+_dimensions[1]);
      bc->addDirichlet(form->uhat(1), leftX,    exactMap[form->uhat(1)->ID()]);
      bc->addDirichlet(form->uhat(2), leftX,    exactMap[form->uhat(2)->ID()]);
      bc->addDirichlet(form->uhat(1), rightX,   exactMap[form->uhat(1)->ID()]);
      bc->addDirichlet(form->uhat(2), rightX,   exactMap[form->uhat(2)->ID()]);
      bc->addDirichlet(form->uhat(1), leftY,    exactMap[form->uhat(1)->ID()]);
      bc->addDirichlet(form->uhat(2), leftY,    exactMap[form->uhat(2)->ID()]);
      bc->addDirichlet(form->uhat(1), rightY,   exactMap[form->uhat(1)->ID()]);
      bc->addDirichlet(form->uhat(2), rightY,   exactMap[form->uhat(2)->ID()]);
      bc->addDirichlet(form->tmhat(1),initTime,-exactMap[form->uhat(1)->ID()]);
      bc->addDirichlet(form->tmhat(2),initTime,-exactMap[form->uhat(2)->ID()]);
    }
    double computeL2Error(SpaceTimeIncompressibleFormulationPtr form, SolutionPtr solutionBackground)
    {
      FunctionPtr u1_soln, u2_soln, sigma11_soln, sigma12_soln, sigma21_soln, sigma22_soln,
                  u1_diff, u2_diff, sigma11_diff, sigma12_diff, sigma21_diff, sigma22_diff,
                  u1_sqr, u2_sqr, sigma11_sqr, sigma12_sqr, sigma21_sqr, sigma22_sqr;
      u1_soln = Function::solution(form->u(1), solutionBackground);
      u2_soln = Function::solution(form->u(2), solutionBackground);
      sigma11_soln = Function::solution(form->sigma(1,1), solutionBackground);
      sigma12_soln = Function::solution(form->sigma(1,2), solutionBackground);
      sigma21_soln = Function::solution(form->sigma(2,1), solutionBackground);
      sigma22_soln = Function::solution(form->sigma(2,2), solutionBackground);
      u1_diff = u1_soln - _u1_exact;
      u2_diff = u2_soln - _u2_exact;
      sigma11_diff = sigma11_soln - _sigma1_exact->x();
      sigma12_diff = sigma12_soln - _sigma1_exact->y();
      sigma21_diff = sigma21_soln - _sigma2_exact->x();
      sigma22_diff = sigma22_soln - _sigma2_exact->y();
      u1_sqr = u1_diff*u1_diff;
      u2_sqr = u2_diff*u2_diff;
      sigma11_sqr = sigma11_diff*sigma11_diff;
      sigma12_sqr = sigma12_diff*sigma12_diff;
      sigma21_sqr = sigma21_diff*sigma21_diff;
      sigma22_sqr = sigma22_diff*sigma22_diff;
      double u1_l2, u2_l2, sigma11_l2, sigma12_l2, sigma21_l2, sigma22_l2;
      u1_l2 = u1_sqr->integrate(solutionBackground->mesh(), 5);
      u2_l2 = u2_sqr->integrate(solutionBackground->mesh(), 5);
      sigma11_l2 = sigma11_sqr->integrate(solutionBackground->mesh(), 5);
      sigma12_l2 = sigma12_sqr->integrate(solutionBackground->mesh(), 5);
      sigma21_l2 = sigma21_sqr->integrate(solutionBackground->mesh(), 5);
      sigma22_l2 = sigma22_sqr->integrate(solutionBackground->mesh(), 5);
      double l2Error = sqrt(u1_l2+u2_l2+sigma11_l2+sigma12_l2+sigma21_l2+sigma22_l2);
      return l2Error;
    }
};

class KovasznayProblem : public AnalyticalIncompressibleProblem
{
  private:
  public:
    KovasznayProblem(double Re)
    {
      // problemName = "Kovasznay";
      double pi = atan(1)*4;
      double lambda = Re/2-sqrt(Re*Re/4+4*pi*pi);
      FunctionPtr explambdaX = Teuchos::rcp(new Exp_ax(lambda));
      FunctionPtr cos2piY = Teuchos::rcp(new Cos_ay(2*pi));
      FunctionPtr sin2piY = Teuchos::rcp(new Sin_ay(2*pi));
      _u1_exact = 1 - explambdaX*cos2piY;
      _u2_exact = lambda/(2*pi)*explambdaX*sin2piY;
      _sigma1_exact = 1./Re*_u1_exact->grad();
      _sigma2_exact = 1./Re*_u2_exact->grad();

      _x0.push_back(-.5);
      _x0.push_back(-.5);
      _dimensions.push_back(1.5);
      _dimensions.push_back(2.0);
      _elementCounts.push_back(3);
      _elementCounts.push_back(4);
      _t0 = 0.0;
      _t1 = 0.25;
    }
};

class TaylorGreenProblem : public AnalyticalIncompressibleProblem
{
  private:
  public:
    TaylorGreenProblem(double Re)
    {
      // problemName = "Kovasznay";
      double pi = atan(1)*4;
      FunctionPtr temporalDecay = Teuchos::rcp(new Exp_at(-2./Re));
      FunctionPtr sinX = Teuchos::rcp(new Sin_x());
      FunctionPtr cosX = Teuchos::rcp(new Cos_x());
      FunctionPtr sinY = Teuchos::rcp(new Sin_y());
      FunctionPtr cosY = Teuchos::rcp(new Cos_y());
      _u1_exact = sinX*cosY*temporalDecay;
      _u2_exact = -cosX*sinY*temporalDecay;
      _sigma1_exact = 1./Re*_u1_exact->grad();
      _sigma2_exact = 1./Re*_u2_exact->grad();

      _x0.push_back(0);
      _x0.push_back(0);
      _dimensions.push_back(2*pi);
      _dimensions.push_back(2*pi);
      _elementCounts.push_back(4);
      _elementCounts.push_back(4);
      _t0 = 0.0;
      _t1 = 1.0;
    }
};

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
  double Re = 40;
  string problemChoice = "TaylorGreen";
  int numRefs = 1;
  int p = 2, delta_p = 2;
  int numXElems = 1;
  bool useConformingTraces = false;
  string solverChoice = "KLU";
  double solverTolerance = 1e-8;
  double nonlinearTolerance = 1e-5;
  int maxLinearIterations = 1000;
  int maxNonlinearIterations = 20;
  bool computeL2Error = false;
  bool exportSolution = false;
  string norm = "Graph";
  string outputDir = ".";
  string tag="";
  cmdp.setOption("spaceDim", &spaceDim, "spatial dimension");
  cmdp.setOption("Re", &Re, "Re");
  cmdp.setOption("problem", &problemChoice, "Kovasznay, TaylorGreen");
  cmdp.setOption("polyOrder",&p,"polynomial order for field variable u");
  cmdp.setOption("delta_p", &delta_p, "test space polynomial order enrichment");
  cmdp.setOption("numRefs",&numRefs,"number of refinements");
  cmdp.setOption("numXElems",&numXElems,"number of elements in x direction");
  cmdp.setOption("norm", &norm, "norm");
  cmdp.setOption("conformingTraces", "nonconformingTraces", &useConformingTraces, "use conforming traces");
  cmdp.setOption("solver", &solverChoice, "KLU, SuperLU, MUMPS, GMG-Direct, GMG-ILU, GMG-IC");
  cmdp.setOption("solverTolerance", &solverTolerance, "iterative solver tolerance");
  cmdp.setOption("nonlinearTolerance", &nonlinearTolerance, "nonlinear solver tolerance");
  cmdp.setOption("maxLinearIterations", &maxLinearIterations, "maximum number of iterations for linear solver");
  cmdp.setOption("maxNonlinearIterations", &maxNonlinearIterations, "maximum number of iterations for Newton solver");
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

  map<string, Teuchos::RCP<IncompressibleProblem>> problems;
  problems["Kovasznay"] = Teuchos::rcp(new KovasznayProblem(Re));
  problems["TaylorGreen"] = Teuchos::rcp(new TaylorGreenProblem(Re));
  Teuchos::RCP<IncompressibleProblem> problem = problems.at(problemChoice);

  MeshTopologyPtr spaceTimeMeshTopo = problem->meshTopology();

  SpaceTimeIncompressibleFormulationPtr form = Teuchos::rcp(new SpaceTimeIncompressibleFormulation(spaceDim, 1./Re,
        useConformingTraces, spaceTimeMeshTopo, p, delta_p, norm, problem->forcingTerm, ""));

  MeshPtr mesh = form->solutionUpdate()->mesh();
  MeshPtr k0Mesh = Teuchos::rcp( new Mesh (spaceTimeMeshTopo->deepCopy(), form->bf(), 1, delta_p) );
  mesh->registerObserver(k0Mesh);

  // Set up boundary conditions
  problem->setBCs(form);

  // Set up solution
  SolutionPtr solutionUpdate = form->solutionUpdate();
  SolutionPtr solutionBackground = form->solutionBackground();
  // solutionBackground->projectOntoMesh(exactMap);

  RefinementStrategyPtr refStrategy = form->getRefinementStrategy();

  ostringstream solnName;
  solnName << "incompressible" << spaceDim << "D_" << norm << "_" << Re << "_p" << p << "_" << solverChoice;
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
    double l2Update = 1e10;
    int iterCount = 0;
    solverTime->start(true);
    while (l2Update > nonlinearTolerance && iterCount < maxNonlinearIterations)
    {
      Teuchos::RCP<GMGSolver> gmgSolver;
      if (solverChoice[0] == 'G')
      {
        gmgSolver = Teuchos::rcp( new GMGSolver(solutionUpdate, k0Mesh, maxLinearIterations, solverTolerance, Solver::getDirectSolver(true), useStaticCondensation));
        gmgSolver->setAztecOutput(azOutput);
        if (solverChoice == "GMG-Direct")
          gmgSolver->gmgOperator().setSchwarzFactorizationType(GMGOperator::Direct);
        if (solverChoice == "GMG-ILU")
          gmgSolver->gmgOperator().setSchwarzFactorizationType(GMGOperator::ILU);
        if (solverChoice == "GMG-IC")
          gmgSolver->gmgOperator().setSchwarzFactorizationType(GMGOperator::IC);
        solutionUpdate->solve(gmgSolver);
      }
      else
        solutionUpdate->condensedSolve(solvers[solverChoice]);

      // Compute L2 norm of update
      double u1L2Update = solutionUpdate->L2NormOfSolutionGlobal(form->u(1)->ID());
      double u2L2Update = solutionUpdate->L2NormOfSolutionGlobal(form->u(2)->ID());
      l2Update = sqrt(u1L2Update*u1L2Update + u2L2Update*u2L2Update);
      if (commRank == 0)
        cout << "Nonlinear Update:\t " << l2Update << endl;

      form->updateSolution();
      iterCount++;
    }
    double solveTime = solverTime->stop();

    double energyError = solutionUpdate->energyErrorTotal();
    double l2Error = 0;
    if (computeL2Error)
    {
      l2Error = problem->computeL2Error(form, solutionBackground);
    }
    if (commRank == 0)
    {
      cout << "Refinement: " << refIndex
        << " \tElements: " << mesh->numActiveElements()
        << " \tDOFs: " << mesh->numGlobalDofs()
        << " \tEnergy Error: " << energyError
        << " \tL2 Error: " << l2Error
        << " \tSolve Time: " << solveTime
        // << " \tIteration Count: " << iterationCount
        << endl;
      dataFile << refIndex
        << " " << mesh->numActiveElements()
        << " " << mesh->numGlobalDofs()
        << " " << energyError
        << " " << l2Error
        << " " << solveTime
        // << " " << iterationCount
        << endl;
    }

    if (exportSolution)
      exporter->exportSolution(solutionBackground, refIndex);

    if (refIndex != numRefs)
      refStrategy->refine();
  }
  dataFile.close();

  return 0;
}
