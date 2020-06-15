/*
Main functions for biogeochemistry on a column.

Author: Ethan Coon (ecoon@lanl.gov)
        Chonggang Xu (cxu@lanl.gov)

License: BSD
*/


#ifndef ATS_BGC_SIMPLE_FUNCS_HH_
#define ATS_BGC_SIMPLE_FUNCS_HH_

#include <vector>

#include "Teuchos_RCP.hpp"

#include "utils.hh"
#include "PFT.hh"
#include "SoilCarbon.hh"

namespace Amanzi {
namespace BGC {

void BGCAdvance(double t, double dt, double gridarea, double cryoturbation_coef,
             const MetData& met,
             const Epetra_SerialDenseVector& SoilTArr,
             const Epetra_SerialDenseVector& SoilArr,
             const Epetra_SerialDenseVector& SoilDArr,
             const Epetra_SerialDenseVector& SoilThicknessArr,
             std::vector<Teuchos::RCP<PFT> >& pftarr,
             std::vector<Teuchos::RCP<SoilCarbon> >& soilcarr,
             Epetra_SerialDenseVector& SoilCO2Arr,
             Epetra_SerialDenseVector& TransArr,
             double& sw_shaded);

void Cryoturbate(double dt,
		 const Epetra_SerialDenseVector& SoilTArr,
		 const Epetra_SerialDenseVector& SoilDArr,
		 const Epetra_SerialDenseVector& SoilThicknessArr,
		 std::vector<Teuchos::RCP<SoilCarbon> >& soilcarr,
		 double diffusion_coef);

void Cryoturbate(double dt,
		 const Epetra_SerialDenseVector& SoilTArr,
		 const Epetra_SerialDenseVector& SoilDArr,
		 const Epetra_SerialDenseVector& SoilThicknessArr,
		 std::vector<Teuchos::RCP<SoilCarbon> >& soilcarr,
		 std::vector<double>& diffusion_coefs);


} // namespace
} // namespace

#endif

