#include "InnerProductScratchPad.h"
#include "Mesh.h"
#include "Solution.h"

// class TimeIntegrator;
// typedef Teuchos::RCP<TimeIntegrator> TimeIntegratorPtr;

class InvDtFunction : public Function
{
  private:
    double __invDt;

  public:
    InvDtFunction(double _dt) : Function(0){
      __invDt = 1./_dt;
    }
    void setDt(double _dt){
      __invDt = 1./_dt;
    }
    double getDt(){
      return 1./__invDt;
    }
    void values(FieldContainer<double> &values, BasisCachePtr basisCache) {
      CHECK_VALUES_RANK(values);
      values.initialize(__invDt);
    }
};

class SteadyResidual
{
  protected:
    VarFactory &varFactory;
  public:
    SteadyResidual(VarFactory &varFactory):varFactory(varFactory) {};
    virtual LinearTermPtr createResidual(SolutionPtr solution) = 0;
};

class TimeIntegrator
{
  protected:
    BFPtr _steadyJacobian;
    SteadyResidual &_steadyResidual;
    Teuchos::RCP<RHSEasy> _rhs;
    SolutionPtr _solution;
    SolutionPtr _prevTimeSolution;
    SolutionPtr _prevNLSolution;
    SolutionPtr _zeroSolution;
    FunctionPtr _invDt;
    double _t;
    double _dt;
    int _timestep;
    bool _nonlinear;
    double _nlTolerance;
    double _nlL2Error;
    int _nlIteration;

  public:
    TimeIntegrator(BFPtr steadyJacobian, SteadyResidual &steadyResidual, MeshPtr mesh,
        Teuchos::RCP<BCEasy> bc, IPPtr ip, map<int, FunctionPtr> initialCondition, bool nonlinear);
    SolutionPtr solution();
    SolutionPtr prevSolution();
    FunctionPtr invDt();
    void setNLTolerance(double tol) { _nlTolerance = tol; }
    double getNLTolerance() { return _nlTolerance; }
    virtual void addTimeTerm(VarPtr trialVar, VarPtr testVar, FunctionPtr multiplier);
    virtual void runToTime(double T, double dt) = 0;
    virtual void calcNextTimeStep(double dt);
    void printTimeStepMessage();
    void printNLMessage();
};

class ImplicitEulerIntegrator : public TimeIntegrator
{
  private:

  public:
    ImplicitEulerIntegrator(BFPtr steadyJacobian, SteadyResidual &steadyResidual, MeshPtr mesh,
        Teuchos::RCP<BCEasy> bc, IPPtr ip, map<int, FunctionPtr> initialCondition, bool nonlinear);
    void runToTime(double T, double dt);
};

// class ESDIRKIntegrator : public TimeIntegrator
// {
//   private:
//     // Standard Butcher tables run from 1 to s
//     // I am running from 0 to s-1 to match 0 index
//     int _numStages;
//     vector< vector<double> > a;
//     vector<double> b;
//     // For ESDIRK schemes, stage 1 is _prevTimeSolution
//     vector< SolutionPtr > _stageSolution;
//     vector< Teuchos::RCP<RHSEasy> > _stageRHS;
//     vector< LinearTermPtr > _steadyLinearTerm;
//
//   public:
//
//     ESDIRKIntegrator(BFPtr steadyBF, Teuchos::RCP<RHSEasy> steadyRHS, MeshPtr mesh,
//         Teuchos::RCP<BCEasy> bc, IPPtr ip, map<int, FunctionPtr> initialCondition, int numStages, bool nonlinear);
//     virtual void addTimeTerm(VarPtr trialVar, VarPtr testVar, FunctionPtr multiplier);
//     virtual void runToTime(double T, double dt);
//     virtual void calcNextTimeStep(double dt);
// };
