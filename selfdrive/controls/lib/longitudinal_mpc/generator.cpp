#include <math.h>
#include <acado_code_generation.hpp>

const int controlHorizon = 50;

using namespace std;

#define G 9.81

#define RW(v_ego, v_l, time_gap) (v_ego * time_gap - (v_l - v_ego) * time_gap + v_ego*v_ego/(2*G) - v_l*v_l / (2*G))
#define NORM_RW_ERROR(v_ego, v_l, p, time_gap) ((RW(v_ego, v_l, time_gap) + 4.0 - p)/(sqrt(v_ego + 0.5) + 0.1))

int main( )
{
  USING_NAMESPACE_ACADO


  DifferentialEquation f;

  DifferentialState x_ego, v_ego, a_ego;
  OnlineData x_l, v_l;

  Control j_ego;

  // follow distance expressed as a stopping distance in seconds
  // see https://github.com/rhinodavid/CommaButtons
  // see https://github.com/acado/acado/issues/54 for a discussion of `OnlineData`
  OnlineData time_gap;

  /**
   * openpilot targets a 4m distance behind a target vehicle while stopped.
   * When stopping (at a light, for instance) this car-length distance feels
   * a bit awkward. Let's attempt to scale this down as the car comes to a stop.
   *
   * At ~20 mph, start closing the distance from 4m, down to 1.5m as the car stops
   *
   * 9.0 m/s = 20.13 mi/h
   *
   * Use a sigmoid function (check out a visualization:
   * https://www.desmos.com/calculator/ev4n5otjw0)
   *
   *           2.75
   * ---------------------- + 1.25 = follow dist const
   *      (2.2 - 0.9 * v_ego)
   * 1 + e
   */

  auto follow_const_m = (2.75 / (1 + exp(2.2 - 0.9 * v_ego))) + 1.25;
  auto desired = follow_const_m + RW(v_ego, v_l, time_gap);
  auto d_l = x_l - x_ego;

  // Equations of motion
  f << dot(x_ego) == v_ego;
  f << dot(v_ego) == a_ego;
  f << dot(a_ego) == j_ego;

  // Running cost
  Function h;
  h << exp(0.3 * NORM_RW_ERROR(v_ego, v_l, d_l, time_gap));
  h << (d_l - desired) / (0.05 * v_ego + 0.5);
  h << a_ego * (0.1 * v_ego + 1.0);
  h << j_ego * (0.1 * v_ego + 1.0);

  // Weights are defined in mpc.
  BMatrix Q(4,4); Q.setAll(true);

  // Terminal cost
  Function hN;
  hN << exp(0.3 * NORM_RW_ERROR(v_ego, v_l, d_l, time_gap));
  hN << (d_l - desired) / (0.05 * v_ego + 0.5);
  hN << a_ego * (0.1 * v_ego + 1.0);

  // Weights are defined in mpc.
  BMatrix QN(3,3); QN.setAll(true);

  // Non uniform time grid
  // First 5 timesteps are 0.2, after that it's 0.6
  DMatrix numSteps(20, 1);
  for (int i = 0; i < 5; i++){
    numSteps(i) = 1;
  }
  for (int i = 5; i < 20; i++){
    numSteps(i) = 3;
  }

  // Setup Optimal Control Problem
  const double tStart = 0.0;
  const double tEnd   = 10.0;

  OCP ocp( tStart, tEnd, numSteps);
  ocp.subjectTo(f);

  ocp.minimizeLSQ(Q, h);
  ocp.minimizeLSQEndTerm(QN, hN);

  ocp.subjectTo( 0.0 <= v_ego);
  ocp.setNOD(3);

  OCPexport mpc(ocp);
  mpc.set( HESSIAN_APPROXIMATION, GAUSS_NEWTON );
  mpc.set( DISCRETIZATION_TYPE, MULTIPLE_SHOOTING );
  mpc.set( INTEGRATOR_TYPE, INT_RK4 );
  mpc.set( NUM_INTEGRATOR_STEPS, controlHorizon);
  mpc.set( MAX_NUM_QP_ITERATIONS, 500);
  mpc.set( CG_USE_VARIABLE_WEIGHTING_MATRIX, YES);

  mpc.set( SPARSE_QP_SOLUTION, CONDENSING );
  mpc.set( QP_SOLVER, QP_QPOASES );
  mpc.set( HOTSTART_QP, YES );
  mpc.set( GENERATE_TEST_FILE, NO);
  mpc.set( GENERATE_MAKE_FILE, NO );
  mpc.set( GENERATE_MATLAB_INTERFACE, NO );
  mpc.set( GENERATE_SIMULINK_INTERFACE, NO );

  if (mpc.exportCode( "lib_mpc_export" ) != SUCCESSFUL_RETURN)
    exit( EXIT_FAILURE );

  mpc.printDimensionsQP( );

  return EXIT_SUCCESS;
}
