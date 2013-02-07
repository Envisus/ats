/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
/* -------------------------------------------------------------------------
ATS

License: see $ATS_DIR/COPYRIGHT
Author: Ethan Coon

This class should never be instantiated -- it just provides a base class for
multiple coupler types.
------------------------------------------------------------------------- */

#ifndef PKS_MPC_SURFACE_SUBSURFACE_COUPLER_HH_
#define PKS_MPC_SURFACE_SUBSURFACE_COUPLER_HH_

#include "strong_mpc.hh"

namespace Amanzi {

class MPCSurfaceSubsurfaceCoupler : public StrongMPC {

 public:
  MPCSurfaceSubsurfaceCoupler(Teuchos::ParameterList& plist,
          const Teuchos::RCP<TreeVector>& soln);

  // -- Setup data.
   virtual void setup(const Teuchos::Ptr<State>& S);

 protected:
  Key domain_mesh_key_;
  Key surf_mesh_key_;
  Teuchos::RCP<AmanziMesh::Mesh> surf_mesh_;
  Teuchos::RCP<AmanziMesh::Mesh> domain_mesh_;

  Key domain_pk_name_;
  Key surf_pk_name_;
  Teuchos::RCP<PKBDFBase> surf_pk_;
  Teuchos::RCP<PKBDFBase> domain_pk_;

};

} // namespace


#endif
