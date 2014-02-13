/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */

/*
  Evaluates the Kr associated with the unfrozen fraction of water.

  Authors: Ethan Coon (ecoon@lanl.gov)
*/

#include "boost/math/constants/constants.hpp"
#include <cmath>

#include "dbc.hh"
#include "errors.hh"
#include "zero_uf_relperm_model.hh"

namespace Amanzi {
namespace Flow {
namespace FlowRelations {

ZeroUFRelPermModel::ZeroUFRelPermModel(Teuchos::ParameterList& plist) :
    plist_(plist),
    pi_(boost::math::constants::pi<double>()) {

  alpha_ = plist_.get<int>("unfrozen rel perm alpha", 4);
  if (alpha_ % 2 != 0) {
    Errors::Message message("Unfrozen Fraction Rel Perm: alpha must be an even integer");
    Exceptions::amanzi_throw(message);
  }

  h_cutoff_ = plist_.get<double>("unfrozen rel perm cutoff height", 0.01);
}

double
ZeroUFRelPermModel::SurfaceRelPerm(double uf, double h) {
  double kr = std::pow(std::sin(pi_ * uf / 2.), alpha_);

  if (h <= 0.) {
    kr = 0.;
  } else if (h < h_cutoff_) {
    double fac = std::pow(std::sin(pi_ * (h/h_cutoff_) / 2.), 2);
    kr *= fac; // kr --> 0 as h --> 0
  }
  return kr;
}



} // namespace
} // namespace
} // namespace