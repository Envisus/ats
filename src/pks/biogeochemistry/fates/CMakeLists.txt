# -*- mode: cmake -*-

# ATS Surface balance PKs describe Evaporation, energy fluxes from
#  long/showtwave radiation, precip, etc etc etc


include_directories(${ATS_SOURCE_DIR}/src/pks)

add_library(pk_veg
  fates_pk.cc
)

install(TARGETS pk_veg DESTINATION lib)

#================================================
# register evaluators/factories/pks

#generate_evaluators_registration_header(
#  HEADERFILE BGC_registration.hh
#  LISTNAME   BGC_REG
#  INSTALL    True
#)


