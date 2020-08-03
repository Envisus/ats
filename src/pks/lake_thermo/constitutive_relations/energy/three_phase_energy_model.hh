/*
  The three phase energy model is an algebraic model with dependencies.

  Generated via evaluator_generator with:
Energy for a three-phase, gas+liquid+ice evaluator.
    
  Authors: Ethan Coon (ecoon@lanl.gov)
*/

#ifndef AMANZI_ENERGY_THREE_PHASE_ENERGY_MODEL_HH_
#define AMANZI_ENERGY_THREE_PHASE_ENERGY_MODEL_HH_

namespace Amanzi {
namespace Energy {
namespace Relations {

class ThreePhaseEnergyModel {

 public:
  explicit
  ThreePhaseEnergyModel(Teuchos::ParameterList& plist);

  double Energy(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;

  double DEnergyDPorosity(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDBasePorosity(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDSaturationLiquid(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDMolarDensityLiquid(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDInternalEnergyLiquid(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDSaturationIce(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDMolarDensityIce(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDInternalEnergyIce(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDSaturationGas(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDMolarDensityGas(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDInternalEnergyGas(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDDensityRock(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDInternalEnergyRock(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  double DEnergyDCellVolume(double phi, double phi0, double sl, double nl, double ul, double si, double ni, double ui, double sg, double ng, double ug, double rho_r, double ur, double cv) const;
  
 protected:
  void InitializeFromPlist_(Teuchos::ParameterList& plist);

 protected:



};

} //namespace
} //namespace
} //namespace

#endif