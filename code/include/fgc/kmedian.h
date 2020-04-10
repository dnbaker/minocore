#pragma once
#ifndef FGC_KMEDIAN_H__
#define FGC_KMEDIAN_H__
#include "kmeans.h"
#include <algorithm>
#include <boost/accumulators/accumulators.hpp>                                                         
#include <boost/accumulators/statistics/p_square_cumul_dist.hpp>                                       
#include <boost/accumulators/statistics/stats.hpp>                                                     
#include <boost/accumulators/statistics/sum.hpp>                                                       
#include <boost/accumulators/statistics/median.hpp>
#include <boost/accumulators/statistics/weighted_median.hpp>

namespace fgc {
namespace coresets {
using namespace blz;
using namespace boost::accumulators;

template<typename MT, bool SO, typename VT, typename WeightType=const typename MT::ElementType *>
auto &geomedian(const blz::DenseMatrix<MT, SO> &mat, blz::DenseVector<VT, !SO> &dv, double eps=1e-8,
              const WeightType &weights=nullptr) {
    // Solve geometric median for a set of points.
    //
    using FT = typename std::decay_t<decltype(~mat)>::ElementType;
    const auto &_mat = ~mat;
#if 1
    ~dv = blz::mean<blz::columnwise>(_mat);
#else
    (~dv).resize((~mat).columns());
    randomize(~dv);
#endif
    FT prevcost = std::numeric_limits<FT>::max();
    blz::DV<FT, !SO> costs(_mat.rows(), FT(0));
    size_t iternum = 0;
    const size_t nr = _mat.rows();
    assert((~dv).size() == (~mat).columns());
    for(;;) {
        for(size_t i = 0; i < nr; ++i) {
            auto dist = blz::l2Dist(row(_mat, i BLAZE_CHECK_DEBUG), ~dv);
            if(weights) dist *= weights[i];
            costs[i] = dist;
        }
        ++iternum;
        costs = 1. / costs;
        costs *= 1. / blaze::sum(costs);
        ~dv = costs * ~mat;
        FT newcost = l1Dist(row(_mat, 0 BLAZE_CHECK_DEBUG), ~dv);
        for(size_t i = 1; i < nr; newcost += l1Dist(row(_mat, i++ BLAZE_CHECK_DEBUG), ~dv));
        if(std::abs(newcost - prevcost) <= eps) break;
        prevcost = newcost;
        std::fprintf(stderr, "Cost at iteration %zu: %g\n", iternum, newcost);
    }
    return dv;
}

template<typename MT, bool SO, typename VT, bool TF>
void l1_unweighted_median(const blz::DenseMatrix<MT, SO> &data, blz::DenseVector<VT, TF> &ret, bool approx_med=true) {
    assert((~ret).size() == (~data).columns());
    auto &rr(~ret);
    const auto &dr(~data);
    const bool odd = dr.rows() % 2;
    const size_t hlf = dr.rows() / 2;
    const blaze::DynamicVector<uint32_t> indices = generate(dr.rows(), [](auto x){return x;});
    if(approx_med) {
        using acc_tag = boost::accumulators::stats<boost::accumulators::tag::median(with_p_square_quantile)>;
        using FT = ElementType_t<MT>;
        for(size_t i = 0; i < dr.columns(); ++i) {
            boost::accumulators::accumulator_set<FT, acc_tag> acc;
            for(auto v: column(dr, i)) acc(v);
            (~ret)[i] = boost::accumulators::median(acc);
        }
    } else {
        for(size_t i = 0; i < dr.columns(); ++i) {
            blaze::DynamicVector<ElementType_t<MT>, blaze::columnVector> tmpind = column(data, i); // Should do fast copying.
            shared::sort(tmpind.begin(), tmpind.end());
            rr[i] = odd ? tmpind[hlf]: ElementType_t<MT>(.5) * (tmpind[hlf] + tmpind[hlf + 1]);
        }
    }
}



template<typename MT, bool SO, typename VT2, bool TF2, typename FT=CommonType_t<ElementType_t<MT>, ElementType_t<VT2>>>
static inline void weighted_median(const blz::Matrix<MT, SO> &data, blz::DenseVector<VT2, TF2> &ret, const FT *weights, bool approx_med=true) {
    assert(weights);
    const size_t nc = (~data).columns();
    if((~ret).size() != nc) {
        (~ret).resize(nc);
    }
    if(approx_med) {
        //OMP_PFOR
        for(size_t i = 0; i < nc; ++i) {
            auto &mat = ~data;
            using acc_tag = boost::accumulators::stats<boost::accumulators::tag::weighted_median>;
            boost::accumulators::accumulator_set<FT, acc_tag, FT> acc;
            auto col = column(mat, i);
            for(size_t j = 0; j < col.size(); ++j) {
                acc(col[j], weight = weights[j]);
            }
            (~ret)[i] = boost::accumulators::median(acc);
        }
    } else {
        throw std::runtime_error("Not implemented yet; this process is expensive and hard to make cache-friendly.");
    }
#if 0
    blz::DynamicVector<FT> midpoints = blz::sum<blz::columnwise>(~data) * .5;
    shared::flat_hash_set<IT> indices;
    indices.reserve(nc);
    for(size_t i = 0; i < nc;indices.insert(i++));
#endif
}


template<typename MT, bool SO, typename VT, bool TF, typename VT3=blz::CommonType_t<ElementType_t<MT>, ElementType_t<VT>>>
void l1_median(const blz::DenseMatrix<MT, SO> &data, blz::DenseVector<VT, TF> &ret, const VT3 *weights=static_cast<VT3 *>(nullptr), bool approx_med=true) {
    if(weights)
        weighted_median(data, ret, weights, approx_med);
    else
        l1_unweighted_median(data, ret, approx_med);
}


} // namespace coresets
} // namespace fgc
#endif /* FGC_KMEDIAN_H__ */
