//@HEADER
// ************************************************************************
//     Genten: Software for Generalized Tensor Decompositions
//     by Sandia National Laboratories
//
// Sandia National Laboratories is a multimission laboratory managed
// and operated by National Technology and Engineering Solutions of Sandia,
// LLC, a wholly owned subsidiary of Honeywell International, Inc., for the
// U.S. Department of Energy's National Nuclear Security Administration under
// contract DE-NA0003525.
//
// Copyright 2017 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// ************************************************************************
//@HEADER

#pragma once

#include "Genten_GCP_SGD_Iter.hpp"

// To do:
//  * Fix VectorSize (use largest power of 2 <= nc, max of 128)
//  * Test on Volta.  Do we need warp sync's?
//  * Understand why/fix RowBlockSize == 128 causes nan's
//  * Template on stepper and add eval_async()
//  * Fix GCP-SGD banner when using async
//  * Fix timers
//  * Compare performance on larger tensors with longer modes
//  * Implement other asynchronous steppers

namespace Genten {

  namespace Impl {

    template <typename ExecSpace, typename LossFunction>
    void gcp_sgd_iter_async_kernel(
      const SptensorT<ExecSpace>& X,
      const KtensorT<ExecSpace>& u,
      const LossFunction& f,
      const ttb_indx nsz,
      const ttb_indx nsnz,
      const ttb_real wz,
      const ttb_real wnz,
      Kokkos::Random_XorShift64_Pool<ExecSpace>& rand_pool,
      const GCP_SGD_Step<ExecSpace,LossFunction>& stepper,
      const AlgParams& algParams)
    {
      typedef Kokkos::TeamPolicy<ExecSpace> Policy;
      typedef typename Policy::member_type TeamMember;
      typedef Kokkos::Random_XorShift64_Pool<ExecSpace> RandomPool;
      typedef typename RandomPool::generator_type generator_type;
      typedef Kokkos::rand<generator_type, ttb_indx> Rand;
      typedef Kokkos::View< ttb_indx**, Kokkos::LayoutRight, typename ExecSpace::scratch_memory_space , Kokkos::MemoryUnmanaged > IndScratchSpace;
      typedef Kokkos::View< ttb_real***, Kokkos::LayoutRight, typename ExecSpace::scratch_memory_space , Kokkos::MemoryUnmanaged > KtnScratchSpace;

      static const bool is_cuda = Genten::is_cuda_space<ExecSpace>::value;
      static const unsigned RowBlockSize = 1; // Doesn't work with 128?
      static const unsigned VectorSize = is_cuda ? 16 : 1; // FIXME
      static const unsigned TeamSize = is_cuda ? 128/VectorSize : 1;
      static const unsigned RowsPerTeam = TeamSize * RowBlockSize;

      /*const*/ ttb_indx num_samples = (nsz+nsnz)*algParams.epoch_iters;
      /*const*/ ttb_indx nnz = X.nnz();
      /*const*/ unsigned nd = u.ndims();
      /*const*/ unsigned nc = u.ncomponents();
      const ttb_indx N = (num_samples+RowsPerTeam-1)/RowsPerTeam;
      const size_t bytes =
        IndScratchSpace::shmem_size(TeamSize,nd) +
        KtnScratchSpace::shmem_size(TeamSize,nd,nc);

      const ttb_real step = stepper.getStep(); // FIXME

      Policy policy(N, TeamSize, VectorSize);
      Kokkos::parallel_for(
        policy.set_scratch_size(0,Kokkos::PerTeam(bytes)),
        KOKKOS_LAMBDA(const TeamMember& team)
      {
        generator_type gen = rand_pool.get_state();
        IndScratchSpace team_ind(team.team_scratch(0), TeamSize, nd);
        KtnScratchSpace team_ktn(team.team_scratch(0), TeamSize, nd, nc);
        const ttb_indx team_rank = team.team_rank();

        for (unsigned ii=0; ii<RowBlockSize; ++ii) {

          // Randomly choose if this is a zero or nonzero sample based
          // on the fraction of requested zero/nonzero samples
          ttb_indx idx = 0;
          Kokkos::single( Kokkos::PerThread( team ), [&] (ttb_indx& i)
          {
            i = Rand::draw(gen,0,nsz+nsnz);
          }, idx);
          const bool nonzero_sample = idx < nsnz;

          ttb_real x_val = 0.0;
          if (nonzero_sample) {
            // Generate random tensor index
            Kokkos::single( Kokkos::PerThread( team ), [&] (ttb_real& xv)
            {
              const ttb_indx i = Rand::draw(gen,0,nnz);
              for (ttb_indx m=0; m<nd; ++m)
                team_ind(team_rank,m) = X.subscript(i,m);
              xv = X.value(i);
            }, x_val);
          }
          else {
            // Generate index -- use broadcast form to force warp sync
            // so that ind is updated before used by other threads
            int sync = 0;
            Kokkos::single( Kokkos::PerThread( team ), [&] (int& s)
            {
              for (ttb_indx m=0; m<nd; ++m)
                team_ind(team_rank,m) = Rand::draw(gen,0,X.size(m));
              s = 1;
            }, sync);
            x_val = 0.0;
          }

          // Read u
          for (unsigned m=0; m<nd; ++m) {
            const ttb_indx k = team_ind(team_rank,m);
            Kokkos::parallel_for(Kokkos::ThreadVectorRange(team,nc),
                                 [&] (const unsigned& j)
            {
              team_ktn(team_rank, m, j) = u[m].entry(k,j);
            });
          }

          // Compute Ktensor value
          ttb_real m_val = 0.0;
          Kokkos::parallel_reduce(Kokkos::ThreadVectorRange(team,nc),
                                  [&] (const unsigned& j, ttb_real& mv)
          {
            ttb_real tmp = 1.0;
            for (unsigned m=0; m<nd; ++m)
              tmp *= team_ktn(team_rank, m, j);
            mv += tmp;
          }, m_val);

          // Compute Y value
          ttb_real y_val;
          if (nonzero_sample)
            y_val = wnz * (f.deriv(x_val, m_val) -
                           f.deriv(ttb_real(0.0), m_val));
          else
             y_val = wz * f.deriv(ttb_real(0.0), m_val);

          // Compute gradient contribution
          for (unsigned n=0; n<nd; ++n) {
            const ttb_indx k = team_ind(team_rank,n);
            Kokkos::parallel_for(Kokkos::ThreadVectorRange(team,nc),
                                 [&] (const unsigned& j)
            {
              ttb_real tmp = y_val;
              for (unsigned m=0; m<nd; ++m)
                if (m != n)
                  tmp *= team_ktn(team_rank, m, j);
              // SGD step -- FIXME to call stepper
              Kokkos::atomic_add(&u[n].entry(k,j), (-step)*tmp);
            });
          }
        }
        rand_pool.free_state(gen);
      }, "gcp_sgd_iter_asyn_kernel");
    }

    template <typename ExecSpace, typename LossFunction>
    struct GCP_SGD_Iter_Async : public GCP_SGD_Iter<ExecSpace, LossFunction> {
      GCP_SGD_Iter_Async() {}

      virtual ~GCP_SGD_Iter_Async() {}

      virtual void run(SptensorT<ExecSpace>& X,
                       const LossFunction& loss_func,
                       const AlgParams& algParams,
                       Sampler<ExecSpace,LossFunction>& sampler,
                       GCP_SGD_Step<ExecSpace,LossFunction>& stepper)
      {
        // Cast sampler to SemiStratified and extract data
        SemiStratifiedSampler<ExecSpace,LossFunction>& semi_strat_sampler =
          dynamic_cast<SemiStratifiedSampler<ExecSpace,LossFunction>&>(sampler);
        const ttb_indx nsz = semi_strat_sampler.getNumSamplesZerosGrad();
        const ttb_indx nsnz = semi_strat_sampler.getNumSamplesNonzerosGrad();
        const ttb_real wz = semi_strat_sampler.getWeightZerosGrad();
        const ttb_real wnz = semi_strat_sampler.getWeightNonzerosGrad();
        auto& rand_pool = semi_strat_sampler.getRandPool();

        // Run kernel
        gcp_sgd_iter_async_kernel(
          X,this->ut,loss_func,nsz,nsnz,wz,wnz,rand_pool,stepper,
          algParams);

        // Update number of iterations
        this->total_iters += algParams.epoch_iters;
      }
    };

  }

}