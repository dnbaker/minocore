#pragma once
#ifndef FGC_KMEDIAN_H__
#define FGC_KMEDIAN_H__
#include "minicore/optim/kmeans.h"
#include "minicore/util/csc.h"
#include <algorithm>

namespace minicore {
namespace coresets {
using namespace blz;

template<typename VT, bool TF, typename FT, typename IT>
static INLINE void __assign(blaze::DenseVector<VT, TF> &vec, IT ind, FT val) {
    (*vec)[ind] = val;
}
#if 1
template<typename VT, bool TF, typename FT, typename IT>
static INLINE void __assign(blaze::SparseVector<VT, TF> &vec, IT ind, FT val) {
    static_assert(std::is_integral_v<IT>, "Sanity1");
    static_assert(std::is_arithmetic_v<FT>, "Sanity2");
    auto &rr = *vec;
    if(val != FT(0.)) {
        if(rr.capacity() <= rr.nonZeros() + 1)
            rr.reserve(std::max((rr.nonZeros() + 1) << 1, size_t(4)));
        rr.append(ind, val);
    }
}
#else
template<typename VT, bool TF, typename FT, typename IT>
static INLINE void __assign(blaze::SparseVector<VT, TF> &vec, IT ind, FT val) {
    (*vec).set(ind, val);
}
#endif


namespace detail {

struct IndexCmp {
    template<typename T>
    bool operator()(const T x, const T y) const {return x->index() > y->index();}
    template<typename T, typename IT>
    bool operator()(const std::pair<T, IT> x, const std::pair<T, IT> y) const {
        return this->operator()(x.first, y.first);
    }
};

template<typename CI, typename IT=uint32_t>
struct IndexPQ: public std::priority_queue<std::pair<CI, IT>, std::vector<std::pair<CI, IT>>, IndexCmp> {
    IndexPQ(size_t nelem) {
        this->c.reserve(nelem);
    }
    auto &getc() {return this->c;}
    const auto &getc() const {return this->c;}
    auto getsorted() const {
        auto tmp = getc();
        std::fprintf(stderr, "pq size: %zu\n", tmp.size());
        std::sort(tmp.begin(), tmp.end(), this->comp);
        return tmp;
    }
};

} // namespace detail

template<typename MT, bool SO, typename VT, bool TF>
void sparse_l1_unweighted_median(const blz::SparseMatrix<MT, SO> &data, blz::Vector<VT, TF> &ret) {
    if((*data).rows() == 1) {
        *ret = row(*data, 0);
        return;
    }
    using FT = blaze::ElementType_t<MT>;
    auto &ctr = *ret;
    if constexpr(blaze::IsSparseVector_v<VT>) {
        ctr.reset();
    }
    using CI = typename MT::ConstIterator;
    const size_t nd = (*data).columns(), nr = (*data).rows(), hlf = nr / 2, odd = nr & 1;
    detail::IndexPQ<CI, uint32_t> pq(nr);
    std::unique_ptr<CI[]> ve(new CI[nr]);
    for(unsigned i = 0; i < nr; ++i) {
        auto r(row(*data, i));
        pq.push(std::pair<CI, uint32_t>(r.begin(), i));
        ve[i] = r.end();
    }
    assert(pq.size() == (*data).rows());
    uint32_t cid = 0;
    std::vector<FT> vals;
    assert(pq.empty() || pq.top().first->index() == std::min_element(pq.getc().begin(), pq.getc().end(), [](auto x, auto y) {return x.first->index() < y.first->index();})->first->index());
    // Setting all to 0 lets us simply skip elements with the wrong number of nonzeros.
    while(pq.size()) {
        //std::fprintf(stderr, "Top index: %zu\n", pq.top().first->index());
        if constexpr(!blaze::IsSparseVector_v<VT>) {
            while(cid < pq.top().first->index())
                __assign(ctr, cid++, 0);
            if(unlikely(cid > pq.top().first->index())) {
                std::fprintf(stderr, "cid: %u. top index: %zu\n", cid, pq.top().first->index());
                std::exit(1);
#if 0
                auto pqs = pq.getsorted();
                for(const auto v: pqs) std::fprintf(stderr, "%zu:%g\n", v.first->index(), v.first->value());
                std::exit(1);
#endif
            }
        } else cid = pq.top().first->index();
        while(pq.top().first->index() == cid) {
            auto pair = pq.top();
            pq.pop();
            vals.push_back(pair.first->value());
            if(++pair.first != ve[pair.second]) {
                pq.push(pair);
            } else if(pq.empty()) break;
        }
        const size_t vsz = vals.size();
        FT val;
        if(vsz < hlf) {
            val = 0.;
        } else {
            shared::sort(vals.data(), vals.data() + vals.size());
            const size_t idx = vals.size() - nr / 2 - 1;
            val = odd ? vals[idx]: (vals[idx] + vals[idx + 1]) * FT(.5);
        }
        __assign(ctr, cid, val);
        ++cid;
        vals.clear();
    }
    if constexpr(blaze::IsDenseVector_v<VT>) {
        while(cid < nd) ctr[cid++] = 0;
    }
}

template<typename MT, bool SO, typename VT, bool TF>
void l1_unweighted_median(const blz::Matrix<MT, SO> &data, blz::Vector<VT, TF> &ret) {
#if 0
    if constexpr(blz::IsSparseMatrix_v<MT>) {
        sparse_l1_unweighted_median(*data, ret);
        return;
    }
#endif
    //std::fprintf(stderr, "%s unweighted l1 median. data shape: %zu/%zu. Return shape: %zu\n", blaze::IsDenseMatrix_v<MT> ? "Dense": "Sparse", (*data).rows(), (*data).columns(), (*ret).size());
    assert((*ret).size() == (*data).columns());
    auto &rr(*ret);
    const auto &dr(*data);
    const bool odd = dr.rows() % 2;
    const size_t hlf = dr.rows() / 2;
    blaze::DynamicVector<ElementType_t<MT>, blaze::columnVector> dv;
    if constexpr(blaze::IsSparseVector_v<VT>) {
        (*ret).reset();
    }
    for(size_t i = 0; i < dr.columns(); ++i) {
        dv = column(dr, i);
        // Should do fast copying.
        shared::sort(dv.begin(), dv.end());
        auto val = odd ? dv[hlf]: ElementType_t<MT>(.5) * (dv[hlf - 1] + dv[hlf]);
#if 0
        std::fprintf(stderr, "val %g at %zu\n", val, i);
#endif
        __assign(rr, i,  val);
        assert(rr[i] == val || !std::fprintf(stderr, "rr[i] %g vs %g\n", double(rr[i]), val));
    }
}


template<typename MT, bool SO, typename VT, bool TF, typename Rows>
void l1_unweighted_median(const blz::Matrix<MT, SO> &_data, const Rows &rs, blz::Vector<VT, TF> &ret) {
    assert((*ret).size() == (*_data).columns());
    auto &rr(*ret);
    const auto &dr(*_data);
    const bool odd = rs.size() % 2;
    const size_t hlf = rs.size() / 2;
    const size_t nc = dr.columns();
    blaze::DynamicMatrix<ElementType_t<MT>, SO> tmpind;
    if constexpr(blaze::IsSparseVector_v<VT>) {
        (*ret).reset();
    }
    size_t i;
    for(i = 0; i < nc;) {
        const unsigned nr = std::min(size_t(8), nc - i);
        tmpind = trans(blaze::submatrix(blaze::rows(dr, rs.data(), rs.size()), 0, i * nr, rs.size(), nr));
        for(unsigned j = 0; j < nr; ++j) {
            auto r(blaze::row(tmpind, j));
            shared::sort(r.begin(), r.end());
            __assign(rr, i + j,  odd ? r[hlf]: ElementType_t<MT>(0.5) * (r[hlf - 1] + r[hlf]));
        }
        i += nr;
    }
}



#if 0
template<typename DataT, typename IndicesT, typename IndPtrT, typename VT2, bool TF2, typename IT=uint32_t, typename WT>
static inline void l1_median(const util::CSparseMatrix<DataT, IndicesT, IndPtrT> &data, blz::Vector<VT2, TF2> &ret, const IT *indices=(const IT *)nullptr, size_t nasn=0, const WT *weights=static_cast<WT *>(nullptr)) {
    const size_t nc = data.columns();
    if((*ret).size() != nc) {
        (*ret).resize(nc);
    }
    (*ret).reset();
    if(unlikely((*data).columns() > ((uint64_t(1) << (sizeof(IT) * CHAR_BIT)) - 1)))
        throw std::runtime_error("Use a different index type, there are more features than fit in IT");
    const size_t npoints = indices ? nasn: (*data).rows();
    if(!npoints) throw std::invalid_argument("Can't take the median of no points");
    using FT = blaze::CommonType_t<DataT, blz::ElementType_t<VT2>, std::decay_t<blz::ElementType_t<WT>>>;
    std::vector<std::vector<std::pair<DataT, IT>>> pairs(nc); // One list of pairs per column
    for(auto &p: pairs) p.reserve(npoints);
#ifdef _OPENMP
    int nt;
    #pragma omp parallel
    {
        nt = omp_get_num_threads();
    }
    auto mutexes = std::make_unique<std::mutex[]>(nt);
    OMP_PFOR
#endif
    for(size_t i = 0; i < npoints; ++i) {
        size_t j = 0;
        const auto rowind = indices ? size_t(indices[i]): i;
        const std::pair<DataT, IT> empty(0, rowind);
        auto crow = row(data, rowind);
        auto cbeg = crow.begin(), cend = crow.end();
        for(;cbeg != cend;++cbeg, ++j) {
            const auto ind = cbeg->index();
            while(j < ind) {
                OMP_ONLY(std::lock_guard<std::mutex> lock(mutexes[j]);)
                pairs[j++].push_back(empty);
            }
            OMP_ONLY(std::lock_guard<std::mutex> lock(mutexes[ind]);)
            pairs[ind].push_back(std::pair<DataT, IT>(cbeg->value(), rowind));
            ++cbeg;
        }
        while(j < nc) {
            OMP_ONLY(std::lock_guard<std::mutex> lock(mutexes[j]);)
            pairs[j++].push_back(empty);
        }
    }
    // First, compute sorted pairs
    // Then find median for each column
    OMP_PFOR_DYN
    for(size_t i = 0; i < nc; ++i) {
        auto &cpairs = pairs[i];
        shared::sort(cpairs.begin(), cpairs.end());
        FT wsum = 0., maxw = -std::numeric_limits<FT>::max();
        IT maxind = -0;
        for(size_t j = 0; j < npoints; ++j) {
           double neww = 1.;
           if(weights) neww = (*weights)[cpairs[j].second];
           wsum += neww;
           if(neww > maxw) maxw = neww, maxind = j;
        }
        if(maxw > wsum * .5) {
            // Return the value of the tuple with maximum weight
            __assign(*ret, i, cpairs[maxind].first);
            continue;
        }
        FT mid = wsum * .5;
        auto it = std::lower_bound(cpairs.begin(), cpairs.end(), mid,
             [](std::pair<DataT, IT> x, FT y)
        {
            return x.first < y;
        });
        OMP_CRITICAL {
            __assign(*ret, i, it->first == mid ? FT(.5 * (it->first + it[1].first)): FT(it[1].first));
        }
    }
}

#endif
template<typename MT, bool SO, typename VT2, bool TF2, typename IT=uint32_t, typename WT>
static inline void weighted_median(const blz::Matrix<MT, SO> &data, blz::Vector<VT2, TF2> &ret, const WT &weights) {
    const size_t nc = (*data).columns();
    if((*ret).size() != nc) {
        (*ret).resize(nc);
    }
    if constexpr(blaze::IsSparseVector_v<VT2>) {
        (*ret).reset();
    }
    if(unlikely((*data).columns() > ((uint64_t(1) << (sizeof(IT) * CHAR_BIT)) - 1)))
        throw std::runtime_error("Use a different index type, there are more features than fit in IT");
    const size_t nr = (*data).rows();
    auto pairs = std::make_unique<std::pair<ElementType_t<MT>, IT>[]>(nr);
    using FT = blaze::CommonType_t<blz::ElementType_t<MT>, blz::ElementType_t<VT2>, std::decay_t<decltype(weights[0])>>;
    for(size_t i = 0; i < nc; ++i) {
        auto col = column(*data, i);
        for(size_t j = 0; j < nr; ++j)
            pairs[j] = {col[j], j};
        shared::sort(pairs.get(), pairs.get() + nr);
        FT wsum = 0., maxw = -std::numeric_limits<FT>::max();
        IT maxind = -0;
        for(size_t j = 0; j < nr; ++j) {
           auto neww = weights[pairs[j].second];
           if(neww > maxw) maxw = neww, maxind = j;
        }
        if(maxw > wsum * .5) {
            // Return the value of the tuple with maximum weight
            __assign(*ret, i, pairs[maxind].first);
            continue;
        }
        FT mid = wsum * .5;
        auto it = std::lower_bound(pairs.get(), pairs.get() + nr, mid,
             [](std::pair<ElementType_t<MT>, IT> x, FT y)
        {
            return x.first < y;
        });
        (*ret)[i] = it->first == mid ? FT(.5 * (it->first + it[1].first)): FT(it[1].first);
    }
}


template<typename MT, bool SO, typename VT, bool TF, typename VT3=blz::CommonType_t<ElementType_t<MT>, ElementType_t<VT>>, typename=std::enable_if_t<std::is_arithmetic_v<VT3>>>
void l1_median(const blz::Matrix<MT, SO> &data, blz::Vector<VT, TF> &ret, const VT3 *weights=static_cast<VT3 *>(nullptr)) {
    if(weights)
        weighted_median(data, ret, weights);
    else
        l1_unweighted_median(data, ret);
}

template<typename MT, bool SO, typename VT, bool TF, typename VT3, typename=std::enable_if_t<!std::is_arithmetic_v<VT3> && !std::is_pointer_v<VT3>>>
void l1_median(const blz::Matrix<MT, SO> &data, blz::Vector<VT, TF> &ret, const VT3 &weights) {
    weighted_median(data, ret, weights);
}

template<typename MT, bool SO, typename VT, bool TF, typename Rows, typename VT3=blz::CommonType_t<ElementType_t<MT>, ElementType_t<VT>>, typename=std::enable_if_t<blaze::IsRows_v<Rows>>>
void l1_median(const blz::Matrix<MT, SO> &data, blz::Vector<VT, TF> &ret, const Rows &rows, const VT3 *weights=static_cast<VT3 *>(nullptr)) {
    if(weights) {
        auto dr(blaze::rows(data, rows.data(), rows.size()));
        const blz::CustomVector<VT3, blaze::unaligned, blaze::unpadded> cv((VT3 *)weights, (*data).rows());
        blz::DynamicVector<VT3> selected_weights(blaze::elements(cv, rows.data(), rows.size()));
        weighted_median(dr, ret, selected_weights.data());
    } else l1_unweighted_median(data, rows, ret);
}

template<typename MT, bool SO, typename VT, bool TF, typename IT=uint64_t, typename WeightT=blz::DV<double>>
void l1_median(const blaze::Matrix<MT, SO> &data, blz::Vector<VT, TF> &ret, IT *asp, size_t nasn=0, const WeightT *weights=static_cast<WeightT *>(nullptr)) {
    if(!asp) {
        if(weights) {
            weighted_median(data, ret, *weights);
        } else {
            l1_unweighted_median(data, ret);
        }
    } else {
        if(weights) {
            weighted_median(rows(*data, asp, nasn), ret, *weights);
        } else {
            l1_unweighted_median(rows(*data, asp, nasn), ret);
        }
    }
}

template<typename MT, bool SO, typename VT, bool TF, typename IT=uint64_t, typename WeightT=blz::DV<double>, typename RSums>
void tvd_median(const blaze::Matrix<MT, SO> &data, blz::Vector<VT, TF> &ret, IT *asp, size_t nasn=0, const WeightT *weights=static_cast<WeightT *>(nullptr), const RSums &rsums=RSums()) {
    return l1_median(*data % blaze::expand(1. / rsums, (*data).columns()), ret, asp, nasn, weights);
}


} // namespace coresets
} // namespace minicore
#endif /* FGC_KMEDIAN_H__ */
