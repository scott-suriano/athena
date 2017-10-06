//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file calculate_fluxes.cpp
//  \brief Calculate hydro/MHD fluxes

// C/C++ headers
#include <algorithm>   // min,max

// Athena++ headers
#include "hydro.hpp"
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../coordinates/coordinates.hpp"
#include "../field/field.hpp"
#include "../mesh/mesh.hpp"
#include "../bvals/bvals.hpp"
#include "../reconstruct/reconstruction.hpp"
#include "../gravity/gravity.hpp"

// OpenMP header
#ifdef OPENMP_PARALLEL
#include <omp.h>
#endif

//----------------------------------------------------------------------------------------
//! \fn  void Hydro::CalculateFluxes
//  \brief Calculate Hydrodynamic Fluxes using the Riemann solver

void Hydro::CalculateFluxes(AthenaArray<Real> &w, FaceField &b,
                            AthenaArray<Real> &bcc, int reconstruct_order)
{
  MeshBlock *pmb=pmy_block;
  AthenaArray<Real> &x1flux=flux[X1DIR];
  AthenaArray<Real> &x2flux=flux[X2DIR];
  AthenaArray<Real> &x3flux=flux[X3DIR];
  int is = pmb->is; int js = pmb->js; int ks = pmb->ks;
  int ie = pmb->ie; int je = pmb->je; int ke = pmb->ke;
  int il, iu, jl, ju, kl, ku;

  AthenaArray<Real> b1,b2,b3,w_x1f,w_x2f,w_x3f,e2x1,e3x1,e1x2,e3x2,e1x3,e2x3;
  if (MAGNETIC_FIELDS_ENABLED) {
    b1.InitWithShallowCopy(b.x1f);
    b2.InitWithShallowCopy(b.x2f);
    b3.InitWithShallowCopy(b.x3f);
    w_x1f.InitWithShallowCopy(pmb->pfield->wght.x1f);
    w_x2f.InitWithShallowCopy(pmb->pfield->wght.x2f);
    w_x3f.InitWithShallowCopy(pmb->pfield->wght.x3f);
    e2x1.InitWithShallowCopy(pmb->pfield->e2_x1f);
    e3x1.InitWithShallowCopy(pmb->pfield->e3_x1f);
    e1x2.InitWithShallowCopy(pmb->pfield->e1_x2f);
    e3x2.InitWithShallowCopy(pmb->pfield->e3_x2f);
    e1x3.InitWithShallowCopy(pmb->pfield->e1_x3f);
    e2x3.InitWithShallowCopy(pmb->pfield->e2_x3f);
  }

  int tid=0;
  int nthreads = pmb->pmy_mesh->GetNumMeshThreads();
#pragma omp parallel default(shared) private(tid) num_threads(nthreads)
{
#ifdef OPENMP_PARALLEL
  tid=omp_get_thread_num();
#endif

  AthenaArray<Real> wl, wr;
  wl.InitWithShallowCopy(wl_);
  wr.InitWithShallowCopy(wr_);
  AthenaArray<Real> dxw;
  dxw.InitWithShallowSlice(dxw_,2,tid,1);

//----------------------------------------------------------------------------------------
// i-direction

  // set the loop limits
  jl=js, ju=je, kl=ks, ku=ke;
  if (MAGNETIC_FIELDS_ENABLED) {
    if(pmb->block_size.nx2 > 1) {
      if(pmb->block_size.nx3 == 1) // 2D
        jl=js-1, ju=je+1, kl=ks, ku=ke;
      else // 3D
        jl=js-1, ju=je+1, kl=ks-1, ku=ke+1;
    }
  }

  // reconstruct L/R states
  if (reconstruct_order == 1) {
    pmb->precon->DonorCellX1(kl,ku,jl,ju,is,ie+1,w,IDN,IDN,wl,wr);
    pmb->precon->DonorCellX1(kl,ku,jl,ju,is,ie+1,w,IM1,IM1,wl,wr);
    pmb->precon->DonorCellX1(kl,ku,jl,ju,is,ie+1,w,IM2,IM2,wl,wr);
    pmb->precon->DonorCellX1(kl,ku,jl,ju,is,ie+1,w,IM3,IM3,wl,wr);
    if(NON_BAROTROPIC_EOS) pmb->precon->DonorCellX1(kl,ku,jl,ju,is,ie+1,w,IEN,IEN,wl,wr);
    if (MAGNETIC_FIELDS_ENABLED) {
      pmb->precon->DonorCellX1(kl,ku,jl,ju,is,ie+1,bcc,IB2,IBY,wl,wr);
      pmb->precon->DonorCellX1(kl,ku,jl,ju,is,ie+1,bcc,IB3,IBZ,wl,wr);
    }
  } else {
    pmb->precon->ReconstructFuncX1(pmb->pcoord,kl,ku,jl,ju,is,ie+1,w,IDN,IDN,wl,wr);
    pmb->precon->ReconstructFuncX1(pmb->pcoord,kl,ku,jl,ju,is,ie+1,w,IM1,IM1,wl,wr);
    pmb->precon->ReconstructFuncX1(pmb->pcoord,kl,ku,jl,ju,is,ie+1,w,IM2,IM2,wl,wr);
    pmb->precon->ReconstructFuncX1(pmb->pcoord,kl,ku,jl,ju,is,ie+1,w,IM3,IM3,wl,wr);
    if(NON_BAROTROPIC_EOS) pmb->precon->ReconstructFuncX1(pmb->pcoord,kl,ku,jl,ju,is,ie+1,w,IEN,IEN,wl,wr);
    if (MAGNETIC_FIELDS_ENABLED) {
      pmb->precon->ReconstructFuncX1(pmb->pcoord,kl,ku,jl,ju,is,ie+1,bcc,IB2,IBY,wl,wr);
      pmb->precon->ReconstructFuncX1(pmb->pcoord,kl,ku,jl,ju,is,ie+1,bcc,IB3,IBZ,wl,wr);
    }
  }

  // compute fluxes, store directly into 3D arrays
  // x1flux(IBY) = (v1*b2 - v2*b1) = -EMFZ
  // x1flux(IBZ) = (v1*b3 - v3*b1) =  EMFY
  RiemannSolver(kl,ku,jl,ju,is,ie+1,IVX,b1,wl,wr,x1flux,e3x1,e2x1);

  // compute weights for GS07 CT algorithm
  if (MAGNETIC_FIELDS_ENABLED) {
    for (int k=kl; k<=ku; ++k){ 
#pragma omp for schedule(static)
    for (int j=jl; j<=ju; ++j){

      pmb->pcoord->CenterWidth1(k,j,is,ie+1,dxw);
#pragma simd
      for (int i=is; i<=ie+1; ++i){
        Real v_over_c = (1024.0)*(pmb->pmy_mesh->dt)*x1flux(IDN,k,j,i)
                      / (dxw(i)*(wl(IDN,k,j,i) + wr(IDN,k,j,i)));
        Real tmp_min = std::min(0.5,v_over_c);
        w_x1f(k,j,i) = 0.5 + std::max(-0.5,tmp_min);
      }
    }}
  }

//----------------------------------------------------------------------------------------
// j-direction

  if (pmb->block_size.nx2 > 1) {

    // set the loop limits
    il=is, iu=ie, kl=ks, ku=ke;
    if (MAGNETIC_FIELDS_ENABLED) {
      if(pmb->block_size.nx3 == 1) // 2D
        il=is-1, iu=ie+1, kl=ks, ku=ke;
      else // 3D
        il=is-1, iu=ie+1, kl=ks-1, ku=ke+1;
    }

    // reconstruct L/R states at j
    if (reconstruct_order == 1) {
      pmb->precon->DonorCellX2(kl,ku,js,je+1,il,iu,w,IDN,IDN,wl,wr);
      pmb->precon->DonorCellX2(kl,ku,js,je+1,il,iu,w,IM1,IM1,wl,wr);
      pmb->precon->DonorCellX2(kl,ku,js,je+1,il,iu,w,IM2,IM2,wl,wr);
      pmb->precon->DonorCellX2(kl,ku,js,je+1,il,iu,w,IM3,IM3,wl,wr);
      if(NON_BAROTROPIC_EOS) pmb->precon->DonorCellX2(kl,ku,js,je+1,il,iu,w,IEN,IEN,wl,wr);
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->precon->DonorCellX2(kl,ku,js,je+1,il,iu,bcc,IB3,IBY,wl,wr);
        pmb->precon->DonorCellX2(kl,ku,js,je+1,il,iu,bcc,IB1,IBZ,wl,wr);
      }
    } else {
      pmb->precon->ReconstructFuncX2(pmb->pcoord,kl,ku,js,je+1,il,iu,w,IDN,IDN,wl,wr);
      pmb->precon->ReconstructFuncX2(pmb->pcoord,kl,ku,js,je+1,il,iu,w,IM1,IM1,wl,wr);
      pmb->precon->ReconstructFuncX2(pmb->pcoord,kl,ku,js,je+1,il,iu,w,IM2,IM2,wl,wr);
      pmb->precon->ReconstructFuncX2(pmb->pcoord,kl,ku,js,je+1,il,iu,w,IM3,IM3,wl,wr);
      if(NON_BAROTROPIC_EOS) pmb->precon->ReconstructFuncX2(pmb->pcoord,kl,ku,js,je+1,il,iu,w,IEN,IEN,wl,wr);
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->precon->ReconstructFuncX2(pmb->pcoord,kl,ku,js,je+1,il,iu,bcc,IB3,IBY,wl,wr);
        pmb->precon->ReconstructFuncX2(pmb->pcoord,kl,ku,js,je+1,il,iu,bcc,IB1,IBZ,wl,wr);
      }
    }

    // compute fluxes, store directly into 3D arrays
    // flx(IBY) = (v2*b3 - v3*b2) = -EMFX
    // flx(IBZ) = (v2*b1 - v1*b2) =  EMFZ
    RiemannSolver(kl,ku,js,je+1,il,iu,IVY,b2,wl,wr,x2flux,e1x2,e3x2);

    // compute weights for GS07 CT algorithm
    if (MAGNETIC_FIELDS_ENABLED) {
      for (int k=kl; k<=ku; ++k){
#pragma omp for schedule(static)
      for (int j=js; j<=je+1; ++j){
        pmb->pcoord->CenterWidth2(k,j,il,iu,dxw);
#pragma simd
        for (int i=il; i<=iu; ++i){
          Real v_over_c = (1024.0)*(pmb->pmy_mesh->dt)*x2flux(IDN,k,j,i)
                        / (dxw(i)*(wl(IDN,k,j,i) + wr(IDN,k,j,i)));
          Real tmp_min = std::min(0.5,v_over_c);
          w_x2f(k,j,i) = 0.5 + std::max(-0.5,tmp_min);
        }
      }}
    }
  }

//----------------------------------------------------------------------------------------
// k-direction 

  if (pmb->block_size.nx3 > 1) {

    // set the loop limits
    il=is, iu=ie, jl=js, ju=je;
    if (MAGNETIC_FIELDS_ENABLED)
      il=is-1, iu=ie+1, jl=js-1, ju=je+1;

    // reconstruct L/R states at k
    if (reconstruct_order == 1) {
      pmb->precon->DonorCellX3(ks,ke+1,jl,ju,il,iu,w,IDN,IDN,wl,wr);
      pmb->precon->DonorCellX3(ks,ke+1,jl,ju,il,iu,w,IM1,IM1,wl,wr);
      pmb->precon->DonorCellX3(ks,ke+1,jl,ju,il,iu,w,IM2,IM2,wl,wr);
      pmb->precon->DonorCellX3(ks,ke+1,jl,ju,il,iu,w,IM3,IM3,wl,wr);
      if(NON_BAROTROPIC_EOS) pmb->precon->DonorCellX3(ks,ke+1,jl,ju,il,iu,w,IEN,IEN,wl,wr);
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->precon->DonorCellX3(ks,ke+1,jl,ju,il,iu,bcc,IB1,IBY,wl,wr);
        pmb->precon->DonorCellX3(ks,ke+1,jl,ju,il,iu,bcc,IB2,IBZ,wl,wr);
      }
    } else {
      pmb->precon->ReconstructFuncX3(pmb->pcoord,ks,ke+1,jl,ju,il,iu,w,IDN,IDN,wl,wr);
      pmb->precon->ReconstructFuncX3(pmb->pcoord,ks,ke+1,jl,ju,il,iu,w,IM1,IM1,wl,wr);
      pmb->precon->ReconstructFuncX3(pmb->pcoord,ks,ke+1,jl,ju,il,iu,w,IM2,IM2,wl,wr);
      pmb->precon->ReconstructFuncX3(pmb->pcoord,ks,ke+1,jl,ju,il,iu,w,IM3,IM3,wl,wr);
      if(NON_BAROTROPIC_EOS) pmb->precon->ReconstructFuncX3(pmb->pcoord,ks,ke+1,jl,ju,il,iu,w,IEN,IEN,wl,wr);
      if (MAGNETIC_FIELDS_ENABLED) {
        pmb->precon->ReconstructFuncX3(pmb->pcoord,ks,ke+1,jl,ju,il,iu,bcc,IB1,IBY,wl,wr);
        pmb->precon->ReconstructFuncX3(pmb->pcoord,ks,ke+1,jl,ju,il,iu,bcc,IB2,IBZ,wl,wr);
      }
    }

    // compute fluxes, store directly into 3D arrays
    // flx(IBY) = (v3*b1 - v1*b3) = -EMFY
    // flx(IBZ) = (v3*b2 - v2*b3) =  EMFX
    RiemannSolver(ks,ke+1,jl,ju,il,iu,IVZ,b3,wl,wr,x3flux,e2x3,e1x3);

    // compute weights for GS07 CT algorithm
    if (MAGNETIC_FIELDS_ENABLED) {
#pragma omp for schedule(static)
      for (int k=ks; k<=ke+1; ++k){
      for (int j=jl; j<=ju; ++j){
        pmb->pcoord->CenterWidth3(k,j,il,iu,dxw);
#pragma simd
        for (int i=il; i<=iu; ++i){
          Real v_over_c = (1024.0)*(pmb->pmy_mesh->dt)*x3flux(IDN,k,j,i)
                        / (dxw(i)*(wl(IDN,k,j,i) + wr(IDN,k,j,i)));
          Real tmp_min = std::min(0.5,v_over_c);
          w_x3f(k,j,i) = 0.5 + std::max(-0.5,tmp_min);
        }
      }}
    }
  }

} // end of omp parallel region

  if(SELF_GRAVITY_ENABLED == 1) AddGravityFluxMG();
  if(SELF_GRAVITY_ENABLED == 2) AddGravityFluxMG();

  return;
}
