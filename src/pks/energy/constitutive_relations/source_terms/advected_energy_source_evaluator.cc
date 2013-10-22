/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */

/*
  Source term evaluator for enthalpy of mass source.

  Authors: Ethan Coon (ecoon@lanl.gov)
*/

#include "advected_energy_source_evaluator.hh"

namespace Amanzi {
namespace Energy {
namespace EnergyRelations {

// constructor format for all derived classes
AdvectedEnergySourceEvaluator::AdvectedEnergySourceEvaluator(Teuchos::ParameterList& plist) :
    SecondaryVariableFieldEvaluator(plist)
{
  InitializeFromPlist_();
}

AdvectedEnergySourceEvaluator::AdvectedEnergySourceEvaluator(const AdvectedEnergySourceEvaluator& other) :
    SecondaryVariableFieldEvaluator(other),
    domain_(other.domain_),
    internal_enthalpy_key_(other.internal_enthalpy_key_),
    external_enthalpy_key_(other.external_enthalpy_key_),
    mass_source_key_(other.mass_source_key_),
    internal_density_key_(other.internal_density_key_),
    external_density_key_(other.external_density_key_),
    cell_vol_key_(other.cell_vol_key_),
    conducted_source_key_(other.conducted_source_key_),
    include_conduction_(other.include_conduction_)
{}

Teuchos::RCP<FieldEvaluator>
AdvectedEnergySourceEvaluator::Clone() const {
  return Teuchos::rcp(new AdvectedEnergySourceEvaluator(*this));
}

// Required methods from SecondaryVariableFieldEvaluator
void
AdvectedEnergySourceEvaluator::EvaluateField_(const Teuchos::Ptr<State>& S,
        const Teuchos::Ptr<CompositeVector>& result) {
  const Epetra_MultiVector& int_enth = *S->GetFieldData(internal_enthalpy_key_)
      ->ViewComponent("cell",false);
  const Epetra_MultiVector& ext_enth = *S->GetFieldData(external_enthalpy_key_)
      ->ViewComponent("cell",false);
  const Epetra_MultiVector& mass_source = *S->GetFieldData(mass_source_key_)
      ->ViewComponent("cell",false);
  const Epetra_MultiVector& int_dens = *S->GetFieldData(internal_density_key_)
      ->ViewComponent("cell",false);
  const Epetra_MultiVector& ext_dens = *S->GetFieldData(internal_density_key_)
      ->ViewComponent("cell",false);

  const Epetra_MultiVector& cv = *S->GetFieldData(cell_vol_key_)
      ->ViewComponent("cell",false);

  Epetra_MultiVector& res = *result->ViewComponent("cell",false);
  unsigned int ncells = res.MyLength();
  for (unsigned int c=0; c!=ncells; ++c) {
    if (mass_source[0][c] > 0.) { // positive indicates increase of water in surface
      // upwind, take external values
      res[0][c] = cv[0][c] * mass_source[0][c] * ext_dens[0][c] * ext_enth[0][c];
    } else {
      // upwind, take internal values
      res[0][c] = cv[0][c] * mass_source[0][c] * int_dens[0][c] * int_enth[0][c];
    }
  }

  if (include_conduction_) {
    const Epetra_MultiVector& cond = *S->GetFieldData(conducted_source_key_)
        ->ViewComponent("cell",false);
    for (unsigned int c=0; c!=ncells; ++c) {
      res[0][c] += cv[0][c] * cond[0][c];
    }
  }
}

void
AdvectedEnergySourceEvaluator::EvaluateFieldPartialDerivative_(const Teuchos::Ptr<State>& S,
        Key wrt_key, const Teuchos::Ptr<CompositeVector>& result) {
  result->PutScalar(0.);
}

void
AdvectedEnergySourceEvaluator::InitializeFromPlist_() {
  domain_ = plist_.get<std::string>("domain", "");
  std::string domain_prefix = domain_ == std::string("") ? domain_ : domain_+std::string("_");

  internal_enthalpy_key_ = plist_.get<std::string>("internal enthalpy key",
          domain_prefix+std::string("enthalpy"));
  external_enthalpy_key_ = plist_.get<std::string>("external enthalpy key",
          domain_prefix+std::string("mass_source_enthalpy"));
  mass_source_key_ = plist_.get<std::string>("mass source key",
          domain_prefix+std::string("mass_source"));
  internal_density_key_ = plist_.get<std::string>("internal density key",
          domain_prefix+std::string("molar_density_liquid"));
  external_density_key_ = plist_.get<std::string>("external density key",
          domain_prefix+std::string("source_molar_density"));

  dependencies_.insert(internal_enthalpy_key_);
  dependencies_.insert(external_enthalpy_key_);
  dependencies_.insert(mass_source_key_);
  dependencies_.insert(internal_density_key_);
  dependencies_.insert(external_density_key_);

  include_conduction_ = plist_.get<bool>("include conduction");
  if (include_conduction_) {
    conducted_source_key_ = plist_.get<std::string>("conducted energy source key",
            domain_prefix+std::string("conducted_energy_source"));
    dependencies_.insert(conducted_source_key_);
  }

  cell_vol_key_ = plist_.get<std::string>("cell volume key",
          domain_prefix+std::string("cell_volume"));


  if (my_key_ == std::string("")) {
    if (include_conduction_) {
      my_key_ = plist_.get<std::string>("energy source key",
              domain_prefix+std::string("total_energy_source"));
    } else {
      my_key_ = plist_.get<std::string>("energy source key",
              domain_prefix+std::string("advected_energy_source"));
    }
  }
}


} //namespace
} //namespace
} //namespace

