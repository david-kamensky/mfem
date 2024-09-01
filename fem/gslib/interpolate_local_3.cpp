// Copyright (c) 2010-2024, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "../gslib.hpp"
#include "../../general/forall.hpp"

#ifdef MFEM_USE_GSLIB

namespace mfem
{

#define CODE_INTERNAL 0
#define CODE_BORDER 1
#define CODE_NOT_FOUND 2

static MFEM_HOST_DEVICE void lagrange_eval(double *p0, double x,
                                           int i, int p_Nq,
                                           double *z, double *lagrangeCoeff)
{
   double p_i = (1 << (p_Nq - 1));
   for (int j = 0; j < p_Nq; ++j)
   {
      double d_j = x - z[j];
      p_i *= j == i ? 1 : d_j;
   }
   p0[i] = lagrangeCoeff[i] * p_i;
}

template<int T_D1D = 0>
static void InterpolateLocal3DKernel(const double *const gf_in,
                                     int *const el,
                                     double *const r,
                                     double *const int_out,
                                     const int npt,
                                     const int ncomp,
                                     const int nel,
                                     const int gf_offset,
                                     double *gll1D,
                                     double *lagcoeff,
                                     const int pN = 0)
{
   const int Nfields = ncomp;
   const int MD1 = T_D1D ? T_D1D : 10;
   const int D1D = T_D1D ? T_D1D : pN;
   const int p_Np = D1D*D1D*D1D;
   MFEM_VERIFY(MD1 <= 10,"Increase Max allowable polynomial order.");
   MFEM_VERIFY(D1D != 0, "Polynomial order not specified.");
   mfem::forall_2D(npt, D1D, D1D, [=] MFEM_HOST_DEVICE (int i)
   {
      MFEM_SHARED double wtr[3*MD1];
      MFEM_SHARED double sums[MD1*MD1];

      // Evaluate basis functions at the reference space coordinates
      MFEM_FOREACH_THREAD(j,x,D1D)
      {
         MFEM_FOREACH_THREAD(k,y,D1D)
         {
            if (k <= 2)
            {
               lagrange_eval(wtr + k*D1D, r[3*i+k], j, D1D, gll1D, lagcoeff);
            }
            if (D1D == 2 && k == 1)
            {
               lagrange_eval(wtr + 2*D1D, r[3*i+2], j, D1D, gll1D, lagcoeff);
            }
         }
      }
      MFEM_SYNC_THREAD;

      for (int fld = 0; fld < Nfields; ++fld)
      {
         // If using GetNodalValues, ordering is NDOFSxNELxVDIM
         // const int elemOffset = el[i] * p_Np + fld * gf_offset;
         //if using R->Mult for L -> E-Vec use below.
         const int elemOffset = el[i] * p_Np * Nfields + fld * p_Np;
         MFEM_FOREACH_THREAD(j,x,D1D)
         {
            MFEM_FOREACH_THREAD(k,y,D1D)
            {
               sums[j + k*D1D] = 0.0;
               for (int l = 0; l < D1D; ++l)
               {
                  sums[j + k*D1D] += gf_in[elemOffset + j + k*D1D + l*D1D*D1D] *
                                     wtr[2*D1D+l];
               }
               sums[j+k*D1D] *= wtr[D1D+k]*wtr[j];
            }
         }
         MFEM_SYNC_THREAD;

         MFEM_FOREACH_THREAD(j,x,D1D)
         {
            MFEM_FOREACH_THREAD(k,y,D1D)
            {
               if (j == 0 && k == 0)
               {
                  double sumv = 0.0;
                  for (int jj = 0; jj < D1D*D1D; ++jj)
                  {
                     sumv += sums[jj];
                  }
                  int_out[i + fld * npt] = sumv;
               }
            }
         }
         MFEM_SYNC_THREAD;
      }
   });
}

void FindPointsGSLIB::InterpolateLocal3(const Vector &field_in,
                                        Array<int> &gsl_elem_dev_l,
                                        Vector &gsl_ref_l,
                                        Vector &field_out,
                                        int npt, int ncomp,
                                        int nel, int dof1Dsol)
{
   if (npt == 0) { return; }
   const int gf_offset = field_in.Size()/ncomp;
   auto pfin = field_in.Read();
   auto pgsle = gsl_elem_dev_l.ReadWrite();
   auto pgslr = gsl_ref_l.ReadWrite();
   auto pfout = field_out.Write();
   auto pgll = DEV.gll1d_sol.ReadWrite();
   auto plcf = DEV.lagcoeff_sol.ReadWrite();
   switch (dof1Dsol)
   {
      case 2: return InterpolateLocal3DKernel<2>(pfin, pgsle, pgslr, pfout,
                                                    npt, ncomp, nel, gf_offset,
                                                    pgll, plcf);
      case 3: return InterpolateLocal3DKernel<3>(pfin, pgsle, pgslr, pfout,
                                                    npt, ncomp, nel, gf_offset,
                                                    pgll, plcf);
      case 4: return InterpolateLocal3DKernel<4>(pfin, pgsle, pgslr, pfout,
                                                    npt, ncomp, nel, gf_offset,
                                                    pgll, plcf);
      case 5: return InterpolateLocal3DKernel<5>(pfin, pgsle, pgslr, pfout,
                                                    npt, ncomp, nel, gf_offset,
                                                    pgll, plcf);
      default: return InterpolateLocal3DKernel(pfin, pgsle, pgslr, pfout,
                                                  npt, ncomp, nel, gf_offset,
                                                  pgll, plcf, dof1Dsol);
   }
}


#undef CODE_INTERNAL
#undef CODE_BORDER
#undef CODE_NOT_FOUND

} // namespace mfem

#endif //ifdef MFEM_USE_GSLIB
