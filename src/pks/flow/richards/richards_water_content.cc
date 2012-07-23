/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */

/* -----------------------------------------------------------------------------
ATS

Authors: Ethan Coon (ecoon@lanl.gov)

FieldModel for water content.

Wrapping this conserved quantity as a field model makes it easier to take
derivatives, keep updated, and the like.  The equation for this is simply:

WC = phi * (s_liquid * n_liquid + omega_gas * s_gas * n_gas)

This is simply the conserved quantity in Richards equation.
----------------------------------------------------------------------------- */


#include "richards_water_content.hh"

namespace Amanzi {
namespace Flow {

RichardsWaterContent::RichardsWaterContent(Teuchos::ParameterList& wc_plist,
        const Teuchos::Ptr<State>& S) {
  my_key_ = std::string("water_content");
  dependencies_.insert(std::string("porosity"));

  dependencies_.insert(std::string("saturation_liquid"));
  dependencies_.insert(std::string("molar_density_liquid"));

  dependencies_.insert(std::string("saturation_gas"));
  dependencies_.insert(std::string("molar_density_gas"));
  dependencies_.insert(std::string("mol_frac_gas"));

  CheckCompatibility_or_die_(S);
};

RichardsWaterContent::RichardsWaterContent(const RichardsWaterContent& other) :
    SecondaryVariableFieldModel(other) {};

Teuchos::RCP<FieldModel>
RichardsWaterContent::Clone() const {
  return Teuchos::rcp(new RichardsWaterContent(*this));
};


void RichardsWaterContent::EvaluateField_(const Teuchos::Ptr<State>& S,
        const Teuchos::Ptr<CompositeVector>& result) {
  Teuchos::RCP<const CompositeVector> phi = S->GetFieldData("porosity");
  Teuchos::RCP<const CompositeVector> s_l = S->GetFieldData("saturation_liquid");
  Teuchos::RCP<const CompositeVector> n_l = S->GetFieldData("molar_density_liquid");
  Teuchos::RCP<const CompositeVector> s_g = S->GetFieldData("saturation_gas");
  Teuchos::RCP<const CompositeVector> n_g = S->GetFieldData("molar_density_gas");
  Teuchos::RCP<const CompositeVector> omega_g = S->GetFieldData("mol_frac_gas");

  for (int c=0; c!=result->size("cell"); ++c) {
    (*result)("cell",c) = (*phi)("cell",c) * ( (*s_l)("cell",c)*(*n_l)("cell",c)
            + (*s_g)("cell",c)*(*n_g)("cell",c)*(*omega_g)("cell",c) );
  }

};


void RichardsWaterContent::EvaluateFieldPartialDerivative_(const Teuchos::Ptr<State>& S,
        Key wrt_key, const Teuchos::Ptr<CompositeVector>& result) {
  Teuchos::RCP<const CompositeVector> phi = S->GetFieldData("porosity");
  Teuchos::RCP<const CompositeVector> s_l = S->GetFieldData("saturation_liquid");
  Teuchos::RCP<const CompositeVector> n_l = S->GetFieldData("molar_density_liquid");
  Teuchos::RCP<const CompositeVector> s_g = S->GetFieldData("saturation_gas");
  Teuchos::RCP<const CompositeVector> n_g = S->GetFieldData("molar_density_gas");
  Teuchos::RCP<const CompositeVector> omega_g = S->GetFieldData("mol_frac_gas");

  if (wrt_key == "porosity") {
    for (int c=0; c!=result->size("cell"); ++c) {
j      (*result)("cell",c) = (*s_l)("cell",c)*(*n_l)("cell",c)
          + (*s_g)("cell",c)*(*n_g)("cell",c)*(*omega_g)("cell",c);
    }
  } else if (wrt_key == "saturation_liquid") {
    for (int c=0; c!=result->size("cell"); ++c) {
      (*result)("cell",c) = (*phi)("cell",c) * (*n_l)("cell",c);
    }
  } else if (wrt_key == "molar_density_liquid") {
    for (int c=0; c!=result->size("cell"); ++c) {
      (*result)("cell",c) = (*phi)("cell",c) * (*s_l)("cell",c);
    }
  } else if (wrt_key == "saturation_gas") {
    for (int c=0; c!=result->size("cell"); ++c) {
      (*result)("cell",c) = (*phi)("cell",c) * (*n_g)("cell",c)*(*omega_g)("cell",c);
    }
  } else if (wrt_key == "molar_density_gas") {
    for (int c=0; c!=result->size("cell"); ++c) {
      (*result)("cell",c) = (*phi)("cell",c) * (*s_g)("cell",c)*(*omega_g)("cell",c);
    }
  } else if (wrt_key == "mol_frac_gas") {
    for (int c=0; c!=result->size("cell"); ++c) {
      (*result)("cell",c) = (*phi)("cell",c) * (*s_g)("cell",c)*(*n_g)("cell",c);
    }
  }
};


} //namespace
} //namespace
