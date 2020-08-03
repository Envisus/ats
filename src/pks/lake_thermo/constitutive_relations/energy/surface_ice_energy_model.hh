/*
  The surface ice energy model is an algebraic model with dependencies.

  Generated via evaluator_generator with:
Energy evaulator for ice+liquid surface water.
    
  Authors: Ethan Coon (ecoon@lanl.gov)
*/

#ifndef AMANZI_ENERGY_SURFACE_ICE_ENERGY_MODEL_HH_
#define AMANZI_ENERGY_SURFACE_ICE_ENERGY_MODEL_HH_

namespace Amanzi {
namespace Energy {
namespace Relations {

class SurfaceIceEnergyModel {

 public:
  explicit
  SurfaceIceEnergyModel(Teuchos::ParameterList& plist);

  double Energy(double h, double eta, double nl, double ul, double ni, double ui, double cv) const;

  double DEnergyDPondedDepth(double h, double eta, double nl, double ul, double ni, double ui, double cv) const;
  double DEnergyDUnfrozenFraction(double h, double eta, double nl, double ul, double ni, double ui, double cv) const;
  double DEnergyDMolarDensityLiquid(double h, double eta, double nl, double ul, double ni, double ui, double cv) const;
  double DEnergyDInternalEnergyLiquid(double h, double eta, double nl, double ul, double ni, double ui, double cv) const;
  double DEnergyDMolarDensityIce(double h, double eta, double nl, double ul, double ni, double ui, double cv) const;
  double DEnergyDInternalEnergyIce(double h, double eta, double nl, double ul, double ni, double ui, double cv) const;
  double DEnergyDCellVolume(double h, double eta, double nl, double ul, double ni, double ui, double cv) const;
  
 protected:
  void InitializeFromPlist_(Teuchos::ParameterList& plist);

 protected:



};

} //namespace
} //namespace
} //namespace

#endif