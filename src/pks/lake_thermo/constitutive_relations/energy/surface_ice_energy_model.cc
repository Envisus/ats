/*
  The surface ice energy model is an algebraic model with dependencies.

  Generated via evaluator_generator with:
Energy evaulator for ice+liquid surface water.
    
  Authors: Ethan Coon (ecoon@lanl.gov)
*/

#include "Teuchos_ParameterList.hpp"
#include "dbc.hh"
#include "surface_ice_energy_model.hh"

namespace Amanzi {
namespace Energy {
namespace Relations {

// Constructor from ParameterList
SurfaceIceEnergyModel::SurfaceIceEnergyModel(Teuchos::ParameterList& plist)
{
  InitializeFromPlist_(plist);
}


// Initialize parameters
void
SurfaceIceEnergyModel::InitializeFromPlist_(Teuchos::ParameterList& plist)
{

}


// main method
double
SurfaceIceEnergyModel::Energy(double h, double eta, double nl, double ul, double ni, double ui, double cv) const
{
  return cv*h*(eta*nl*ul + ni*ui*(-eta + 1));
}

double
SurfaceIceEnergyModel::DEnergyDPondedDepth(double h, double eta, double nl, double ul, double ni, double ui, double cv) const
{
  return cv*(eta*nl*ul + ni*ui*(-eta + 1));
}

double
SurfaceIceEnergyModel::DEnergyDUnfrozenFraction(double h, double eta, double nl, double ul, double ni, double ui, double cv) const
{
  return cv*h*(-ni*ui + nl*ul);
}

double
SurfaceIceEnergyModel::DEnergyDMolarDensityLiquid(double h, double eta, double nl, double ul, double ni, double ui, double cv) const
{
  return cv*eta*h*ul;
}

double
SurfaceIceEnergyModel::DEnergyDInternalEnergyLiquid(double h, double eta, double nl, double ul, double ni, double ui, double cv) const
{
  return cv*eta*h*nl;
}

double
SurfaceIceEnergyModel::DEnergyDMolarDensityIce(double h, double eta, double nl, double ul, double ni, double ui, double cv) const
{
  return cv*h*ui*(-eta + 1);
}

double
SurfaceIceEnergyModel::DEnergyDInternalEnergyIce(double h, double eta, double nl, double ul, double ni, double ui, double cv) const
{
  return cv*h*ni*(-eta + 1);
}

double
SurfaceIceEnergyModel::DEnergyDCellVolume(double h, double eta, double nl, double ul, double ni, double ui, double cv) const
{
  return h*(eta*nl*ul + ni*ui*(-eta + 1));
}

} //namespace
} //namespace
} //namespace
  