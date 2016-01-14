#include "QPCommon.h"
#include <Eigen/StdVector>
#include "drakeMexUtil.h"
#include "controlMexUtil.h"
#include "yamlUtil.h"
#include "Path.h"

// #include <limits>
// #include <cmath>
// #include "drakeUtil.h"

using namespace std;
using namespace Eigen;
using namespace YAML;


vector<int> findPositionIndices(const RigidBodyTree &robot, const vector<string> &joint_names) {
  vector<int> position_indices;
  position_indices.reserve(joint_names.size());
  for (const auto& joint_name : joint_names) {
    const RigidBody &body = *robot.findJoint(joint_name);
    for (int i = 0; i < body.getJoint().getNumPositions(); i++) {
      position_indices.push_back(body.position_num_start + i);
    }
  }
  return position_indices;
}

RobotPropertyCache parseKinematicTreeMetadata(const YAML::Node& metadata, const RigidBodyTree& robot) {
  RobotPropertyCache ret;
  map<Side, string> side_identifiers = { {Side::RIGHT, "r"}, {Side::LEFT, "l"} };

  Node body_names = metadata["body_names"];
  Node feet = body_names["feet"];
  for (const auto& side : Side::values) {
    ret.foot_ids[side] = robot.findLinkId(feet[side_identifiers[side]].as<string>());
  }

  Node joint_group_names = metadata["joint_group_names"];
  for (const auto& side : Side::values) {
    auto side_id = side_identifiers.at(side);
    ret.position_indices.legs[side] = findPositionIndices(robot, joint_group_names["legs"][side_id].as<vector<string>>());
    ret.position_indices.knees[side] = robot.findJoint(joint_group_names["knees"][side_id].as<string>())->position_num_start;
    ret.position_indices.ankles[side] = findPositionIndices(robot, joint_group_names["ankles"][side_id].as<vector<string>>());
    ret.position_indices.arms[side] = findPositionIndices(robot, joint_group_names["arms"][side_id].as<vector<string>>());
  }
  ret.position_indices.neck = findPositionIndices(robot, joint_group_names["neck"].as<vector<string>>());
  ret.position_indices.back_bkz = robot.findJoint(joint_group_names["back_bkz"].as<string>())->position_num_start;
  ret.position_indices.back_bky = robot.findJoint(joint_group_names["back_bky"].as<string>())->position_num_start;

  return ret;
}

void parseIntegratorParams(const mxArray *params_obj, IntegratorParams &params) {
  const mxArray *pobj;
  pobj = myGetField(params_obj, "gains");
  Map<VectorXd> gains(mxGetPrSafe(pobj), mxGetNumberOfElements(pobj));
  params.gains = gains;

  pobj = myGetField(params_obj, "clamps");
  Map<VectorXd> clamps(mxGetPrSafe(pobj), mxGetNumberOfElements(pobj));
  params.clamps = clamps;

  pobj = myGetField(params_obj, "eta");
  params.eta = mxGetScalar(pobj);

  return;
}

void parseJointSoftLimits(const mxArray *params_obj, const RigidBodyTree& r, JointSoftLimitParams *params) {
  int nq = r.num_positions;

  if (mxGetNumberOfElements(params_obj) != nq)
    mexErrMsgTxt("Joint soft limits should be of size nq");

  params->enabled = Matrix<bool, Dynamic, 1>::Zero(nq);
  params->disable_when_body_in_support = VectorXi::Zero(nq);
  params->lb = VectorXd::Zero(nq);
  params->ub = VectorXd::Zero(nq);
  params->kp = VectorXd::Zero(nq);
  params->kd = VectorXd::Zero(nq);
  params->weight = VectorXd::Zero(nq);
  params->k_logistic = VectorXd::Zero(nq);

  for (int i=0; i < nq; i++) {
    params->enabled(i) = static_cast<bool> (mxGetScalar(mxGetFieldSafe(params_obj, i, "enabled")));
    params->disable_when_body_in_support(i) = static_cast<int> (mxGetScalar(mxGetFieldSafe(params_obj, i, "disable_when_body_in_support")));
    params->lb(i) = mxGetScalar(mxGetFieldSafe(params_obj, i, "lb"));
    params->ub(i) = mxGetScalar(mxGetFieldSafe(params_obj, i, "ub"));
    params->kp(i) = mxGetScalar(mxGetFieldSafe(params_obj, i, "kp"));
    params->kd(i) = mxGetScalar(mxGetFieldSafe(params_obj, i, "kd"));
    params->weight(i) = mxGetScalar(mxGetFieldSafe(params_obj, i, "weight"));
    params->k_logistic(i) = mxGetScalar(mxGetFieldSafe(params_obj, i, "k_logistic"));
  }
  return;
}

void parseWholeBodyParams(const mxArray *params_obj, const RigidBodyTree& r, WholeBodyParams *params) {
  const mxArray *int_obj = myGetField(params_obj, "integrator");
  const mxArray *qddbound_obj = myGetField(params_obj, "qdd_bounds");
  int nq = r.num_positions;
  int nv = r.num_velocities;

  if (mxGetNumberOfElements(myGetField(params_obj, "Kp")) != nq) mexErrMsgTxt("Kp should be of size nq");
  if (mxGetNumberOfElements(myGetField(params_obj, "Kd")) != nq) mexErrMsgTxt("Kd should be of size nq");
  if (mxGetNumberOfElements(myGetField(params_obj, "w_qdd")) != nv) mexErrMsgTxt("w_qdd should be of size nv");
  if (mxGetNumberOfElements(myGetField(int_obj, "gains")) != nq) mexErrMsgTxt("gains should be of size nq");
  if (mxGetNumberOfElements(myGetField(int_obj, "clamps")) != nq) mexErrMsgTxt("clamps should be of size nq");
  if (mxGetNumberOfElements(myGetField(qddbound_obj, "min")) != nv) mexErrMsgTxt("qdd min should be of size nv");
  if (mxGetNumberOfElements(myGetField(qddbound_obj, "max")) != nv) mexErrMsgTxt("qdd max should be of size nv");

  Map<VectorXd>Kp(mxGetPrSafe(myGetField(params_obj, "Kp")), mxGetNumberOfElements(myGetField(params_obj, "Kp")));
  params->Kp = Kp;

  Map<VectorXd>Kd(mxGetPrSafe(myGetField(params_obj, "Kd")), mxGetNumberOfElements(myGetField(params_obj, "Kd")));
  params->Kd = Kd;

  Map<VectorXd>w_qdd(mxGetPrSafe(myGetField(params_obj, "w_qdd")), mxGetNumberOfElements(myGetField(params_obj, "w_qdd")));
  params->w_qdd = w_qdd;

  parseIntegratorParams(int_obj, params->integrator);

  Map<VectorXd>min(mxGetPrSafe(myGetField(qddbound_obj, "min")), mxGetNumberOfElements(myGetField(qddbound_obj, "min")));
  params->qdd_bounds.min = min;

  Map<VectorXd>max(mxGetPrSafe(myGetField(qddbound_obj, "max")), mxGetNumberOfElements(myGetField(qddbound_obj, "max")));
  params->qdd_bounds.max = max;
  return;
}

void parseBodyMotionParams(const mxArray *params_obj, int i, BodyMotionParams *params) {
  const mxArray *pobj;
  const mxArray *bounds_obj = myGetField(params_obj, i, "accel_bounds");

  pobj = myGetField(params_obj, i, "Kp");
  sizecheck(pobj, 6, 1);
  Map<Vector6d>Kp(mxGetPrSafe(pobj));
  params->Kp = Kp;

  pobj = myGetField(params_obj, i, "Kd");
  sizecheck(pobj, 6, 1);
  Map<Vector6d>Kd(mxGetPrSafe(pobj));
  params->Kd = Kd;

  pobj = myGetField(params_obj, i, "weight");
  sizecheck(pobj, 1, 1);
  params->weight = mxGetScalar(pobj);

  pobj = myGetField(bounds_obj, "min");
  sizecheck(pobj, 6, 1);
  Map<Vector6d>min(mxGetPrSafe(pobj));
  params->accel_bounds.min = min;

  pobj = myGetField(bounds_obj, "max");
  sizecheck(pobj, 6, 1);
  Map<Vector6d>max(mxGetPrSafe(pobj));
  params->accel_bounds.max = max;
  return;
}

void parseVRefIntegratorParams(const mxArray *params_obj, VRefIntegratorParams *params) {
  const mxArray *pobj;

  pobj = myGetField(params_obj, "zero_ankles_on_contact");
  if (!mxIsDouble(pobj)) mexErrMsgTxt("zero_ankles_on_contact should be a double (yes, even though it's treated as a logical. sorry...)");
  sizecheck(pobj, 1, 1);
  params->zero_ankles_on_contact = (mxGetScalar(pobj) != 0);

  pobj = myGetField(params_obj, "eta");
  sizecheck(pobj, 1, 1);
  params->eta = mxGetScalar(pobj);

  pobj = myGetField(params_obj, "delta_max");
  sizecheck(pobj, 1, 1);
  params->delta_max = mxGetScalar(pobj);

  return;
}

void parseHardwareGains(const mxArray *params_obj, const RigidBodyTree& r, HardwareGains *params) {
  const mxArray *pobj;
  int nu = r.num_velocities - 6;

  pobj = myGetField(params_obj, "k_f_p");
  sizecheck(pobj, nu, 1);
  Map<VectorXd> k_f_p(mxGetPrSafe(pobj), nu);
  params->k_f_p = k_f_p;

  pobj = myGetField(params_obj, "k_q_p");
  sizecheck(pobj, nu, 1);
  Map<VectorXd> k_q_p(mxGetPrSafe(pobj), nu);
  params->k_q_p = k_q_p;

  pobj = myGetField(params_obj, "k_q_i");
  sizecheck(pobj, nu, 1);
  Map<VectorXd> k_q_i(mxGetPrSafe(pobj), nu);
  params->k_q_i = k_q_i;

  pobj = myGetField(params_obj, "k_qd_p");
  sizecheck(pobj, nu, 1);
  Map<VectorXd> k_qd_p(mxGetPrSafe(pobj), nu);
  params->k_qd_p = k_qd_p;

  pobj = myGetField(params_obj, "ff_qd");
  sizecheck(pobj, nu, 1);
  Map<VectorXd> ff_qd(mxGetPrSafe(pobj), nu);
  params->ff_qd = ff_qd;

  pobj = myGetField(params_obj, "ff_f_d");
  sizecheck(pobj, nu, 1);
  Map<VectorXd> ff_f_d(mxGetPrSafe(pobj), nu);
  params->ff_f_d = ff_f_d;

  pobj = myGetField(params_obj, "ff_const");
  sizecheck(pobj, nu, 1);
  Map<VectorXd> ff_const(mxGetPrSafe(pobj), nu);
  params->ff_const = ff_const;

  pobj = myGetField(params_obj, "ff_qd_d");
  sizecheck(pobj, nu, 1);
  Map<VectorXd> ff_qd_d(mxGetPrSafe(pobj), nu);
  params->ff_qd_d = ff_qd_d;
  return;
}

void parseHardwareParams(const mxArray *params_obj, const RigidBodyTree& r, HardwareParams *params) {
  const mxArray *pobj;

  parseHardwareGains(myGetField(params_obj, "gains"), r, &(params->gains));

  int nu = r.num_velocities - 6;
  params->joint_is_force_controlled = Matrix<bool, Dynamic, 1>::Zero(nu);
  params->joint_is_position_controlled = Matrix<bool, Dynamic, 1>::Zero(nu);

  pobj = myGetField(params_obj, "joint_is_position_controlled");
  sizecheck(pobj, nu, 1);
  Map<VectorXd>pos_double(mxGetPrSafe(pobj), nu);

  pobj = myGetField(params_obj, "joint_is_force_controlled");
  sizecheck(pobj, nu, 1);
  Map<VectorXd>force_double(mxGetPrSafe(pobj), nu);

  for (int i=0; i < nu; i++) {
    params->joint_is_force_controlled(i) = force_double(i) > 0.5;
    params->joint_is_position_controlled(i) = pos_double(i) > 0.5;
  }
  return;
}

void parseQPControllerParams(const mxArray *params_obj, const RigidBodyTree& r, QPControllerParams *params) {
  const mxArray *pobj;

  pobj = myGetProperty(params_obj, "W_kdot");
  sizecheck(pobj, 3, 3);
  Map<Matrix3d>W_kdot(mxGetPrSafe(pobj));
  params->W_kdot = W_kdot;

  pobj = myGetProperty(params_obj, "Kp_ang");
  sizecheck(pobj, 1, 1);
  params->Kp_ang = mxGetScalar(pobj);

  pobj = myGetProperty(params_obj, "w_slack");
  sizecheck(pobj, 1, 1);
  params->w_slack = mxGetScalar(pobj);

  pobj = myGetProperty(params_obj, "slack_limit");
  sizecheck(pobj, 1, 1);
  params->slack_limit = mxGetScalar(pobj);

  pobj = myGetProperty(params_obj, "w_grf");
  sizecheck(pobj, 1, 1);
  params->w_grf = mxGetScalar(pobj);

  pobj = myGetProperty(params_obj, "Kp_accel");
  sizecheck(pobj, 1, 1);
  params->Kp_accel = mxGetScalar(pobj);

  pobj = myGetProperty(params_obj, "contact_threshold");
  sizecheck(pobj, 1, 1);
  params->contact_threshold = mxGetScalar(pobj);

  pobj = myGetProperty(params_obj, "min_knee_angle");
  sizecheck(pobj, 1, 1);
  params->min_knee_angle = mxGetScalar(pobj);

  params->center_of_mass_observer_gain = matlabToEigenMap<4, 4>(mxGetPropertySafe(params_obj, "center_of_mass_observer_gain"));

  pobj = mxGetPropertySafe(params_obj, "use_center_of_mass_observer");
  sizecheck(pobj,1,1);
  params->use_center_of_mass_observer = mxIsLogicalScalarTrue(pobj);

  parseWholeBodyParams(myGetProperty(params_obj, "whole_body"), r, &(params->whole_body));
  parseVRefIntegratorParams(myGetProperty(params_obj, "vref_integrator"), &(params->vref_integrator));
  parseJointSoftLimits(myGetProperty(params_obj, "joint_soft_limits"), r, &(params->joint_soft_limits));

  BodyMotionParams body_motion_params;
  const mxArray *body_motion_obj = myGetProperty(params_obj, "body_motion");
  int num_tracked_bodies = mxGetNumberOfElements(body_motion_obj);
  params->body_motion.resize(num_tracked_bodies);
  for (int i=0; i < num_tracked_bodies; i++) {
    parseBodyMotionParams(body_motion_obj, i, &body_motion_params);
    params->body_motion[i] = body_motion_params;
  }

  parseHardwareParams(myGetProperty(params_obj, "hardware"), r, &(params->hardware));
  return;
}

void parseQPControllerParamSets(const mxArray *pobj, const RigidBodyTree& r, map<string,QPControllerParams> *param_sets) {
  int num_fields = mxGetNumberOfFields(pobj);
  if (num_fields == 0) mexErrMsgTxt("could not get any field names from the param_sets object\n"); 

  QPControllerParams params(r);
  const char* fieldname;
  for (int i=0; i < num_fields; i++) {
    fieldname = mxGetFieldNameByNumber(pobj, i);
    parseQPControllerParams(myGetField(pobj, fieldname), r, &params);
    param_sets->insert(pair<string,QPControllerParams>(string(fieldname), params));
  }

  return;
}

JointNames parseRobotJointNames(const string& hardware_data_file_name, const RigidBodyTree& tree) {
  JointNames ret;
  ret.drake.resize(tree.actuators.size());
  transform(tree.actuators.begin(), tree.actuators.end(), ret.drake.begin(),
            [](const RigidBodyActuator &actuator) { return actuator.body->getJoint().getName(); });
  Node hardware_data = LoadFile(hardware_data_file_name);
  ret.robot = hardware_data["joint_names"].as<vector<string>>();

  return ret;
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  if (nrhs<1) mexErrMsgTxt("usage: ptr = constructQPDataPointerMex(robot_obj, control_config_filename, B, umin, umax, gurobi_opts);");

  if (nrhs == 1) {
    // By convention, calling the constructor with just one argument (the pointer) should delete the pointer
    if (isa(prhs[0],"DrakeMexPointer")) { 
      destroyDrakeMexPointer<NewQPControllerData*>(prhs[0]);
      return;
    } else {
      mexErrMsgIdAndTxt("Drake:constructQPDataPointerMex:BadInputs", "Expected a DrakeMexPointer (or a subclass)");
    }
  }


  if (nlhs<1) mexErrMsgTxt("take at least one output... please.");

  int narg = 0;

  auto urdf_filename = mxGetStdString(prhs[narg++]);
  unique_ptr<RigidBodyTree> robot_ptr(new RigidBodyTree(urdf_filename));
  set<string> collision_groups_to_keep = {"heel", "toe"};
  auto filter = [&](const string &group_name) { return collision_groups_to_keep.find(group_name) == collision_groups_to_keep.end(); };
  robot_ptr->removeCollisionGroupsIf(filter);
  robot_ptr->compile();

  NewQPControllerData* pdata = new NewQPControllerData(move(robot_ptr));


  // kinematic tree metadata & param sets
  std::string control_config_filename = mxGetStdString(prhs[narg]);
  YAML::Node control_config = LoadFile(control_config_filename);
  pdata->param_sets = loadAllParamSets(control_config["qp_controller_params"], *(pdata->r)); 
  pdata->rpc = parseKinematicTreeMetadata(control_config["kinematic_tree_metadata"], *(pdata->r));
  narg++;

  // umin
  int nq = pdata->r->num_positions, nu = static_cast<int>(pdata->r->actuators.size());

  pdata->umin.resize(nu);
  pdata->umax.resize(nu);
  for (int i = 0; i < pdata->r->actuators.size(); i++) {
    pdata->umin(i) = pdata->r->actuators.at(i).effort_limit_min;
    pdata->umax(i) = pdata->r->actuators.at(i).effort_limit_max;
  }

  // use_fast_qp
  pdata->use_fast_qp = (int) mxGetScalar(prhs[narg]);
  narg++;

  // gurobi_opts
  const mxArray* psolveropts = prhs[narg];
  narg++;

  // input_coordinate_names
  pdata->input_joint_names = parseRobotJointNames(mxGetStdString(prhs[narg]), *(pdata->r));
  narg++;

  // Done parsing inputs


  pdata->qdd_lb = VectorXd::Zero(nq).array() - numeric_limits<double>::infinity();
  pdata->qdd_ub = VectorXd::Zero(nq).array() + numeric_limits<double>::infinity();

  //create gurobi environment
  int error = GRBloadenv(&(pdata->env),NULL);
  if (error) {
    mexPrintf("Gurobi error code: %d\n", error);
    mexErrMsgTxt("Cannot load gurobi environment");
  }

  // set solver params (http://www.gurobi.com/documentation/5.5/reference-manual/node798#sec:Parameters)
  int method = (int) mxGetScalar(myGetField(psolveropts,"method"));
  CGE ( GRBsetintparam(pdata->env,"outputflag",0), pdata->env );
  CGE ( GRBsetintparam(pdata->env,"method",method), pdata->env );
  // CGE ( GRBsetintparam(pdata->env,"method",method), pdata->env );
  CGE ( GRBsetintparam(pdata->env,"presolve",0), pdata->env );
  if (method==2) {
    CGE ( GRBsetintparam(pdata->env,"bariterlimit",20), pdata->env );
    CGE ( GRBsetintparam(pdata->env,"barhomogeneous",0), pdata->env );
    CGE ( GRBsetdblparam(pdata->env,"barconvtol",0.0005), pdata->env );
  }

  // preallocate some memory
  pdata->H.resize(nq,nq);
  pdata->H_float.resize(6,nq);
  pdata->H_act.resize(nu,nq);

  pdata->C.resize(nq);
  pdata->C_float.resize(6);
  pdata->C_act.resize(nu);

  pdata->J.resize(3,nq);
  pdata->J_xy.resize(2,nq);
  pdata->Hqp.resize(nq,nq);
  pdata->fqp.resize(nq);
  pdata->Ag.resize(6,nq);
  pdata->Ak.resize(3,nq);

  pdata->state.vbasis_len = 0;
  pdata->state.cbasis_len = 0;
  pdata->state.vbasis = NULL;
  pdata->state.cbasis = NULL;

  pdata->state.t_prev = 0;
  pdata->state.vref_integrator_state = VectorXd::Zero(pdata->r->num_velocities);
  pdata->state.q_integrator_state = VectorXd::Zero(pdata->r->num_positions);
  pdata->state.foot_contact_prev[0] = false;
  pdata->state.foot_contact_prev[1] = false;
  pdata->state.num_active_contact_pts = 0;

  pdata->state.center_of_mass_observer_state = Vector4d::Zero();
  pdata->state.last_com_ddot = Vector3d::Zero();

  plhs[0] = createDrakeMexPointer((void*) pdata, "NewQPControllerData");

  return;
}












