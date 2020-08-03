/* -*-  mode: c++; indent-tabs-mode: nil -*- */

/* -------------------------------------------------------------------------
ATS

License: see $ATS_DIR/COPYRIGHT
Author: Ethan Coon

Internal energy model -- function of temperature only.

UNITS: J/{mol/kg}
------------------------------------------------------------------------- */

#ifndef AMANZI_ENERGYRELATIONS_IEM_
#define AMANZI_ENERGYRELATIONS_IEM_

#include "Teuchos_ParameterList.hpp"

namespace Amanzi {
namespace Energy {

class IEM {

public:
  virtual ~IEM() {}

  // IEM(Teuchos::ParameterList& plist);
  virtual bool IsMolarBasis() = 0;
  virtual double InternalEnergy(double temp) = 0;
  virtual double DInternalEnergyDT(double temp) = 0;
};

}
}

#endif
