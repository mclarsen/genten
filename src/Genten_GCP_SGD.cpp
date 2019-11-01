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

#include <iomanip>
#include <algorithm>
#include <cmath>

#include "Genten_GCP_SGD.hpp"
#include "Genten_GCP_StratifiedSampler.hpp"
#include "Genten_GCP_SemiStratifiedSampler.hpp"
#include "Genten_GCP_ValueKernels.hpp"
#include "Genten_GCP_LossFunctions.hpp"
#include "Genten_GCP_KokkosVector.hpp"

#include "Genten_Sptensor.hpp"
#include "Genten_SystemTimer.hpp"
#include "Genten_RandomMT.hpp"
#include "Genten_MixedFormatOps.hpp"

#ifdef HAVE_CALIPER
#include <caliper/cali.h>
#endif

namespace Genten {

  namespace Impl {

    template <typename TensorT, typename ExecSpace, typename LossFunction>
    void gcp_sgd_impl(TensorT& X, KtensorT<ExecSpace>& u0,
                      const LossFunction& loss_func,
                      const AlgParams& algParams,
                      ttb_indx& numEpochs,
                      ttb_real& fest,
                      std::ostream& out)
    {
      typedef GCP::KokkosVector<ExecSpace> VectorType;
      typedef typename VectorType::view_type view_type;
      using std::sqrt;
      using std::pow;

      const ttb_indx nd = u0.ndims();
      const ttb_indx nc = u0.ncomponents();

      // Constants for the algorithm
      const ttb_real tol = algParams.gcp_tol;
      const ttb_real decay = algParams.decay;
      const ttb_real rate = algParams.rate;
      const ttb_indx max_fails = algParams.max_fails;
      const ttb_indx epoch_iters = algParams.epoch_iters;
      const ttb_indx frozen_iters = algParams.frozen_iters;
      const ttb_indx seed = algParams.seed;
      const ttb_indx maxEpochs = algParams.maxiters;
      const ttb_indx printIter = algParams.printitn;
      const bool compute_fit = algParams.compute_fit;

      // ADAM parameters
      const bool use_adam = algParams.use_adam;
      const ttb_real beta1 = algParams.adam_beta1;
      const ttb_real beta2 = algParams.adam_beta2;
      const ttb_real eps = algParams.adam_eps;

      // Create sampler
      Sampler<ExecSpace,LossFunction> *sampler = nullptr;
      if (algParams.sampling_type == GCP_Sampling::Stratified)
        sampler = new Genten::StratifiedSampler<ExecSpace,LossFunction>(
          X, algParams);
      else if (algParams.sampling_type == GCP_Sampling::SemiStratified)
        sampler = new Genten::SemiStratifiedSampler<ExecSpace,LossFunction>(
          X, algParams);
      else
        Genten::error("Genten::gcp_sgd - unknown sampling type");

      // bounds
      constexpr bool has_bounds = (LossFunction::has_lower_bound() ||
                                   LossFunction::has_upper_bound());
      constexpr ttb_real lb = LossFunction::lower_bound();
      constexpr ttb_real ub = LossFunction::upper_bound();

      if (printIter > 0) {
        out << "Starting GCP-SGD" << std::endl;
        sampler->print(out);
      }

      // Timers -- turn on fences when timing info is requested so we get
      // accurate kernel times
      int num_timers = 0;
      const int timer_sgd = num_timers++;
      const int timer_sort = num_timers++;
      const int timer_sample_f = num_timers++;
      const int timer_sample_g = num_timers++;
      const int timer_fest = num_timers++;
      const int timer_grad = num_timers++;
      const int timer_grad_nzs = num_timers++;
      const int timer_grad_zs = num_timers++;
      const int timer_grad_init = num_timers++;
      const int timer_step = num_timers++;
      const int timer_sample_g_z_nz = num_timers++;
      const int timer_sample_g_perm = num_timers++;
      SystemTimer timer(num_timers, algParams.timings);

      // Start timer for total execution time of the algorithm.
      timer.start(timer_sgd);

      // Distribute the initial guess to have weights of one.
      u0.normalize(Genten::NormTwo);
      u0.distribute();

      // Ktensor-vector for solution
      VectorType u(u0);
      u.copyFromKtensor(u0);
      KtensorT<ExecSpace> ut = u.getKtensor();

      // Gradient Ktensor
      VectorType g = u.clone();
      KtensorT<ExecSpace> gt = g.getKtensor();

      // Copy Ktensor for restoring previous solution
      VectorType u_prev = u.clone();
      u_prev.set(u);

      // ADAM first (m) and second (v) moment vectors
      VectorType adam_m, adam_v, adam_m_prev, adam_v_prev;
      if (use_adam) {
        adam_m = u.clone();
        adam_v = u.clone();
        adam_m_prev = u.clone();
        adam_v_prev = u.clone();
        adam_m.zero();
        adam_v.zero();
        adam_m_prev.zero();
        adam_v_prev.zero();
      }

      // Initialize sampler (sorting, hashing, ...)
      timer.start(timer_sort);
      RandomMT rng(seed);
      Kokkos::Random_XorShift64_Pool<ExecSpace> rand_pool(rng.genrnd_int32());
      sampler->initialize(rand_pool, out);
      timer.stop(timer_sort);

      // Sample X for f-estimate
      SptensorT<ExecSpace> X_val, X_grad;
      ArrayT<ExecSpace> w_val, w_grad;
      timer.start(timer_sample_f);
      sampler->sampleTensor(false, ut, loss_func, X_val, w_val);
      timer.stop(timer_sample_f);

      // Objective estimates
      ttb_real fit = 0.0;
      ttb_real x_norm = 0.0;
      timer.start(timer_fest);
      fest = Impl::gcp_value(X_val, ut, w_val, loss_func);
      if (compute_fit) {
        x_norm = X.norm();
        ttb_real u_norm = u.normFsq();
        ttb_real dot = innerprod(X, ut);
        fit = 1.0 - sqrt(x_norm*x_norm + u_norm*u_norm - 2.0*dot) / x_norm;
      }
      timer.stop(timer_fest);
      ttb_real fest_prev = fest;
      ttb_real fit_prev = fit;

      if (printIter > 0) {
        out << "Initial f-est: "
            << std::setw(13) << std::setprecision(6) << std::scientific
            << fest;
        if (compute_fit)
          out << ", fit: "
              << std::setw(10) << std::setprecision(3) << std::scientific
              << fit;
        out << std::endl;
      }

      // SGD epoch loop
      ttb_real nuc = 1.0;
      ttb_indx total_iters = 0;
      ttb_indx nfails = 0;
      ttb_real beta1t = 1.0;
      ttb_real beta2t = 1.0;
      for (numEpochs=0; numEpochs<maxEpochs; ++numEpochs) {
        // Gradient step size
        ttb_real step = nuc*rate;

        // Epoch iterations
        for (ttb_indx iter=0; iter<epoch_iters; ++iter) {

          // ADAM step size
          // Note sure if this should be constant for frozen iters?
          ttb_real adam_step = 0.0;
          if (use_adam) {
            beta1t = beta1 * beta1t;
            beta2t = beta2 * beta2t;
            adam_step = step*sqrt(1.0-beta2t) / (1.0-beta1t);
          }

          // sample for gradient
          if (!algParams.fuse) {
            timer.start(timer_sample_g);
            timer.start(timer_sample_g_z_nz);
            sampler->sampleTensor(true, ut, loss_func,  X_grad, w_grad);
            timer.stop(timer_sample_g_z_nz);
            timer.start(timer_sample_g_perm);
            if (algParams.mttkrp_method == MTTKRP_Method::Perm)
              X_grad.createPermutation();
            timer.stop(timer_sample_g_perm);
            timer.stop(timer_sample_g);
          }

          for (ttb_indx giter=0; giter<frozen_iters; ++giter) {
             ++total_iters;

            // compute gradient
            timer.start(timer_grad);
            if (algParams.fuse) {
              timer.start(timer_grad_init);
              g.zero(); // algorithm does not use weights
              timer.stop(timer_grad_init);
              sampler->fusedGradient(ut, loss_func, gt,
                                     timer, timer_grad_nzs, timer_grad_zs);
            }
            else {
              gt.weights() = 1.0; // gt is zeroed in mttkrp
              // for (unsigned m=0; m<nd; ++m)
              //   mttkrp(X_grad, ut, m, gt[m], algParams);
              mttkrp_all(X_grad, ut, gt, algParams);
            }
            timer.stop(timer_grad);

            // take step and clip for bounds
            timer.start(timer_step);
            view_type uv = u.getView();
            view_type gv = g.getView();
            if (use_adam) {
              view_type mv = adam_m.getView();
              view_type vv = adam_v.getView();
              u.apply_func(KOKKOS_LAMBDA(const ttb_indx i)
              {
                mv(i) = beta1*mv(i) + (1.0-beta1)*gv(i);
                vv(i) = beta2*vv(i) + (1.0-beta2)*gv(i)*gv(i);
                ttb_real uu = uv(i);
                uu -= adam_step*mv(i)/sqrt(vv(i)+eps);
                if (has_bounds)
                  uu = uu < lb ? lb : (uu > ub ? ub : uu);
                uv(i) = uu;
              });
            }
            else {
              u.apply_func(KOKKOS_LAMBDA(const ttb_indx i)
              {
                ttb_real uu = uv(i);
                uu -= step*gv(i);
                if (has_bounds)
                  uu = uu < lb ? lb : (uu > ub ? ub : uu);
                uv(i) = uu;
              });
            }
            timer.stop(timer_step);
          }
        }

        // compute objective estimate
        timer.start(timer_fest);
        fest = Impl::gcp_value(X_val, ut, w_val, loss_func);
        if (compute_fit) {
          ttb_real u_norm = u.normFsq();
          ttb_real dot = innerprod(X, ut);
          fit = 1.0 - sqrt(x_norm*x_norm + u_norm*u_norm - 2.0*dot) / x_norm;
        }
        timer.stop(timer_fest);

        // check convergence
        const bool failed_epoch = fest > fest_prev;

        if (failed_epoch)
          ++nfails;

         // Print progress of the current iteration.
        if ((printIter > 0) && (((numEpochs + 1) % printIter) == 0)) {
          out << "Epoch " << std::setw(3) << numEpochs + 1 << ": f-est = "
              << std::setw(13) << std::setprecision(6) << std::scientific
              << fest;
          if (compute_fit)
            out << ", fit = "
                << std::setw(10) << std::setprecision(3) << std::scientific
                << fit;
          out << ", step = "
              << std::setw(8) << std::setprecision(1) << std::scientific
              << step;
          if (failed_epoch)
            out << ", nfails = " << nfails
                << " (resetting to solution from last epoch)";
          out << std::endl;
        }

        if (failed_epoch) {
          nuc *= decay;

          // restart from last epoch
          u.set(u_prev);
          fest = fest_prev;
          fit = fit_prev;
          if (use_adam) {
            adam_m.set(adam_m_prev);
            adam_v.set(adam_v_prev);
            beta1t /= pow(beta1,epoch_iters);
            beta2t /= pow(beta2,epoch_iters);
          }
        }
        else {
          // update previous data
          u_prev.set(u);
          fest_prev = fest;
          fit_prev = fit;
          if (use_adam) {
            adam_m_prev.set(adam_m);
            adam_v_prev.set(adam_v);
          }
        }

        if (nfails > max_fails || fest < tol)
          break;
      }
      timer.stop(timer_sgd);

      if (printIter > 0) {
        out << "Final f-est: "
            << std::setw(13) << std::setprecision(6) << std::scientific
            << fest;
        if (compute_fit)
          out << ", fit: "
              << std::setw(10) << std::setprecision(3) << std::scientific
              << fit;
        out << std::endl
            << "GCP-SGD completed " << total_iters << " iterations in "
            << std::setw(8) << std::setprecision(2) << std::scientific
            << timer.getTotalTime(timer_sgd) << " seconds" << std::endl;
        if (algParams.timings) {
          out << "\tsort/hash: " << timer.getTotalTime(timer_sort)
              << " seconds\n"
              << "\tsample-f:  " << timer.getTotalTime(timer_sample_f)
              << " seconds\n";
          if (!algParams.fuse) {
            out << "\tsample-g:  " << timer.getTotalTime(timer_sample_g)
                << " seconds\n"
                << "\t\tzs/nzs:   " << timer.getTotalTime(timer_sample_g_z_nz)
                << " seconds\n";
            if (algParams.mttkrp_method == MTTKRP_Method::Perm) {
              out << "\t\tperm:     " << timer.getTotalTime(timer_sample_g_perm)
                  << " seconds\n";
            }
          }
          out << "\tf-est:     " << timer.getTotalTime(timer_fest)
              << " seconds\n"
              << "\tgradient:  " << timer.getTotalTime(timer_grad)
              << " seconds\n";
          if (algParams.fuse) {
            out << "\t\tinit:    " << timer.getTotalTime(timer_grad_init)
                << " seconds\n"
                << "\t\tnzs:     " << timer.getTotalTime(timer_grad_nzs)
                << " seconds\n"
                << "\t\tzs:      " << timer.getTotalTime(timer_grad_zs)
                << " seconds\n";
          }
          out << "\tstep/clip: " << timer.getTotalTime(timer_step)
              << " seconds\n";
        }
      }

      u.copyToKtensor(u0);

      // Normalize Ktensor u
      u0.normalize(Genten::NormTwo);
      u0.arrange();

      delete sampler;
    }

  }


  template<typename TensorT, typename ExecSpace>
  void gcp_sgd(TensorT& x, KtensorT<ExecSpace>& u,
               const AlgParams& algParams,
               ttb_indx& numIters,
               ttb_real& resNorm,
               std::ostream& out)
  {
#ifdef HAVE_CALIPER
    cali::Function cali_func("Genten::gcp_sgd");
#endif

    // Check size compatibility of the arguments.
    if (u.isConsistent() == false)
      Genten::error("Genten::gcp_sgd - ktensor u is not consistent");
    if (x.ndims() != u.ndims())
      Genten::error("Genten::gcp_sgd - u and x have different num dims");
    for (ttb_indx  i = 0; i < x.ndims(); i++)
    {
      if (x.size(i) != u[i].nRows())
        Genten::error("Genten::gcp_sgd - u and x have different size");
    }

    // Dispatch implementation based on loss function type
    if (algParams.loss_function_type == GCP_LossFunction::Gaussian)
      Impl::gcp_sgd_impl(x, u, GaussianLossFunction(algParams.loss_eps),
                         algParams, numIters, resNorm, out);
    else if (algParams.loss_function_type == GCP_LossFunction::Rayleigh)
      Impl::gcp_sgd_impl(x, u, RayleighLossFunction(algParams.loss_eps),
                         algParams, numIters, resNorm, out);
    else if (algParams.loss_function_type == GCP_LossFunction::Gamma)
      Impl::gcp_sgd_impl(x, u, GammaLossFunction(algParams.loss_eps),
                         algParams, numIters, resNorm, out);
    else if (algParams.loss_function_type == GCP_LossFunction::Bernoulli)
      Impl::gcp_sgd_impl(x, u, BernoulliLossFunction(algParams.loss_eps),
                         algParams, numIters, resNorm, out);
    else if (algParams.loss_function_type == GCP_LossFunction::Poisson)
      Impl::gcp_sgd_impl(x, u, PoissonLossFunction(algParams.loss_eps),
                         algParams, numIters, resNorm, out);
    else
       Genten::error("Genten::gcp_sgd - unknown loss function");
  }

}

#define INST_MACRO(SPACE)                                               \
  template void gcp_sgd<SptensorT<SPACE>,SPACE>(                        \
    SptensorT<SPACE>& x,                                                \
    KtensorT<SPACE>& u,                                                 \
    const AlgParams& algParams,                                         \
    ttb_indx& numIters,                                                 \
    ttb_real& resNorm,                                                  \
    std::ostream& out);

GENTEN_INST(INST_MACRO)
