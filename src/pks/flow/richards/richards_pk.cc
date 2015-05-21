/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */

/* -------------------------------------------------------------------------
This is the flow component of the Amanzi code.
License: BSD
Authors: Neil Carlson (version 1)
         Konstantin Lipnikov (version 2) (lipnikov@lanl.gov)
         Ethan Coon (ATS version) (ecoon@lanl.gov)
------------------------------------------------------------------------- */
#include "boost/math/special_functions/fpclassify.hpp"

#include "Epetra_Import.h"

#include "flow_bc_factory.hh"

#include "Point.hh"

#include "upwind_cell_centered.hh"
#include "upwind_arithmetic_mean.hh"
#include "upwind_total_flux.hh"
#include "upwind_gravity_flux.hh"

#include "composite_vector_function.hh"
#include "composite_vector_function_factory.hh"

#include "predictor_delegate_bc_flux.hh"
#include "wrm_evaluator.hh"
#include "rel_perm_evaluator.hh"
#include "richards_water_content.hh"
#include "OperatorDefs.hh"

#include "richards.hh"

#define DEBUG_RES_FLAG 0


namespace Amanzi {
namespace Flow {

// -------------------------------------------------------------
// Constructor
// -------------------------------------------------------------
Richards::Richards(const Teuchos::RCP<Teuchos::ParameterList>& plist,
                   Teuchos::ParameterList& FElist,
                   const Teuchos::RCP<TreeVector>& solution) :
    PKDefaultBase(plist, FElist, solution),
    PKPhysicalBDFBase(plist, FElist, solution),
    coupled_to_surface_via_head_(false),
    coupled_to_surface_via_flux_(false),
    infiltrate_only_if_unfrozen_(false),
    modify_predictor_with_consistent_faces_(false),
    modify_predictor_wc_(false),
    modify_predictor_bc_flux_(false),
    upwind_from_prev_flux_(false),
    precon_wc_(false),
    niter_(0),
    dynamic_mesh_(false),
    clobber_surf_kr_(false),
    vapor_diffusion_(false),
    perm_scale_(1.)
{
  // set a few parameters before setup
  plist_->set("primary variable key", "pressure");
  plist_->sublist("primary variable evaluator").set("manage communication", true);
}

// -------------------------------------------------------------
// Setup data
// -------------------------------------------------------------
void Richards::setup(const Teuchos::Ptr<State>& S) {
  PKPhysicalBDFBase::setup(S);
  SetupRichardsFlow_(S);
  SetupPhysicalEvaluators_(S);

  flux_tol_ = plist_->get<double>("flux tolerance", 1.);
};


// -------------------------------------------------------------
// Pieces of the construction process that are common to all
// Richards-like PKs.
// -------------------------------------------------------------
void Richards::SetupRichardsFlow_(const Teuchos::Ptr<State>& S) {

  // Require fields and evaluators for those fields.
  std::vector<AmanziMesh::Entity_kind> locations2(2);
  std::vector<std::string> names2(2);
  std::vector<int> num_dofs2(2,1);
  locations2[0] = AmanziMesh::CELL;
  locations2[1] = AmanziMesh::FACE;
  names2[0] = "cell";
  names2[1] = "face";


  std::vector<AmanziMesh::Entity_kind> locations1(2);
  std::vector<std::string> names1(2);
  std::vector<int> num_dofs1(2,1);
  locations1[0] = AmanziMesh::CELL;
  locations1[1] = AmanziMesh::BOUNDARY_FACE;
  names1[0] = "cell";
  names1[1] = "boundary_face";

  if (!(plist_->sublist("Diffusion").get<bool>("TPFA use cells only", false)) ){
      // -- primary variable: pressure on both cells and faces, ghosted, with 1 dof
    S->RequireField(key_, name_)->SetMesh(mesh_)->SetGhosted()
                          ->SetComponents(names2, locations2, num_dofs2);
  }
  else {
      // -- primary variable: pressure on both cells and boundary faces, ghosted, with 1 dof
    S->RequireField(key_, name_)->SetMesh(mesh_)->SetGhosted()
                           ->SetComponents(names1, locations1, num_dofs1);
  }

#if DEBUG_RES_FLAG
  // -- residuals of various iterations for debugging
  for (unsigned int i=1; i!=23; ++i) {
    std::stringstream namestream;
    namestream << "flow_residual_" << i;
    std::stringstream solnstream;
    solnstream << "flow_solution_" << i;
    S->RequireField(namestream.str(), name_)->SetMesh(mesh_)->SetGhosted()
                    ->SetComponents(names2, locations2, num_dofs2);
    S->RequireField(solnstream.str(), name_)->SetMesh(mesh_)->SetGhosted()
                    ->SetComponents(names2, locations2, num_dofs2);
  }
#endif

  // -- secondary variables, no evaluator used
  S->RequireField("darcy_flux_direction", name_)->SetMesh(mesh_)->SetGhosted()
      ->SetComponent("face", AmanziMesh::FACE, 1);
  S->RequireField("darcy_flux", name_)->SetMesh(mesh_)->SetGhosted()
                                ->SetComponent("face", AmanziMesh::FACE, 1);
  S->RequireField("darcy_velocity", name_)->SetMesh(mesh_)->SetGhosted()
                                ->SetComponent("cell", AmanziMesh::CELL, 3);

  // Get data for non-field quanitites.
  S->RequireFieldEvaluator("cell_volume");
  S->RequireGravity();
  S->RequireScalar("atmospheric_pressure");

  // Create the absolute permeability tensor.
  unsigned int c_owned = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::OWNED);
  K_ = Teuchos::rcp(new std::vector<WhetStone::Tensor>(c_owned));
  for (unsigned int c=0; c!=c_owned; ++c) {
    (*K_)[c].Init(mesh_->space_dimension(),1);
  }
  // scaling for permeability
  perm_scale_ = plist_->get<double>("permeability rescaling", 1.0);

  // source terms
  is_source_term_ = plist_->get<bool>("source term", false);
  if (is_source_term_) {
    explicit_source_ = plist_->get<bool>("explicit source term", false);
    S->RequireField("mass_source")->SetMesh(mesh_)
        ->AddComponent("cell", AmanziMesh::CELL, 1);
    S->RequireFieldEvaluator("mass_source");
  }
  
  // Create the boundary condition data structures.
  Teuchos::ParameterList bc_plist = plist_->sublist("boundary conditions", true);
  FlowBCFactory bc_factory(mesh_, bc_plist);
  bc_pressure_ = bc_factory.CreatePressure();
  bc_flux_ = bc_factory.CreateMassFlux();
  infiltrate_only_if_unfrozen_ = bc_plist.get<bool>("infiltrate only if unfrozen",false);
  bc_seepage_ = bc_factory.CreateSeepageFacePressure();
  bc_seepage_->Compute(0.); // compute at t=0 to set up

  int nfaces = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::USED);
  bc_markers_.resize(nfaces, Operators::OPERATOR_BC_NONE);
  bc_values_.resize(nfaces, 0.0);
  std::vector<double> mixed;
  bc_ = Teuchos::rcp(new Operators::BCs(Operators::OPERATOR_BC_TYPE_FACE, bc_markers_, bc_values_, mixed));
  
  // how often to update the fluxes?
  std::string updatestring = plist_->get<std::string>("update flux mode", "iteration");
  if (updatestring == "iteration") {
    update_flux_ = UPDATE_FLUX_ITERATION;
  } else if (updatestring == "timestep") {
    update_flux_ = UPDATE_FLUX_TIMESTEP;
  } else if (updatestring == "vis") {
    update_flux_ = UPDATE_FLUX_VIS;
  } else if (updatestring == "never") {
    update_flux_ = UPDATE_FLUX_NEVER;
  } else {
    Errors::Message message(std::string("Unknown frequence for updating the overland flux: ")+updatestring);
    Exceptions::amanzi_throw(message);
  }

  // coupling
  // -- coupling done by a Neumann condition
  coupled_to_surface_via_flux_ = plist_->get<bool>("coupled to surface via flux", false);
  if (coupled_to_surface_via_flux_) {
    S->RequireField("surface_subsurface_flux")
        ->SetMesh(S->GetMesh("surface"))
        ->AddComponent("cell", AmanziMesh::CELL, 1);
  }

  // -- coupling done by a Dirichlet condition
  coupled_to_surface_via_head_ = plist_->get<bool>("coupled to surface via head", false);
  if (coupled_to_surface_via_head_) {
    S->RequireField("surface_pressure");
    // override the flux update -- must happen every iteration
    update_flux_ = UPDATE_FLUX_ITERATION;
  }

  // -- Make sure coupling isn't flagged multiple ways.
  ASSERT(!(coupled_to_surface_via_flux_ && coupled_to_surface_via_head_));


  // Create the upwinding method
  S->RequireField("numerical_rel_perm", name_)->SetMesh(mesh_)->SetGhosted()
                    ->SetComponents(names2, locations2, num_dofs2);
  S->GetField("numerical_rel_perm",name_)->set_io_vis(false);
  S->RequireField("dnumerical_rel_perm_dpressure", name_)->SetMesh(mesh_)->SetGhosted()
                    ->SetComponents(names2, locations2, num_dofs2);
  S->GetField("dnumerical_rel_perm_dpressure",name_)->set_io_vis(false);

  clobber_surf_kr_ = plist_->get<bool>("clobber surface rel perm", false);
  std::string method_name = plist_->get<std::string>("relative permeability method", "upwind with gravity");
  symmetric_ = false;
  if (method_name == "upwind with gravity") {
    upwinding_ = Teuchos::rcp(new Operators::UpwindGravityFlux(name_,
            "relative_permeability", "numerical_rel_perm", K_));
    Krel_method_ = Operators::UPWIND_METHOD_GRAVITY;
  } else if (method_name == "cell centered") {
    upwinding_ = Teuchos::rcp(new Operators::UpwindCellCentered(name_,
            "relative_permeability", "numerical_rel_perm"));
    symmetric_ = true;
    Krel_method_ = Operators::UPWIND_METHOD_CENTERED;
  } else if (method_name == "upwind with Darcy flux") {
    upwind_from_prev_flux_ = plist_->get<bool>("upwind flux from previous iteration", false);
    if (upwind_from_prev_flux_) {
      upwinding_ = Teuchos::rcp(new Operators::UpwindTotalFlux(name_,
                    "relative_permeability", "numerical_rel_perm", "darcy_flux", 1.e-8));
    } else {
      upwinding_ = Teuchos::rcp(new Operators::UpwindTotalFlux(name_,
                    "relative_permeability", "numerical_rel_perm", "darcy_flux_direction", 1.e-8));
      upwinding_deriv_ = Teuchos::rcp(new Operators::UpwindTotalFlux(name_,
                    "drelative_permeability_dpressure", "dnumerical_rel_perm_dpressure", "darcy_flux_direction", 1.e-8));
    }
    Krel_method_ = Operators::UPWIND_METHOD_TOTAL_FLUX;
  } else if (method_name == "arithmetic mean") {
    upwinding_ = Teuchos::rcp(new Operators::UpwindArithmeticMean(name_,
            "relative_permeability", "numerical_rel_perm"));
    Krel_method_ = Operators::UPWIND_METHOD_ARITHMETIC_MEAN;
  } else {
    std::stringstream messagestream;
    messagestream << "Richards Flow PK has no upwinding method named: " << method_name;
    Errors::Message message(messagestream.str());
    Exceptions::amanzi_throw(message);
  }


  vapor_diffusion_ = false;
  //  vapor_diffusion_ = plist_->get<bool>("include vapor diffusion", false);
  // if (vapor_diffusion_){
  //   // Create the vapor diffusion vectors
  //   S->RequireField("vapor_diffusion_pressure", name_)->SetMesh(mesh_)->SetGhosted()->SetComponent("cell", AmanziMesh::CELL, 1);
  //   S->GetField("vapor_diffusion_pressure",name_)->set_io_vis(true);


  //   S->RequireField("vapor_diffusion_temperature", name_)->SetMesh(mesh_)->SetGhosted()
  //     ->SetComponent("cell", AmanziMesh::CELL, 1);
  //   S->GetField("vapor_diffusion_temperature",name_)->set_io_vis(true);
  // }

  // operators for the diffusion terms
  Teuchos::ParameterList& mfd_plist = plist_->sublist("Diffusion");
  matrix_diff_ = Teuchos::rcp(new Operators::OperatorDiffusionWithGravity(mfd_plist, mesh_));
  matrix_ = matrix_diff_->global_operator();

  // if (vapor_diffusion_){
  //   // operator for the vapor diffusion terms
  //   matrix_vapor_ = Operators::CreateMatrixMFD(mfd_plist, mesh_);
  //   symmetric_ = false;
  //   matrix_vapor_ ->set_symmetric(symmetric_);
  //   matrix_vapor_ ->SymbolicAssembleGlobalMatrices();
  //   matrix_vapor_ ->InitPreconditioner();
  // }

  // operator with no krel for flux direction, consistent faces
  Teuchos::ParameterList face_diff_list(mfd_plist);
  face_diff_list.set("nonlinear coefficient", "none");
  face_matrix_diff_ = Teuchos::rcp(new Operators::OperatorDiffusionWithGravity(face_diff_list, mesh_));

  // preconditioner for the NKA system
  Teuchos::ParameterList& mfd_pc_plist = plist_->sublist("Diffusion PC");
  preconditioner_diff_ = Teuchos::rcp(new Operators::OperatorDiffusionWithGravity(mfd_pc_plist, mesh_));
  preconditioner_ = preconditioner_diff_->global_operator();
  preconditioner_acc_ = Teuchos::rcp(new Operators::OperatorAccumulation(AmanziMesh::CELL, preconditioner_));

  // wc preconditioner
  precon_used_ = plist_->isSublist("preconditioner");
  precon_wc_ = plist_->get<bool>("precondition using WC", false);

  // predictors for time integration
  modify_predictor_with_consistent_faces_ =
    plist_->get<bool>("modify predictor with consistent faces", false);
  modify_predictor_bc_flux_ =
    plist_->get<bool>("modify predictor for flux BCs", false);
  modify_predictor_first_bc_flux_ =
    plist_->get<bool>("modify predictor for initial flux BCs", false);
  modify_predictor_wc_ =
    plist_->get<bool>("modify predictor via water content", false);

}


// -------------------------------------------------------------
// Create the physical evaluators for water content, water
// retention, rel perm, etc, that are specific to Richards.
// -------------------------------------------------------------
void Richards::SetupPhysicalEvaluators_(const Teuchos::Ptr<State>& S) {
  // -- Absolute permeability.
  //       For now, we assume scalar permeability.  This will change.
  S->RequireField("permeability")->SetMesh(mesh_)->SetGhosted()
      ->AddComponent("cell", AmanziMesh::CELL, 1);
  S->RequireFieldEvaluator("permeability");

  // -- water content, and evaluator
  S->RequireField("water_content")->SetMesh(mesh_)->SetGhosted()
      ->AddComponent("cell", AmanziMesh::CELL, 1);
  S->RequireFieldEvaluator("water_content");

  // -- Water retention evaluators
  // -- saturation
  Teuchos::ParameterList wrm_plist = plist_->sublist("water retention evaluator");
  Teuchos::RCP<FlowRelations::WRMEvaluator> wrm =
      Teuchos::rcp(new FlowRelations::WRMEvaluator(wrm_plist));
  S->SetFieldEvaluator("saturation_liquid", wrm);
  S->SetFieldEvaluator("saturation_gas", wrm);

  // -- rel perm
  std::vector<AmanziMesh::Entity_kind> locations2(2);
  std::vector<std::string> names2(2);
  std::vector<int> num_dofs2(2,1);
  locations2[0] = AmanziMesh::CELL;
  locations2[1] = AmanziMesh::BOUNDARY_FACE;
  names2[0] = "cell";
  names2[1] = "boundary_face";

  S->RequireField("relative_permeability")->SetMesh(mesh_)->SetGhosted()
      ->AddComponents(names2, locations2, num_dofs2);
  wrm_plist.set<double>("permeability rescaling", perm_scale_);
  Teuchos::RCP<FlowRelations::RelPermEvaluator> rel_perm_evaluator =
      Teuchos::rcp(new FlowRelations::RelPermEvaluator(wrm_plist, wrm->get_WRMs()));
  S->SetFieldEvaluator("relative_permeability", rel_perm_evaluator);
  wrms_ = wrm->get_WRMs();

  // -- Liquid density and viscosity for the transmissivity.
  S->RequireField("molar_density_liquid")->SetMesh(mesh_)->SetGhosted()
      ->AddComponent("cell", AmanziMesh::CELL, 1);
  S->RequireFieldEvaluator("molar_density_liquid");

  // -- liquid mass density for the gravity fluxes
  S->RequireField("mass_density_liquid")->SetMesh(mesh_)->SetGhosted()
      ->AddComponent("cell", AmanziMesh::CELL, 1);
  S->RequireFieldEvaluator("mass_density_liquid"); // simply picks up the molar density one.


}
    

// -------------------------------------------------------------
// Initialize PK
// -------------------------------------------------------------
void Richards::initialize(const Teuchos::Ptr<State>& S) {

  // Initialize BDF stuff and physical domain stuff.
  PKPhysicalBDFBase::initialize(S);


  // debugggin cruft
#if DEBUG_RES_FLAG
  for (unsigned int i=1; i!=23; ++i) {
    std::stringstream namestream;
    namestream << "flow_residual_" << i;
    S->GetFieldData(namestream.str(),name_)->PutScalar(0.);
    S->GetField(namestream.str(),name_)->set_initialized();

    std::stringstream solnstream;
    solnstream << "flow_solution_" << i;
    S->GetFieldData(solnstream.str(),name_)->PutScalar(0.);
    S->GetField(solnstream.str(),name_)->set_initialized();
  }
#endif

  // check whether this is a dynamic mesh problem
  if (S->HasField("vertex coordinate")) dynamic_mesh_ = true;

  // Set extra fields as initialized -- these don't currently have evaluators,
  // and will be initialized in the call to commit_state()
  S->GetFieldData("numerical_rel_perm",name_)->PutScalar(1.0);
  S->GetField("numerical_rel_perm",name_)->set_initialized();
  S->GetFieldData("dnumerical_rel_perm_dpressure",name_)->PutScalar(1.0);
  S->GetField("dnumerical_rel_perm_dpressure",name_)->set_initialized();

  if (vapor_diffusion_){
    S->GetFieldData("vapor_diffusion_pressure",name_)->PutScalar(1.0);
    S->GetField("vapor_diffusion_pressure",name_)->set_initialized();
    S->GetFieldData("vapor_diffusion_temperature",name_)->PutScalar(1.0);
    S->GetField("vapor_diffusion_temperature",name_)->set_initialized();
  }


  S->GetFieldData("darcy_flux", name_)->PutScalar(0.0);
  S->GetField("darcy_flux", name_)->set_initialized();
  S->GetFieldData("darcy_flux_direction", name_)->PutScalar(0.0);
  S->GetField("darcy_flux_direction", name_)->set_initialized();
  S->GetFieldData("darcy_velocity", name_)->PutScalar(0.0);
  S->GetField("darcy_velocity", name_)->set_initialized();

  // absolute perm
  SetAbsolutePermeabilityTensor_(S);

  // operators
  Teuchos::RCP<const Epetra_Vector> gvec = S->GetConstantVectorData("gravity");
  AmanziGeometry::Point g(3);
  g[0] = (*gvec)[0]; g[1] = (*gvec)[1]; g[2] = (*gvec)[2];

  matrix_diff_->SetGravity(g);
  matrix_diff_->SetBCs(bc_, bc_);
  matrix_diff_->Setup(K_);

  preconditioner_diff_->SetGravity(g);
  preconditioner_diff_->SetBCs(bc_, bc_);
  preconditioner_diff_->Setup(K_);
  preconditioner_->SymbolicAssembleMatrix();

  face_matrix_diff_->SetGravity(g);
  face_matrix_diff_->SetBCs(bc_, bc_);
  face_matrix_diff_->Setup(K_);
  face_matrix_diff_->Setup(Teuchos::null, Teuchos::null);
  face_matrix_diff_->UpdateMatrices(Teuchos::null, Teuchos::null);

  // if (vapor_diffusion_){
  //   //vapor diffusion
  //   matrix_vapor_->CreateMFDmassMatrices(Teuchos::null);
  //   // residual vector for vapor diffusion
  //   res_vapor = Teuchos::rcp(new CompositeVector(*S->GetFieldData("pressure"))); 
  // }

};


// -----------------------------------------------------------------------------
// Update any secondary (dependent) variables given a solution.
//
//   After a timestep is evaluated (or at ICs), there is no way of knowing if
//   secondary variables have been updated to be consistent with the new
//   solution.
// -----------------------------------------------------------------------------
void Richards::commit_state(double dt, const Teuchos::RCP<State>& S) {
  Teuchos::OSTab tab = vo_->getOSTab();
  if (vo_->os_OK(Teuchos::VERB_EXTREME))
    *vo_->os() << "Commiting state." << std::endl;

  PKPhysicalBDFBase::commit_state(dt, S);
  
  niter_ = 0;

  bool update = UpdatePermeabilityData_(S.ptr());

  update |= S->GetFieldEvaluator(key_)->HasFieldChanged(S.ptr(), name_);
  update |= S->GetFieldEvaluator("mass_density_liquid")->HasFieldChanged(S.ptr(), name_);

  if (update_flux_ == UPDATE_FLUX_TIMESTEP ||
      (update_flux_ == UPDATE_FLUX_ITERATION && update)) {

    // update the stiffness matrix
    Teuchos::RCP<const CompositeVector> rel_perm =
      S->GetFieldData("numerical_rel_perm");
    Teuchos::RCP<const CompositeVector> rho = S->GetFieldData("mass_density_liquid");
    matrix_->Init();
    matrix_diff_->SetDensity(rho);
    matrix_diff_->Setup(rel_perm, Teuchos::null);
    matrix_diff_->UpdateMatrices(Teuchos::null, Teuchos::null);

    // derive fluxes
    Teuchos::RCP<const CompositeVector> pres = S->GetFieldData("pressure");
    Teuchos::RCP<CompositeVector> flux = S->GetFieldData("darcy_flux", name_);
    matrix_diff_->UpdateFlux(*pres, *flux);
  }

  // As a diagnostic, calculate the mass balance error
// #if DEBUG_FLAG
//   if (S_next_ != Teuchos::null) {
//     Teuchos::RCP<const CompositeVector> wc1 = S_next_->GetFieldData("water_content");
//     Teuchos::RCP<const CompositeVector> wc0 = S_->GetFieldData("water_content");
//     Teuchos::RCP<const CompositeVector> darcy_flux = S->GetFieldData("darcy_flux", name_);
//     CompositeVector error(*wc1);

//     for (unsigned int c=0; c!=error.size("cell"); ++c) {
//       error("cell",c) = (*wc1)("cell",c) - (*wc0)("cell",c);

//       AmanziMesh::Entity_ID_List faces;
//       std::vector<int> dirs;
//       mesh_->cell_get_faces_and_dirs(c, &faces, &dirs);
//       for (unsigned int n=0; n!=faces.size(); ++n) {
//         error("cell",c) += (*darcy_flux)("face",faces[n]) * dirs[n] * dt;
//       }
//     }

//     double einf(0.0);
//     error.NormInf(&einf);

//     // VerboseObject stuff.
//     Teuchos::OSTab tab = vo_->getOSTab();
//     *vo_->os() << "Final Mass Balance Error: " << einf << std::endl;
//   }
// #endif
};


// -----------------------------------------------------------------------------
// Update any diagnostic variables prior to vis (in this case velocity field).
// -----------------------------------------------------------------------------
void Richards::calculate_diagnostics(const Teuchos::RCP<State>& S) {
  Teuchos::OSTab tab = vo_->getOSTab();
  if (vo_->os_OK(Teuchos::VERB_EXTREME))
    *vo_->os() << "Calculating diagnostic variables." << std::endl;

  // update the cell velocities
  if (update_flux_ == UPDATE_FLUX_VIS) {
    Teuchos::RCP<const CompositeVector> rel_perm =
      S->GetFieldData("numerical_rel_perm");
    Teuchos::RCP<const CompositeVector> rho =
        S->GetFieldData("mass_density_liquid");
    // update the stiffness matrix
    matrix_diff_->SetDensity(rho);
    matrix_diff_->Setup(rel_perm, Teuchos::null);
    matrix_diff_->UpdateMatrices(Teuchos::null, Teuchos::null);

    // derive fluxes
    Teuchos::RCP<CompositeVector> flux = S->GetFieldData("darcy_flux", name_);
    Teuchos::RCP<const CompositeVector> pres = S->GetFieldData("pressure");
    matrix_diff_->UpdateFlux(*pres, *flux);
  }

  // if (update_flux_ != UPDATE_FLUX_NEVER) {
  //   Teuchos::RCP<CompositeVector> darcy_velocity = S->GetFieldData("darcy_velocity", name_);
  //   Teuchos::RCP<const CompositeVector> flux = S->GetFieldData("darcy_flux");
  //   matrix_->DeriveCellVelocity(*flux, darcy_velocity.ptr());

  //   S->GetFieldEvaluator("molar_density_liquid")->HasFieldChanged(S.ptr(), name_);
  //   const Epetra_MultiVector& nliq_c = *S->GetFieldData("molar_density_liquid")
  //       ->ViewComponent("cell",false);

  //   Epetra_MultiVector& vel_c = *darcy_velocity->ViewComponent("cell",false);
  //   unsigned int ncells = vel_c.MyLength();
  //   for (unsigned int c=0; c!=ncells; ++c) {
  //     for (int n=0; n!=vel_c.NumVectors(); ++n) {
  //       vel_c[n][c] /= nliq_c[0][c];
  //     }
  //   }
  // }
};


// -----------------------------------------------------------------------------
// Use the physical rel perm (on cells) to update a work vector for rel perm.
//
//   This deals with upwinding, etc.
// -----------------------------------------------------------------------------
bool Richards::UpdatePermeabilityData_(const Teuchos::Ptr<State>& S) {
  Teuchos::OSTab tab = vo_->getOSTab();
  if (vo_->os_OK(Teuchos::VERB_EXTREME))
    *vo_->os() << "  Updating permeability?";

  Teuchos::RCP<CompositeVector> uw_rel_perm = S->GetFieldData("numerical_rel_perm", name_);
  Teuchos::RCP<const CompositeVector> rel_perm = S->GetFieldData("relative_permeability");
  bool update_perm = S->GetFieldEvaluator("relative_permeability")
      ->HasFieldChanged(S, name_);

  // requirements due to the upwinding method
  if (Krel_method_ == Operators::UPWIND_METHOD_TOTAL_FLUX) {
    bool update_dir = S->GetFieldEvaluator("mass_density_liquid")
        ->HasFieldChanged(S, name_);
    update_dir |= S->GetFieldEvaluator(key_)->HasFieldChanged(S, name_);

    if (update_dir) {
      // update the direction of the flux -- note this is NOT the flux
      Teuchos::RCP<const CompositeVector> rho = S->GetFieldData("mass_density_liquid");
      face_matrix_diff_->SetDensity(rho);

      Teuchos::RCP<CompositeVector> flux_dir =
          S->GetFieldData("darcy_flux_direction", name_);
      Teuchos::RCP<const CompositeVector> pres = S->GetFieldData(key_);
      face_matrix_diff_->UpdateFlux(*pres, *flux_dir);
    }

    update_perm |= update_dir;
  }

  if (update_perm) {
    // Move rel perm on boundary_faces into uw_rel_perm on faces
    const Epetra_Import& vandelay = mesh_->exterior_face_importer();
    const Epetra_MultiVector& rel_perm_bf =
        *rel_perm->ViewComponent("boundary_face",false);
    {
      Epetra_MultiVector& uw_rel_perm_f = *uw_rel_perm->ViewComponent("face",false);
      uw_rel_perm_f.Export(rel_perm_bf, vandelay, Insert);
    }

    // Upwind, only overwriting boundary faces if the wind says to do so.
    upwinding_->Update(S);

    if (clobber_surf_kr_) {
      Epetra_MultiVector& uw_rel_perm_f = *uw_rel_perm->ViewComponent("face",false);
      uw_rel_perm_f.Export(rel_perm_bf, vandelay, Insert);
    }
  }

  // debugging
  if (vo_->os_OK(Teuchos::VERB_EXTREME)) {
    *vo_->os() << " " << update_perm << std::endl;
  }
  return update_perm;
};


bool Richards::UpdatePermeabilityDerivativeData_(const Teuchos::Ptr<State>& S) {
  Teuchos::OSTab tab = vo_->getOSTab();
  if (vo_->os_OK(Teuchos::VERB_EXTREME))
    *vo_->os() << "  Updating permeability derivatives?";

  bool update_perm = S->GetFieldEvaluator("relative_permeability")->HasFieldDerivativeChanged(S, name_, key_);
  Teuchos::RCP<CompositeVector> duw_rel_perm = S->GetFieldData("dnumerical_rel_perm_dpressure", name_);
  Teuchos::RCP<const CompositeVector> drel_perm = S->GetFieldData("drelative_permeability_dpressure");

  if (update_perm) {
    // Move rel perm on boundary_faces into uw_rel_perm on faces
    // const Epetra_Import& vandelay = mesh_->exterior_face_importer();
    // const Epetra_MultiVector& drel_perm_bf =
    //     *drel_perm->ViewComponent("boundary_face",false);
    // {
    //   Epetra_MultiVector& duw_rel_perm_f = *duw_rel_perm->ViewComponent("face",false);
    //   duw_rel_perm_f.Export(drel_perm_bf, vandelay, Insert);
    // }
    duw_rel_perm->PutScalar(0.);

    // Upwind, only overwriting boundary faces if the wind says to do so.
    upwinding_deriv_->Update(S);

    // if (clobber_surf_kr_) {
    //   Epetra_MultiVector& duw_rel_perm_f = *duw_rel_perm->ViewComponent("face",false);
    //   duw_rel_perm_f.PutScalar(0.);

    //   //duw_rel_perm_f.Export(drel_perm_bf, vandelay, Insert);
    // }
  }

  // debugging
  if (vo_->os_OK(Teuchos::VERB_EXTREME)) {
    *vo_->os() << " " << update_perm << std::endl;
  }
  return update_perm;
};



// -----------------------------------------------------------------------------
// Evaluate boundary conditions at the current time.
// -----------------------------------------------------------------------------
void Richards::UpdateBoundaryConditions_(bool kr) {
  Teuchos::OSTab tab = vo_->getOSTab();
  if (vo_->os_OK(Teuchos::VERB_EXTREME))
    *vo_->os() << "  Updating BCs." << std::endl;

  // initialize all to 0
  for (unsigned int n=0; n!=bc_markers_.size(); ++n) {
    bc_markers_[n] = Operators::OPERATOR_BC_NONE;
    bc_values_[n] = 0.0;
  }

  // Dirichlet boundary conditions
  Functions::BoundaryFunction::Iterator bc;
  for (bc=bc_pressure_->begin(); bc!=bc_pressure_->end(); ++bc) {
    int f = bc->first;
    bc_markers_[f] = Operators::OPERATOR_BC_DIRICHLET;
    bc_values_[f] = bc->second;
  }

  const Epetra_MultiVector& rel_perm = 
    *S_next_->GetFieldData("numerical_rel_perm")->ViewComponent("face",false);

  if (!infiltrate_only_if_unfrozen_) {
    // Standard Neuman boundary conditions
    for (bc=bc_flux_->begin(); bc!=bc_flux_->end(); ++bc) {
      int f = bc->first;
      bc_markers_[f] = Operators::OPERATOR_BC_NEUMANN;
      bc_values_[f] = bc->second;
      if (!kr && rel_perm[0][f] > 0.) bc_values_[f] /= rel_perm[0][f];
    }
  } else {
    // Neumann boundary conditions that turn off if temp < freezing
    const Epetra_MultiVector& temp = *S_next_->GetFieldData("temperature")->ViewComponent("face");
    for (bc=bc_flux_->begin(); bc!=bc_flux_->end(); ++bc) {
      int f = bc->first;
      bc_markers_[f] = Operators::OPERATOR_BC_NEUMANN;
      if (temp[0][f] > 273.15) {
        bc_values_[f] = bc->second;
        if (!kr && rel_perm[0][f] > 0.) {
          bc_values_[f] /= rel_perm[0][f];
        }
      } else {
        bc_values_[f] = 0.;
      }
    }
  }

  // seepage face -- pressure <= p_atm, outward mass flux >= 0
  // const Epetra_MultiVector pressure = *S_next_->GetFieldData(key_)->ViewComponent("face");
  const double& p_atm = *S_next_->GetScalarData("atmospheric_pressure");
  for (bc=bc_seepage_->begin(); bc!=bc_seepage_->end(); ++bc) {
    int f = bc->first;
    //    std::cout << "Found seepage face: " << f << " at: " << mesh_->face_centroid(f) << " with normal: " << mesh_->face_normal(f) << std::endl;
    double bc_pressure = BoundaryValue(S_next_->GetFieldData(key_), f);
    if (bc_pressure < bc->second) {
      bc_markers_[f] = Operators::OPERATOR_BC_NEUMANN;
      bc_values_[f] = 0.;
      
    } else {
      bc_markers_[f] = Operators::OPERATOR_BC_DIRICHLET;
      bc_values_[f] = bc->second;
    }
  }
  if (bc_seepage_->size() > 0)
    std::cout << "Seepage with " << bc_seepage_->size() << " faces" << std::endl;

  // surface coupling
  if (coupled_to_surface_via_head_) {
    // Face is Dirichlet with value of surface head
    Teuchos::RCP<const AmanziMesh::Mesh> surface = S_next_->GetMesh("surface");
    const Epetra_MultiVector& head = *S_next_->GetFieldData("surface_pressure")
        ->ViewComponent("cell",false);

    unsigned int ncells_surface = head.MyLength();
    for (unsigned int c=0; c!=ncells_surface; ++c) {
      // -- get the surface cell's equivalent subsurface face
      AmanziMesh::Entity_ID f =
        surface->entity_get_parent(AmanziMesh::CELL, c);

      // -- set that value to dirichlet
      bc_markers_[f] = Operators::OPERATOR_BC_DIRICHLET;
      bc_values_[f] = head[0][c];
    }
  }

  // surface coupling
  if (coupled_to_surface_via_flux_) {
    // Face is Neumann with value of surface residual
    Teuchos::RCP<const AmanziMesh::Mesh> surface = S_next_->GetMesh("surface");
    const Epetra_MultiVector& flux = *S_next_->GetFieldData("surface_subsurface_flux")
        ->ViewComponent("cell",false);

    unsigned int ncells_surface = flux.MyLength();
    for (unsigned int c=0; c!=ncells_surface; ++c) {
      // -- get the surface cell's equivalent subsurface face
      AmanziMesh::Entity_ID f =
        surface->entity_get_parent(AmanziMesh::CELL, c);

      // -- set that value to Neumann
      bc_markers_[f] = Operators::OPERATOR_BC_NEUMANN;
      bc_values_[f] = flux[0][c] / mesh_->face_area(f);
      if (!kr && rel_perm[0][f] > 0.) bc_values_[f] /= rel_perm[0][f];

      if ((surface->cell_map(false).GID(c) == 0) && vo_->os_OK(Teuchos::VERB_HIGH)) {
        *vo_->os() << "  bc for coupled surface: val=" << bc_values_[f] << std::endl;
      }
      
      // NOTE: flux[0][c] is in units of mols / s, where as Neumann BCs are in
      //       units of mols / s / A.  The right A must be chosen, as it is
      //       the subsurface mesh's face area, not the surface mesh's cell
      //       area.
    }
  }

  // mark all remaining boundary conditions as zero flux conditions
  AmanziMesh::Entity_ID_List cells;
  int nfaces_owned = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::OWNED);
  for (int f = 0; f < nfaces_owned; f++) {
    if (bc_markers_[f] == Operators::OPERATOR_BC_NONE) {
      mesh_->face_get_cells(f, AmanziMesh::USED, &cells);
      int ncells = cells.size();

      if (ncells == 1) {
        bc_markers_[f] = Operators::OPERATOR_BC_NEUMANN;
        bc_values_[f] = 0.0;
      }
    }
  }

};


// -----------------------------------------------------------------------------
// Add a boundary marker to owned faces.
// -----------------------------------------------------------------------------
void
Richards::ApplyBoundaryConditions_(const Teuchos::Ptr<CompositeVector>& pres) {
  Epetra_MultiVector& pres_f = *pres->ViewComponent("face",false);
  unsigned int nfaces = pres_f.MyLength();
  for (unsigned int f=0; f!=nfaces; ++f) {
    if (bc_markers_[f] == Operators::OPERATOR_BC_DIRICHLET) {
      pres_f[0][f] = bc_values_[f];
    }
  }
};


bool Richards::ModifyPredictor(double h, Teuchos::RCP<const TreeVector> u0,
        Teuchos::RCP<TreeVector> u) {
  
  Teuchos::OSTab tab = vo_->getOSTab();
  if (vo_->os_OK(Teuchos::VERB_EXTREME))
    *vo_->os() << "Modifying predictor:" << std::endl;

  bool changed(false);
  if (modify_predictor_bc_flux_ ||
      (modify_predictor_first_bc_flux_ && S_next_->cycle() == 0)) {
    changed |= ModifyPredictorFluxBCs_(h,u);
  }

  if (modify_predictor_wc_) {
    changed |= ModifyPredictorWC_(h,u);
  }

  if (modify_predictor_with_consistent_faces_) {
    changed |= ModifyPredictorConsistentFaces_(h,u);
  }
  return changed;
}

bool Richards::ModifyPredictorFluxBCs_(double h, Teuchos::RCP<TreeVector> u) {
  Teuchos::OSTab tab = vo_->getOSTab();
  if (vo_->os_OK(Teuchos::VERB_EXTREME))
    *vo_->os() << "  modifications to deal with nonlinearity at flux BCs" << std::endl;

  if (flux_predictor_ == Teuchos::null) {
    flux_predictor_ = Teuchos::rcp(new PredictorDelegateBCFlux(S_next_, mesh_, matrix_diff_,
            wrms_, &bc_markers_, &bc_values_));
  }

  // update boundary conditions
  bc_pressure_->Compute(S_next_->time());
  bc_flux_->Compute(S_next_->time());
  UpdateBoundaryConditions_();

  UpdatePermeabilityData_(S_next_.ptr());
  Teuchos::RCP<const CompositeVector> rel_perm =
    S_next_->GetFieldData("numerical_rel_perm");

  matrix_->Init();
  matrix_diff_->Setup(rel_perm, Teuchos::null);
  Teuchos::RCP<const CompositeVector> rho = S_next_->GetFieldData("mass_density_liquid");
  matrix_diff_->SetDensity(rho);
  matrix_diff_->UpdateMatrices(Teuchos::null, Teuchos::null);
  matrix_diff_->ApplyBCs(true, true);

  flux_predictor_->ModifyPredictor(h, u);
  ChangedSolution(); // mark the solution as changed, as modifying with
                      // consistent faces will then get the updated boundary
                      // conditions
  return true;
}

bool Richards::ModifyPredictorConsistentFaces_(double h, Teuchos::RCP<TreeVector> u) {
  Teuchos::OSTab tab = vo_->getOSTab();
  if (vo_->os_OK(Teuchos::VERB_EXTREME))
    *vo_->os() << "  modifications for consistent face pressures." << std::endl;
  
  CalculateConsistentFaces(u->Data().ptr());

  return true;
}

bool Richards::ModifyPredictorWC_(double h, Teuchos::RCP<TreeVector> u) {
  ASSERT(0);
  return false;
}


// void Richards::CalculateConsistentFacesForInfiltration_(
//     const Teuchos::Ptr<CompositeVector>& u) {
//   if (vo_->os_OK(Teuchos::VERB_EXTREME))
//     *vo_->os() << "  modifications to deal with nonlinearity at flux BCs" << std::endl;

//   if (flux_predictor_ == Teuchos::null) {
//     flux_predictor_ = Teuchos::rcp(new PredictorDelegateBCFlux(S_next_, mesh_, matrix_,
//             wrms_, &bc_markers_, &bc_values_));
//   }

//   // update boundary conditions
//   bc_pressure_->Compute(S_next_->time());
//   bc_flux_->Compute(S_next_->time());
//   UpdateBoundaryConditions_();

//   bool update = UpdatePermeabilityData_(S_next_.ptr());
//   Teuchos::RCP<const CompositeVector> rel_perm =
//       S_next_->GetFieldData("numerical_rel_perm");
//   matrix_->CreateMFDstiffnessMatrices(rel_perm.ptr());
//   matrix_->CreateMFDrhsVectors();
//   Teuchos::RCP<const CompositeVector> rho = S_next_->GetFieldData("mass_density_liquid");
//   Teuchos::RCP<const Epetra_Vector> gvec = S_next_->GetConstantVectorData("gravity");
//   AddGravityFluxes_(gvec.ptr(), rel_perm.ptr(), rho.ptr(), matrix_.ptr());
//   matrix_->ApplyBoundaryConditions(bc_markers_, bc_values_);

//   flux_predictor_->ModifyPredictor(u);
// }

void Richards::CalculateConsistentFaces(const Teuchos::Ptr<CompositeVector>& u) {
  // VerboseObject stuff.
  Teuchos::OSTab tab = vo_->getOSTab();
  if (vo_->os_OK(Teuchos::VERB_EXTREME))
    *vo_->os() << "  Modifying predictor for consistent faces" << std::endl;

  // average cells to faces to give a reasonable place to start
  u->ScatterMasterToGhosted("cell");
  const Epetra_MultiVector& u_c = *u->ViewComponent("cell",true);
  Epetra_MultiVector& u_f = *u->ViewComponent("face",false);

  int f_owned = u_f.MyLength();
  for (int f=0; f!=f_owned; ++f) {
    AmanziMesh::Entity_ID_List cells;
    mesh_->face_get_cells(f, AmanziMesh::USED, &cells);
    int ncells = cells.size();

    double face_value = 0.0;
    for (int n=0; n!=ncells; ++n) {
      face_value += u_c[0][cells[n]];
    }
    u_f[0][f] = face_value / ncells;
  }
  ChangedSolution();
  
  // Using the old BCs, so should use the old rel perm?
  // update the rel perm according to the scheme of choice
  //  UpdatePermeabilityData_(S_next_.ptr());

  // update boundary conditions
  bc_pressure_->Compute(S_next_->time());
  bc_flux_->Compute(S_next_->time());
  UpdateBoundaryConditions_(false); // without rel perm

  Teuchos::RCP<const CompositeVector> rel_perm = 
    S_next_->GetFieldData("numerical_rel_perm");
  if (vo_->os_OK(Teuchos::VERB_HIGH)) {
    *vo_->os() << "  consistent face rel perm = " << (*rel_perm->ViewComponent("face",false))[0][7] << std::endl;
  }

  S_next_->GetFieldEvaluator("mass_density_liquid")
      ->HasFieldChanged(S_next_.ptr(), name_);
  Teuchos::RCP<const CompositeVector> rho =
      S_next_->GetFieldData("mass_density_liquid");
  Teuchos::RCP<const Epetra_Vector> gvec =
      S_next_->GetConstantVectorData("gravity");

  // Update the preconditioner with darcy and gravity fluxes
  Teuchos::RCP<CompositeVector> rel_perm_one = Teuchos::rcp(new CompositeVector(*rel_perm, INIT_MODE_NONE));
  rel_perm_one->PutScalar(1.);
  matrix_->Init();
  matrix_diff_->SetDensity(rho);
  matrix_diff_->Setup(rel_perm_one, Teuchos::null);
  matrix_diff_->UpdateMatrices(Teuchos::null, Teuchos::null);
  matrix_diff_->ApplyBCs(true, true);

  // derive the consistent faces, involves a solve
  matrix_diff_->UpdateConsistentFaces(*u);

  db_->WriteVector(" p_consistent face Richards:", u.ptr(), true);
}

// -----------------------------------------------------------------------------
// Check admissibility of the solution guess.
// -----------------------------------------------------------------------------
bool Richards::IsAdmissible(Teuchos::RCP<const TreeVector> up) {
  Teuchos::OSTab tab = vo_->getOSTab();
  if (vo_->os_OK(Teuchos::VERB_EXTREME))
    *vo_->os() << "  Checking admissibility..." << std::endl;

  // For some reason, wandering PKs break most frequently with an unreasonable
  // pressure.  This simply tries to catch that before it happens.
  Teuchos::RCP<const CompositeVector> pres = up->Data();
  double minT, maxT;

  const Epetra_MultiVector& pres_c = *pres->ViewComponent("cell",false);
  double minT_c(1.e15), maxT_c(-1.e15);
  int min_c(-1), max_c(-1);
  for (int c=0; c!=pres_c.MyLength(); ++c) {
    if (pres_c[0][c] < minT_c) {
      minT_c = pres_c[0][c];
      min_c = c;
    }
    if (pres_c[0][c] > maxT_c) {
      maxT_c = pres_c[0][c];
      max_c = c;
    }
  }

  double minT_f(1.e15), maxT_f(-1.e15);
  int min_f(-1), max_f(-1);
  if (pres->HasComponent("face")) {
    const Epetra_MultiVector& pres_f = *pres->ViewComponent("face",false);
    for (int f=0; f!=pres_f.MyLength(); ++f) {
      if (pres_f[0][f] < minT_f) {
        minT_f = pres_f[0][f];
        min_f = f;
      } 
      if (pres_f[0][f] > maxT_f) {
        maxT_f = pres_f[0][f];
        max_f = f;
      }
    }
    minT = std::min(minT_c, minT_f);
    maxT = std::max(maxT_c, maxT_f);

  } else {
    minT = minT_c;
    maxT = maxT_c;
  }

  double minT_l = minT;
  double maxT_l = maxT;
  mesh_->get_comm()->MaxAll(&maxT_l, &maxT, 1);
  mesh_->get_comm()->MinAll(&minT_l, &minT, 1);
  
  if (vo_->os_OK(Teuchos::VERB_HIGH)) {
    *vo_->os() << "    Admissible p? (min/max): " << minT << ",  " << maxT << std::endl;
  }
  
  if (minT < -1.e9 || maxT > 1.e8) {
    if (vo_->os_OK(Teuchos::VERB_MEDIUM)) {
      *vo_->os() << " is not admissible, as it is not within bounds of constitutive models:" << std::endl;
      ENorm_t global_minT_c, local_minT_c;
      ENorm_t global_maxT_c, local_maxT_c;

      local_minT_c.value = minT_c;
      local_minT_c.gid = pres_c.Map().GID(min_c);
      local_maxT_c.value = maxT_c;
      local_maxT_c.gid = pres_c.Map().GID(max_c);

      MPI_Allreduce(&local_minT_c, &global_minT_c, 1, MPI_DOUBLE_INT, MPI_MINLOC, MPI_COMM_WORLD);
      MPI_Allreduce(&local_maxT_c, &global_maxT_c, 1, MPI_DOUBLE_INT, MPI_MAXLOC, MPI_COMM_WORLD);
      *vo_->os() << "   cells (min/max): [" << global_minT_c.gid << "] " << global_minT_c.value
                 << ", [" << global_maxT_c.gid << "] " << global_maxT_c.value << std::endl;

      if (pres->HasComponent("face")) {
        const Epetra_MultiVector& pres_f = *pres->ViewComponent("face",false);
        ENorm_t global_minT_f, local_minT_f;
        ENorm_t global_maxT_f, local_maxT_f;

        local_minT_f.value = minT_f;
        local_minT_f.gid = pres_f.Map().GID(min_f);
        local_maxT_f.value = maxT_f;
        local_maxT_f.gid = pres_f.Map().GID(max_f);
        
        MPI_Allreduce(&local_minT_f, &global_minT_f, 1, MPI_DOUBLE_INT, MPI_MINLOC, MPI_COMM_WORLD);
        MPI_Allreduce(&local_maxT_f, &global_maxT_f, 1, MPI_DOUBLE_INT, MPI_MAXLOC, MPI_COMM_WORLD);
        *vo_->os() << "   cells (min/max): [" << global_minT_f.gid << "] " << global_minT_f.value
                   << ", [" << global_maxT_f.gid << "] " << global_maxT_f.value << std::endl;
      }
    }
    return false;
  }
  return true;
}


double Richards::BoundaryValue(Teuchos::RCP<const Amanzi::CompositeVector> solution, int face_id){

  double value=0.;

  if (solution->HasComponent("face")){
    const Epetra_MultiVector& pres = *solution -> ViewComponent("face",false);
    value = pres[0][face_id];
  }
  else if  (solution->HasComponent("boundary_face")){
    const Epetra_MultiVector& pres = *solution -> ViewComponent("boundary_face",false);
    const Epetra_Map& fb_map = mesh_->exterior_face_map();
    const Epetra_Map& f_map = mesh_->face_map(false);

    int face_gid = f_map.GID(face_id);
    int face_lbid = fb_map.LID(face_gid);

    value =  pres[0][face_lbid];
  }
  else{
    Errors::Message msg("No component is defined for boundary faces\n");
    Exceptions::amanzi_throw(msg);
  }

  return value;

}

} // namespace
} // namespace
