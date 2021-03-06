#ifndef CSC_H__
#define CSC_H__
#include "./shared.h"
#include "./timer.h"
#include "./blaze_adaptor.h"
#include "./merge.h"
#include "./io.h"
#include "./exception.h"
#include "thirdparty/mio.hpp"
#include <fstream>


namespace minicore {

namespace util {

template<typename T>
INLINE T abs_diff(T x, T y) {
    if constexpr(std::is_unsigned_v<T>) {
        return std::max(x, y) - std::min(x, y);
    } else {
        return std::abs(x - y);
    }
}

template<typename T, typename T2>
INLINE auto abs_diff(T x, T2 y) {
    using CT = std::common_type_t<T, T2>;
    return abs_diff(CT(x), CT(y));
}

    static constexpr size_t MINICORE_UTIL_ALN =
#ifdef __AVX512F__
        sizeof(__m512) / sizeof(char);
#elif __AVX2__ || __AVX__
        sizeof(__m256) / sizeof(char);
#elif __SSE4_1__ || __SSE2__
        sizeof(__m128) / sizeof(char);
#else
        1;
#endif

static inline bool is_file(std::string path) noexcept {
    return ::access(path.data(), F_OK) != -1;
}

template<typename DataType>
struct ConstSViewMul: public std::pair<const DataType*, size_t> {
    // Same as ConstSView, but multiplies the value
    // by a constant during dereferencing
    const DataType mul_;
    ConstSViewMul(DataType mul): mul_(mul) {}
    size_t index() const {return this->second;}
    size_t &index() {return this->second;}
    const DataType value() const {return *this->first * mul_;}
};

template<typename IndPtrType=uint64_t, typename IndicesType=uint64_t, typename DataType=uint32_t>
struct CSCMatrixView {
    using ElementType = DataType;
    IndPtrType *const indptr_;
    IndicesType *const indices_;
    DataType *const data_;
    const uint64_t nnz_;
    const uint32_t nf_, n_;
    static_assert(std::is_integral_v<IndPtrType>, "IndPtr must be integral");
    static_assert(std::is_integral_v<IndicesType>, "Indices must be integral");
    static_assert(std::is_arithmetic_v<IndicesType>, "Data must be arithmetic");
    CSCMatrixView(IndPtrType *indptr, IndicesType *indices, DataType *data,
                  uint64_t nnz, uint32_t nfeat, uint32_t nitems):
        indptr_(indptr),
        indices_(indices),
        data_(data),
        nnz_(nnz),
        nf_(nfeat), n_(nitems)
    {
    }
    struct Column {
        const CSCMatrixView &mat_;
        size_t start_;
        size_t stop_;

        Column(const CSCMatrixView &mat, size_t start, size_t stop)
            : mat_(mat), start_(start), stop_(stop)
        {
        }
        size_t nnz() const {return stop_ - start_;}
        size_t size() const {return mat_.columns();}
        template<bool is_const>
        struct ColumnIteratorBase {
            struct ViewType {
                using VT = DataType;
                ViewType(const ColumnIteratorBase &it): it_(it) {}
                const ColumnIteratorBase &it_;
                INLINE size_t index() const {return it_.col_.indices_[it_.index_];}
                INLINE std::conditional_t<is_const, std::add_const_t<VT>, VT> &value()  {return it_.col_.data_[it_.index_];}
                INLINE std::add_const_t<VT> &value() const {return it_.col_.data_[it_.index_];}
            };
            using ColType = std::conditional_t<is_const, const Column, Column>;
            using ViewedType = std::conditional_t<is_const, const DataType, DataType>;
            using difference_type = std::ptrdiff_t;
            using value_type = ViewedType;
            using reference = ViewedType &;
            using pointer = ViewedType *;
            using iterator_category = std::random_access_iterator_tag;
            ColType &col_;
            size_t index_;
            private:
            mutable ViewType data_;
            public:

            template<bool oconst>
            bool operator==(const ColumnIteratorBase<oconst> &o) const {
                return index_ == o.index_;
            }
            template<bool oconst>
            bool operator!=(const ColumnIteratorBase<oconst> &o) const {
                return index_ != o.index_;
            }
            template<bool oconst>
            bool operator<(const ColumnIteratorBase<oconst> &o) const {
                return index_ < o.index_;
            }
            template<bool oconst>
            bool operator>(const ColumnIteratorBase<oconst> &o) const {
                return index_ > o.index_;
            }
            template<bool oconst>
            bool operator<=(const ColumnIteratorBase<oconst> &o) const {
                return index_ <= o.index_;
            }
            template<bool oconst>
            bool operator>=(const ColumnIteratorBase<oconst> &o) const {
                return index_ >= o.index_;
            }
            template<bool oconst>
            difference_type operator-(const ColumnIteratorBase<oconst> &o) const {
                return this->index_ - o.index_;
            }
            ColumnIteratorBase<is_const> &operator++() {
                ++index_;
                return *this;
            }
            ColumnIteratorBase<is_const> operator++(int) {
                ColumnIteratorBase ret(col_, index_);
                ++index_;
                return ret;
            }
            const auto &operator*() const {
                return data_;
            }
            auto &operator*() {
                return data_;
            }
            ViewType *operator->() {
                return &data_;
            }
            const ViewType *operator->() const {
                return &data_;
            }
            ColumnIteratorBase(ColType &col, size_t ind): col_(col), index_(ind) {
            }
        };
        using ColumnIterator = ColumnIteratorBase<false>;
        using ConstColumnIterator = ColumnIteratorBase<true>;
        ColumnIterator begin() {return ColumnIterator(*this, start_);}
        ColumnIterator end()   {return ColumnIterator(*this, stop_);}
        ConstColumnIterator begin() const {return ConstColumnIterator(*this, start_);}
        ConstColumnIterator end()   const {return ConstColumnIterator(*this, stop_);}
    };
    auto column(size_t i) const {
        return Column(*this, indptr_[i], indptr_[i + 1]);
    }
    size_t nnz() const {return nnz_;}
    size_t rows() const {return n_;}
    size_t columns() const {return nf_;}
};

template<typename T>
struct is_csc_view: public std::false_type {};
template<typename IndPtrType, typename IndicesType, typename DataType>
struct is_csc_view<CSCMatrixView<IndPtrType, IndicesType, DataType>>: public std::true_type {};

template<typename T>
static constexpr bool is_csc_view_v = is_csc_view<T>::value;



template<typename VT, typename IT>
struct CSparseVector {
    using ElementType  = VT;
    static constexpr const bool transposeFlag = false;
    VT *data_;
    IT *indices_;
    size_t n_, dim_;

    //std::unique_ptr<VT> sum_;

    CSparseVector(VT *data, IT *indices, size_t n, size_t dim): data_(data), indices_(indices), n_(n), dim_(dim)
    {
    }
    size_t nnz() const {return n_;}
    size_t size() const {return dim_;}
    using NCVT = std::remove_const_t<VT>;
    NCVT sum() const {
#if 0
        std::remove_const_t<VT> ret;
        auto di = reinterpret_cast<uint64_t>(data_);
        if(di % MINICORE_UTIL_ALN && n_ > (MINICORE_UTIL_ALN / sizeof(VT)) && di % sizeof(VT) == 0) {
            // Break into short unaligned + long aligned sum
            const auto offset = (MINICORE_UTIL_ALN - (di % MINICORE_UTIL_ALN));
            const auto offset_n = offset / sizeof(VT);
            assert(reinterpret_cast<uint64_t>((NCVT *)data_ + offset_n) % MINICORE_UTIL_ALN == 0);
            ret = blz::sum(blz::make_cv((NCVT *)data_, offset_n))
                + blz::sum(blz::make_cv<blz::aligned>((NCVT *)data_ + offset_n, n_ - offset_n));
        } else ret = blz::sum(blz::make_cv<blz::aligned>((NCVT *)data_, n_));
        return ret ;
#else
        //std::fprintf(stderr, "Calling sum. value: %g\n", double(blz::sum(blz::make_cv((NCVT *)data_, n_))));
        return blz::sum(blz::make_cv((NCVT *)data_, n_));
#endif
    }
    using DataType = VT;
    template<bool is_const>
    struct CSparseVectorIteratorBase {
        using ColType = std::conditional_t<is_const, std::add_const_t<CSparseVector>, CSparseVector>;
        using ViewedType = std::conditional_t<is_const, std::add_const_t<DataType>, DataType>;
        using difference_type = std::ptrdiff_t;
        using value_type = ViewedType;
        using reference = ViewedType &;
        using pointer = ViewedType *;
        using iterator_category = std::random_access_iterator_tag;
        ColType &col_;
        size_t index_;

        INLINE size_t index() const {return col_.indices_[index_];}
        INLINE std::conditional_t<is_const, std::add_const_t<VT>, VT> &value()  {return col_.data_[index_];}
        INLINE std::add_const_t<VT> &value() const {return col_.data_[index_];}
        using ViewType = CSparseVectorIteratorBase<is_const>;
        template<bool oconst>
        bool operator==(const CSparseVectorIteratorBase<oconst> &o) const {
            return index_ == o.index_;
        }
        template<bool oconst>
        bool operator!=(const CSparseVectorIteratorBase<oconst> &o) const {
            return index_ != o.index_;
        }
        template<bool oconst>
        bool operator<(const CSparseVectorIteratorBase<oconst> &o) const {
            return index_ < o.index_;
        }
        template<bool oconst>
        bool operator>(const CSparseVectorIteratorBase<oconst> &o) const {
            return index_ > o.index_;
        }
        template<bool oconst>
        bool operator<=(const CSparseVectorIteratorBase<oconst> &o) const {
            return index_ <= o.index_;
        }
        template<bool oconst>
        bool operator>=(const CSparseVectorIteratorBase<oconst> &o) const {
            return index_ >= o.index_;
        }
        template<bool oconst>
        difference_type operator-(const CSparseVectorIteratorBase<oconst> &o) const {
            return this->index_ - o.index_;
        }
        CSparseVectorIteratorBase<is_const> &operator++() {
            //std::fprintf(stderr, "before incrementing: indptr: %zu. index: %zu. value: %g\n", index_, size_t(col_.indices_[index_]), col_.data_[index_]);
            ++index_;
            //std::fprintf(stderr, "after incrementing: indptr: %zu. index: %zu. value: %g\n", index_, size_t(col_.indices_[index_]), col_.data_[index_]);
            return *this;
        }
#if 0
        CSparseVectorIteratorBase<is_const> operator++(int) {
            CSparseVectorIteratorBase ret(col_, index_);
            ++index_;
            return ret;
        }
#endif
        const ViewType &operator*() const {
            return *this;
        }
        ViewType &operator*() {
            return *this;
        }
        const ViewType *operator->() const {
            //std::fprintf(stderr, "Calling const -> operator\n");
            return this;
        }
        CSparseVectorIteratorBase(ColType &col, size_t ind): col_(col), index_(ind) {
        }
    };
    double l2Norm() const {
        double ret;
        auto di = reinterpret_cast<uint64_t>(data_);
        if(di % MINICORE_UTIL_ALN) {
            if(n_ > (MINICORE_UTIL_ALN / sizeof(VT)) && di % sizeof(VT) == 0) {
                // Break into short unaligned + long aligned sum
                const auto offset = (MINICORE_UTIL_ALN - (di % MINICORE_UTIL_ALN));
                const auto offset_n = offset / sizeof(VT);
                assert(reinterpret_cast<uint64_t>((NCVT *)data_ + offset_n) % MINICORE_UTIL_ALN == 0);
                ret = sqrNorm(blz::make_cv((NCVT *)data_, offset_n))
                    + sqrNorm(blz::make_cv<blz::aligned>((NCVT *)data_ + offset_n, n_ - offset_n));
            } else {
                ret = sqrNorm(blz::make_cv((NCVT *)data_, n_));
            }
        } else ret = sqrNorm(blz::make_cv<blz::aligned>((NCVT *)data_, n_));
        return std::sqrt(ret);
    }
    using ConstCSparseIterator = CSparseVectorIteratorBase<true>;
    using CSparseIterator = CSparseVectorIteratorBase<false>;
    CSparseIterator begin() {return CSparseIterator(*this, 0);}
    CSparseIterator end()   {return CSparseIterator(*this, n_);}
    ConstCSparseIterator begin() const {return ConstCSparseIterator(*this, 0);}
    ConstCSparseIterator end()   const {return ConstCSparseIterator(*this, n_);}
};

template<typename VT, typename IT, typename IPtrT>
struct CSparseMatrix;

template<typename VT, typename IT>
std::ostream& operator<< (std::ostream& out, const CSparseVector<VT, IT> & item);
template<typename VT, typename IT, typename IPtrT>
std::ostream& operator<< (std::ostream& out, const CSparseMatrix<VT, IT, IPtrT> & item);



template<typename VT, typename IT>
struct ProdCSparseVector {
    VT *data_;
    IT *indices_;
    const size_t n_;
    const size_t dim_;
    const double prod_;

    ProdCSparseVector(const CSparseVector<VT, IT> &ovec, double prod): data_(ovec.data_), indices_(ovec.indices_), n_(ovec.n_), dim_(ovec.dim_), prod_(prod) {
    }
    ProdCSparseVector(const ProdCSparseVector<VT, IT> &ovec, double prod): data_(ovec.data_), indices_(ovec.indices_), n_(ovec.n_), dim_(ovec.dim_), prod_(prod * ovec.prod_) {
    }
    size_t nnz() const {return n_;}
    size_t size() const {return dim_;}
    using NCVT = std::remove_const_t<VT>;
    double sum() const {
#if 0
        double ret;
        auto di = reinterpret_cast<uint64_t>(data_);
        if(di % MINICORE_UTIL_ALN && n_ > (MINICORE_UTIL_ALN / sizeof(VT)) && di % sizeof(VT) == 0) {
            // Break into short unaligned + long aligned sum
            const auto offset = (MINICORE_UTIL_ALN - (di % MINICORE_UTIL_ALN));
            const auto offset_n = offset / sizeof(VT);
            assert(reinterpret_cast<uint64_t>((NCVT *)data_ + offset_n) % MINICORE_UTIL_ALN == 0);
            ret = blz::sum(blz::make_cv((NCVT *)data_, offset_n))
                + blz::sum(blz::make_cv<blz::aligned>((NCVT *)data_ + offset_n, n_ - offset_n));
        } else ret = blz::sum(blz::make_cv<blz::aligned>((NCVT *)data_, n_));
        return ret * prod_;
#else
        std::fprintf(stderr, "Calling sum. value: %g * %g = %g\n", double(blz::sum(blz::make_cv((NCVT *)data_, n_))), prod_, blz::sum(blz::make_cv((NCVT *)data_, n_)) * prod_);
        return blz::sum(blz::make_cv((NCVT *)data_, n_)) * prod_;
#endif
    }
    using ConstCView = ConstSViewMul<VT>;
    using DataType = VT;
    struct ProdCSparseVectorIteratorBase {
        using ColType = std::add_const_t<ProdCSparseVector>;
        using ViewedType = std::add_const_t<DataType>;
        using difference_type = std::ptrdiff_t;
        using value_type = ViewedType;
        using reference = ViewedType &;
        using pointer = ViewedType *;
        using iterator_category = std::random_access_iterator_tag;
        using ViewType = ProdCSparseVectorIteratorBase;
        ColType &col_;
        size_t index_;
        size_t index() const {return col_.indices_[index_];}
        double value() const {
            return col_.data_[index_] * col_.prod_;
        }
        public:

        bool operator==(const ProdCSparseVectorIteratorBase &o) const {
            return index_ == o.index_;
        }
        bool operator!=(const ProdCSparseVectorIteratorBase &o) const {
            return index_ != o.index_;
        }
        bool operator<(const ProdCSparseVectorIteratorBase &o) const {
            return index_ < o.index_;
        }
        bool operator>(const ProdCSparseVectorIteratorBase &o) const {
            return index_ > o.index_;
        }
        bool operator<=(const ProdCSparseVectorIteratorBase &o) const {
            return index_ <= o.index_;
        }
        bool operator>=(const ProdCSparseVectorIteratorBase &o) const {
            return index_ >= o.index_;
        }
        difference_type operator-(const ProdCSparseVectorIteratorBase &o) const {
            return this->index_ - o.index_;
        }
        ProdCSparseVectorIteratorBase &operator++() {
            ++index_;
            return *this;
        }
        ProdCSparseVectorIteratorBase operator++(int) {
            ProdCSparseVectorIteratorBase ret(col_, index_);
            ++index_;
            return ret;
        }
        const ViewType &operator*() const {
            return *this;
        }
        ViewType &operator*() {
            return *this;
        }
        ViewType *operator->() {
            return this;
        }
        const ViewType *operator->() const {
            return this;
        }
        ProdCSparseVectorIteratorBase(ColType &col, size_t ind): col_(col), index_(ind) {
        }
    };
    double l2Norm() const {
#if 0
        double ret;
        auto di = reinterpret_cast<uint64_t>(data_);
        if(di % MINICORE_UTIL_ALN) {
            if(n_ > (MINICORE_UTIL_ALN / sizeof(VT)) && di % sizeof(VT) == 0) {
                // Break into short unaligned + long aligned sum
                const auto offset = (MINICORE_UTIL_ALN - (di % MINICORE_UTIL_ALN));
                const auto offset_n = offset / sizeof(VT);
                assert(reinterpret_cast<uint64_t>((NCVT *)data_ + offset_n) % MINICORE_UTIL_ALN == 0);
                ret = sqrNorm(blz::make_cv((NCVT *)data_, offset_n))
                    + sqrNorm(blz::make_cv<blz::aligned>((NCVT *)data_ + offset_n, n_ - offset_n));
            } else {
                ret = sqrNorm(blz::make_cv((NCVT *)data_, n_));
            }
        } else ret = sqrNorm(blz::make_cv<blz::aligned>((NCVT *)data_, n_));
#else
        double ret = blz::sqrNorm(blz::make_cv((NCVT *)data_, n_));
#endif
        return prod_ * std::sqrt(ret);
    }
    using CSparseIterator = ProdCSparseVectorIteratorBase;
    CSparseIterator begin() {return CSparseIterator(*this, 0);}
    CSparseIterator end()   {return CSparseIterator(*this, n_);}
    CSparseIterator begin() const {return CSparseIterator(*this, 0);}
    CSparseIterator end()   const {return CSparseIterator(*this, n_);}
};

template<typename VT, typename IT>
inline double l2Norm(const CSparseVector<VT, IT> &x) {
    return x.l2Norm();
}
template<typename VT, typename IT>
inline double l2Norm(const ProdCSparseVector<VT, IT> &x) {
    return x.l2Norm();
}

template<typename VT, typename IT, typename OVT>
ProdCSparseVector<VT, IT> operator*(const CSparseVector<VT, IT> &lhs, OVT rhs) {
    return ProdCSparseVector<VT, IT>(lhs, rhs);
}

template<typename VT, typename IT, typename OVT>
ProdCSparseVector<VT, IT> operator/(const CSparseVector<VT, IT> &lhs, OVT rhs) {
    return lhs * double(1. / rhs);
}
template<typename VT, typename IT, typename OVT>
ProdCSparseVector<VT, IT> operator/(const ProdCSparseVector<VT, IT> &lhs, OVT rhs) {
    return ProdCSparseVector<VT, IT>(lhs, 1. / rhs);
}

template<typename VT1, typename IT1, typename VT2, bool TF>
auto l2Dist(const ProdCSparseVector<VT1, IT1> &lhs, const blaze::DenseVector<VT2, TF> &rhs) {
    if(lhs.size() != (*rhs).size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    auto &rr = *rhs;
    using CT = std::common_type_t<VT1, blaze::ElementType_t<VT2>>;
    CT ret = 0;
    size_t si = 0, di = 0;
    for(;si != lhs.n_ || di != (*rhs).size(); ++di) {
        if(si == lhs.n_) {
            ret += sum(subvector(*rhs, di, (*rhs).size() - di));
            break;
        } else if(di == (*rhs).size()) {
            ret += sum(blz::make_cv(&lhs.data_[si], lhs.n_ - si)) * lhs.prod_;
            break;
        } else {
            ret += sum(abs(subvector(*rhs, di, si - di)));
            di = si;
            ret += abs_diff((*rhs)[di], lhs.data_[si]);
            ++si;
        }
    }
    return ret;
}
template<typename VT1, typename IT1, typename VT2, bool TF>
auto l2Dist(const CSparseVector<VT1, IT1> &lhs, const blaze::DenseVector<VT2, TF> &rhs) {
    if(lhs.size() != (*rhs).size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    using CT = std::common_type_t<VT1, blaze::ElementType_t<VT2>>;
    CT ret = 0;
    size_t si = 0, di = 0;
    for(;si != lhs.n_ || di != (*rhs).size(); ++di) {
        if(si == lhs.n_) {
            ret += sum(subvector(*rhs, di, (*rhs).size() - di));
            break;
        } else if(di == (*rhs).size()) {
            ret += sum(blz::make_cv(&lhs.data_[si], lhs.n_ - si));
            break;
        } else {
            while(di < lhs.indices_[si]) {
                ret += std::abs((*rhs)[di++]);
            }
            ret += abs_diff((*rhs)[di], lhs.data_[si]);
            ++si;
        }
    }
    return ret;
}
template<typename VT1, typename IT1, typename VT2, bool TF>
auto l2Dist(const blaze::DenseVector<VT2, TF> &rhs,
            const CSparseVector<VT1, IT1> &lhs)
{
    return l2Dist(lhs, rhs);
}

template<typename VT1, typename IT1, typename VT2, bool TF>
auto l2Dist(const blaze::DenseVector<VT2, TF> &rhs,
            const ProdCSparseVector<VT1, IT1> &lhs)
{
    return l2Dist(lhs, rhs);
}


template<typename VT1, typename IT1, typename VT2, bool TF>
auto l2Dist(const CSparseVector<VT1, IT1> &lhs, const blaze::SparseVector<VT2, TF> &rhs) {
    if(lhs.size() != (*rhs).size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    auto &rr = *rhs;
    using CT = std::common_type_t<VT1, blaze::ElementType_t<VT2>>;
    CT ret = 0;
    merge::for_each_by_case(lhs.size(), lhs.begin(), lhs.end(), rr.begin(), rr.end(),
                                 [&ret](auto, auto lhv, auto rhv) {
                                    auto v = abs_diff(lhv, rhv); ret += v * v;},
                                 [&ret](auto, auto rhv) {ret += rhv * rhv;},
                                 [&ret](auto, auto lhv) {ret += lhv * lhv;});
    return ret;
}

template<typename VT1, typename IT1, typename VT2, bool TF>
auto l2Dist(const blaze::SparseVector<VT2, TF> &rhs, const CSparseVector<VT1, IT1> &lhs) {
    return l2Dist(lhs, rhs);
}
template<typename VT1, typename IT1, typename VT2, bool TF>
auto l2Dist(const ProdCSparseVector<VT1, IT1> &lhs, const blaze::SparseVector<VT2, TF> &rhs) {
    if(lhs.size() != (*rhs).size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    std::common_type_t<VT1, blz::ElementType_t<VT2>> ret = 0;
    merge::for_each_by_case(lhs.size(), lhs.begin(), lhs.end(), (*rhs).begin(), (*rhs).end(),
                                 [&ret](auto, auto lhv, auto rhv) {
                                    auto v = abs_diff(lhv, rhv);
                                    ret += v * v;},
                                 [&ret](auto, auto rhv) {ret += rhv * rhv;},
                                 [&ret](auto, auto lhv) {ret += lhv * lhv;});
    return ret;
}
template<typename VT1, typename IT1, typename VT2, typename IT2>
auto l2Dist(const CSparseVector<VT1, IT1> &lhs, const CSparseVector<VT2, IT2> &rhs) {
    if(lhs.size() != rhs.size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    std::common_type_t<VT1, VT2> ret = 0;
    merge::for_each_by_case(lhs.size(), lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                 [&ret](auto, auto lhv, auto rhv) {
                                    auto v = abs_diff(lhv, rhv); ret += v * v;},
                                 [&ret](auto, auto rhv) {ret += rhv * rhv;},
                                 [&ret](auto, auto lhv) {ret += lhv * lhv;});
    return ret;
}
template<typename VT1, typename IT1, typename VT2, typename IT2>
auto l2Dist(const ProdCSparseVector<VT1, IT1> &lhs, const CSparseVector<VT2, IT2> &rhs) {
    if(lhs.size() != rhs.size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    std::common_type_t<VT1, VT2> ret = 0;
    merge::for_each_by_case(lhs.size(), lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                 [&ret](auto, auto lhv, auto rhv) {
                                    auto v = abs_diff(lhv, rhv); ret += v * v;},
                                 [&ret](auto, auto rhv) {ret += rhv * rhv;},
                                 [&ret](auto, auto lhv) {ret += lhv * lhv;});
    return ret;
}
template<typename VT1, typename IT1, typename VT2, typename IT2>
auto l2Dist(const CSparseVector<VT1, IT1> &lhs, const ProdCSparseVector<VT2, IT2> &rhs) {
    return l2Dist(rhs, lhs);
}
template<typename VT1, typename IT1, typename VT2, typename IT2>
std::common_type_t<VT1, VT2> l2Dist(const ProdCSparseVector<VT1, IT1> &lhs, const ProdCSparseVector<VT2, IT2> &rhs) {
    if(lhs.size() != rhs.size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    std::common_type_t<VT1, VT2> ret = 0;
    merge::for_each_by_case(lhs.size(), lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                 [&ret](auto, auto lhv, auto rhv) {
                                    auto v = abs_diff(lhv, rhv); ret += v * v;},
                                 [&ret](auto, auto rhv) {ret += rhv * rhv;},
                                 [&ret](auto, auto lhv) {ret += lhv * lhv;});
    return ret;
}
template<typename VT1, typename IT1, typename VT2, bool TF>
std::common_type_t<blz::ElementType_t<VT2>, VT1> l2Dist(const blaze::SparseVector<VT2, TF> &rhs, const ProdCSparseVector<VT1, IT1> &lhs) {
    return l2Dist(lhs, rhs);
}

template<typename T1, typename T2>
auto sqrDist(const T1 &lhs, const T2 &rhs) {
    auto ret = l2Dist(lhs, rhs);
    return ret * ret;
}
template<typename T1, typename T2>
auto sqrl2Dist(const T1 &lhs, const T2 &rhs) {
    return sqrDist(lhs, rhs);
}

template<typename T>
INLINE auto abs(T x) {
    if constexpr(std::is_unsigned_v<T>) {
        return x;
    } else {
        return std::abs(x);
    }
}


template<typename VT1, typename IT1, typename VT2, bool TF>
auto l1Dist(const CSparseVector<VT1, IT1> &lhs, const blaze::SparseVector<VT2, TF> &rhs) {
    //for(const auto &x: lhs) std::fprintf(stderr, "lhs %zu/%g\n", x.index(), x.value());
    //for(const auto &x: *rhs) std::fprintf(stderr, "rhs %zu/%g\n", x.index(), x.value());
    //std::fprintf(stderr, "%s l1dist with %zu/%zu sizes\n", __PRETTY_FUNCTION__, lhs.size(), (*rhs).size());
    if(lhs.size() != (*rhs).size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    std::common_type_t<VT1, blaze::ElementType_t<VT2>, float> ret = 0;
    merge::for_each_by_case(lhs.size(), lhs.begin(), lhs.end(), (*rhs).begin(), (*rhs).end(),
                                 [&ret](auto, auto lhv, auto rhv) {ret += abs_diff(lhv, rhv);},
                                 [&ret](auto, auto rhv) {ret += abs(rhv);},
                                 [&ret](auto, auto lhv) {ret += abs(lhv);});
    return ret;
}

template<typename VT1, typename IT1, typename VT2, bool TF>
auto l1Dist(const blaze::SparseVector<VT2, TF> &rhs, const CSparseVector<VT1, IT1> &lhs) {
    return l1Dist(lhs, rhs);
}
template<typename VT1, typename IT1, typename VT2, bool TF>
auto l1Dist(const ProdCSparseVector<VT1, IT1> &lhs, const blaze::SparseVector<VT2, TF> &rhs) {
    if(lhs.size() != (*rhs).size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    std::common_type_t<VT1, blz::ElementType_t<VT2>, float> ret = 0;
    merge::for_each_by_case(lhs.size(), lhs.begin(), lhs.end(), (*rhs).begin(), (*rhs).end(),
                                 [&ret](auto, auto lhv, auto rhv) {ret += abs_diff(lhv, rhv);},
                                 [&ret](auto, auto rhv) {ret += abs(rhv);},
                                 [&ret](auto, auto lhv) {ret += abs(lhv);});
    return ret;
}
template<typename VT1, typename IT1, typename VT2, typename IT2>
auto l1Dist(const CSparseVector<VT1, IT1> &lhs, const CSparseVector<VT2, IT2> &rhs) {
    if(lhs.size() != rhs.size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    std::common_type_t<VT1, VT2, float> ret = 0;
    merge::for_each_by_case(lhs.size(), lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                 [&ret](auto, auto lhv, auto rhv) {ret += abs_diff(lhv, rhv);},
                                 [&ret](auto, auto rhv) {ret += abs(rhv);},
                                 [&ret](auto, auto lhv) {ret += abs(lhv);});
    return ret;
}
template<typename VT1, typename IT1, typename VT2, typename IT2>
auto l1Dist(const ProdCSparseVector<VT1, IT1> &lhs, const CSparseVector<VT2, IT2> &rhs) {
    if(lhs.size() != rhs.size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    std::common_type_t<VT1, VT2, float> ret = 0;
    merge::for_each_by_case(lhs.size(), lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                 [&ret](auto, auto lhv, auto rhv) {ret += abs_diff(lhv, rhv);},
                                 [&ret](auto, auto rhv) {ret += abs(rhv);},
                                 [&ret](auto, auto lhv) {ret += abs(lhv);});
    return ret;
}
template<typename VT1, typename IT1, typename VT2, typename IT2>
auto l1Dist(const CSparseVector<VT1, IT1> &lhs, const ProdCSparseVector<VT2, IT2> &rhs) {
    return l1Dist(rhs, lhs);
}
template<typename VT1, typename IT1, typename VT2, typename IT2>
std::common_type_t<VT1, VT2> l1Dist(const ProdCSparseVector<VT1, IT1> &lhs, const ProdCSparseVector<VT2, IT2> &rhs) {
    if(lhs.size() != rhs.size()) throw std::invalid_argument("lhs and rhs have mismatched sizes");
    std::common_type_t<VT1, VT2, float> ret = 0;
    merge::for_each_by_case(lhs.size(), lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                 [&ret](auto, auto lhv, auto rhv) {ret += abs_diff(lhv, rhv);},
                                 [&ret](auto, auto rhv) {ret += abs(rhv);},
                                 [&ret](auto, auto lhv) {ret += abs(lhv);});
    std::fprintf(stderr, "ret for l1Dist: %g\n", ret);
    return ret;
}
template<typename VT1, typename IT1, typename VT2, bool TF>
std::common_type_t<blz::ElementType_t<VT2>, VT1> l1Dist(const blaze::SparseVector<VT2, TF> &rhs, const ProdCSparseVector<VT1, IT1> &lhs) {
    return l1Dist(lhs, rhs);
}

template<typename T1, typename T2>
double dot_by_case(const T1 &lhs, const T2 &rhs) {
    double ret = 0.;
    merge::for_each_by_case(lhs.size(), lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                            [&ret](auto, auto lhv, auto rhv) {ret += lhv * rhv;},
                            [&ret](auto, auto) {},
                            [&ret](auto, auto) {});
    return ret;
}
template<typename VT, typename IT, typename VT2, typename IT2>
double dot(const CSparseVector<VT, IT> &lhs, const ProdCSparseVector<VT2, IT2> &rhs) {
    return dot_by_case(lhs, rhs);
}
template<typename VT, typename IT, typename VT2, typename IT2>
double dot(const ProdCSparseVector<VT, IT> &lhs, const CSparseVector<VT2, IT2> &rhs) {
    return dot(rhs, lhs);
}
template<typename VT, typename IT, typename VT2, typename IT2>
double dot(const CSparseVector<VT, IT> &lhs, const CSparseVector<VT2, IT2> &rhs) {
    return dot_by_case(lhs, rhs);
}
template<typename VT, typename IT, typename VT2, typename IT2>
double dot(const ProdCSparseVector<VT, IT> &lhs, const ProdCSparseVector<VT2, IT2> &rhs) {
    return dot_by_case(lhs, rhs);
}

template<typename VT, typename IT, typename VT2, bool TF>
double dot(const CSparseVector<VT, IT> &lhs, const blaze::SparseVector<VT2, TF> &rhs) {
    return dot_by_case(lhs, *rhs);
}
template<typename VT, typename IT, typename VT2, bool TF>
double dot(const ProdCSparseVector<VT, IT> &lhs, const blaze::SparseVector<VT2, TF> &rhs) {
    return dot_by_case(lhs, *rhs);
}
template<typename VT, typename IT, typename VT2, bool TF>
double dot(const blz::SparseVector<VT2, TF> &lhs, const CSparseVector<VT, IT> &rhs) {
    return dot(rhs, *lhs);
}
template<typename VT, typename IT, typename VT2, bool TF>
double dot(const blz::SparseVector<VT2, TF> &lhs, const ProdCSparseVector<VT, IT> &rhs) {
    return dot(rhs, *lhs);
}

template<typename T>
struct IsCSparseVector {
    static constexpr bool value = false;
};
template<typename VT, typename IT>
struct IsCSparseVector<CSparseVector<VT, IT>>: public std::true_type {};
template<typename VT, typename IT>
struct IsCSparseVector<ProdCSparseVector<VT, IT>>: public std::true_type {};

template<typename T>
static constexpr const bool IsCSparseVector_v = IsCSparseVector<T>::value;

template<typename VT, typename IT>
auto make_csparse_view(VT *data, IT *idx, size_t n, size_t dim=-1) {
    return CSparseVector<VT, IT>(data, idx, n, dim);
}

template<typename DataType, typename IndPtrType, typename IndicesType>
size_t nonZeros(const typename CSCMatrixView<IndPtrType, IndicesType, DataType>::Column &col) {
    return col.nnz();
}
template<typename DataType, typename IndPtrType, typename IndicesType>
size_t nonZeros(const CSCMatrixView<IndPtrType, IndicesType, DataType> &mat) {
    return mat.nnz();
}

template<typename VT, typename IT>
struct COOMatrixView {
    IT *x, *y;
    VT *data;
    size_t nr_, nc_, nnz_;
    size_t nnz() const {return nnz_;}
    size_t rows() const {return nr_;}
    size_t columns() const {return nc_;}
};

template<typename VT, typename IT>
struct COOMatrix {
    std::vector<IT> x_, y_;
    std::vector<VT> data_;
    operator COOMatrixView<VT, IT> &() {
        return COOMatrixView<VT, IT> {x_.data(), y_.data(), data_.data()};
    }
    operator COOMatrixView<const VT, const IT> &() const {
        return COOMatrixView<const VT, const IT> {x_.data(), y_.data(), data_.data()};
    }
    size_t nr_, nc_;
    size_t nnz() const {return x_.size();}
    size_t rows() const {return nr_;}
    size_t columns() const {return nc_;}
    void add(IT x, IT y, VT data) {
        x_.push_back(x); y_.push_back(y); data_.push_back(data_);
    }
};


template<typename VT, typename IT, typename IPtrT>
struct CSparseMatrix {
    VT *__restrict__ data_;
    IT *__restrict__ indices_;
    IPtrT *__restrict__ indptr_;
    size_t nr_, nc_, nnz_;
    constexpr CSparseMatrix(VT *__restrict__ data, IT *__restrict__ indices, IPtrT *__restrict__ indptr, size_t nr, size_t nc, size_t nnz):
        data_(data), indices_(indices), indptr_(indptr), nr_(nr), nc_(nc), nnz_(nnz)
    {
    }
    size_t nnz() const {return nnz_;}
    size_t rows() const {return nr_;}
    size_t columns() const {return nc_;}
    auto row(size_t i) {
        return CSparseVector<VT, IT>(data_ + indptr_[i], indices_ + indptr_[i], indptr_[i + 1] - indptr_[i], nc_);
    }
    auto row(size_t i) const {
        return CSparseVector<const VT, IT>(data_ + indptr_[i], indices_ + indptr_[i], indptr_[i + 1] - indptr_[i], nc_);
    }
    auto sum() const {
        return blz::sum(blz::CustomVector<VT, blaze::unaligned, blaze::unpadded>(data_, nnz_));
    }
    auto &operator~() {return *this;}
    const auto &operator~() const {return *this;}
    auto &operator*() {return *this;}
    const auto &operator*() const {return *this;}
    using ElementType = VT;
};

using blaze::unchecked;

template<typename VT, typename ORVT, typename IT, typename IPtr, bool TF, typename OIT=IT, typename WeightT=blz::DV<VT>, bool rowwise=true>
void l1_median(const CSparseMatrix<VT, IT, IPtr> &mat, blaze::Vector<ORVT, TF> &ret, OIT *asnptr=static_cast<OIT *>(nullptr), size_t asnsz=0, WeightT *weightc=static_cast<WeightT *>(nullptr)) {
    if constexpr(rowwise) {
        using RVT = blaze::ElementType_t<ORVT>;
        shared::flat_hash_map<IT, std::vector<RVT>> nzfeatures;
        nzfeatures.reserve(mat.columns());
        const size_t nrows = asnsz ? asnsz: mat.rows();
        if(weightc) throw NotImplementedError("Not implemented: weighted L1 median for CSparseMatrix");
        for(size_t i = 0; i < nrows; ++i) {
            auto rownum = asnsz ? OIT(asnptr[i]): OIT(i);
            auto r = row(mat, rownum, unchecked);
            auto nnz = nonZeros(r);
            if(!nnz) continue;
            for(size_t i = 0; i < r.n_; ++i) {
                auto idx = r.indices_[i];
                auto val = r.data_[i];
                auto it = nzfeatures.find(idx);
                if(it == nzfeatures.end()) {
                    it = nzfeatures.emplace(idx, std::vector<RVT>{RVT(val)}).first;
                } else {
                    it->second.push_back(val);
                }
            }
        }
        const size_t nfeat = nzfeatures.size();
        (*ret).reset();
        (*ret).resize(mat.nc_);
        (*ret).reserve(nfeat);
        const bool nr_is_odd = nrows & 1;
        for(auto &pair: nzfeatures) {
            auto it = &pair.second;
            auto i = pair.first;
            auto ibeg = it->begin(), iend = it->end();
            shared::sort(ibeg, iend);
            size_t nel = it->size();
            if(nel > nrows / 2) {
                if(it->front() > 0.) {
                    if(nr_is_odd) {
                        auto mid = ibeg + nrows / 2 - (nrows - nel);
                        (*ret)[i] = *mid;
                    } else {
                        auto midm1 = ibeg + nrows / 2 - (nrows - nel), mid = midm1 + 1;
                        (*ret)[i] = .5 * (*mid + *midm1);
                    }
                } else if(it->back() < 0.) {
                    if(nr_is_odd) {
                        (*ret)[i] = *(iend - nrows / 2 + (nrows - nel));
                    } else {
                        auto midm1 = iend - nrows / 2 + (nrows - nel), mid = midm1 + 1;
                        (*ret)[i] = .5 * (*mid + *midm1);
                    }
                } else {
                    it->resize(nrows, RVT(0));
                    shared::sort(it->begin(), it->end());
                    if(nr_is_odd) (*ret)[i] = (*it)[nrows / 2];
                    else (*ret)[i] = .5 * ((*it)[nrows / 2] + (*it)[nrows / 2 + 1]);
                }
            } else if((nrows & 1) && nel == nrows / 2) {
                if(it->front() > 0.) (*ret)[i] = .5 * it->front();
                else if(it->back() < 0.) (*ret)[i] = .5 * it->back();
            } // else the value is 0
        }
    } else {
        throw std::runtime_error("Not implemented");
    }
}

template<typename VT, typename ORVT, typename IT, typename IPtr, bool TF, typename OIT, typename WeightT=blz::DV<VT>, bool rowwise=true, typename RSums>
void tvd_median(const CSparseMatrix<VT, IT, IPtr> &mat, blaze::SparseVector<ORVT, TF> &ret, OIT *asnptr=static_cast<OIT *>(nullptr), size_t asnsz=0, WeightT *weightc=static_cast<WeightT *>(nullptr), const RSums &rsums=RSums()) {
    if constexpr(rowwise) {
        using RVT = blaze::ElementType_t<ORVT>;
        shared::flat_hash_map<IT, std::vector<RVT>> nzfeatures;
        nzfeatures.reserve(mat.columns());
        const size_t nrows = asnsz ? asnsz: mat.rows();
        if(weightc) throw NotImplementedError("Not implemented");
        for(size_t i = 0; i < nrows; ++i) {
            auto rownum = asnsz ? OIT(asnptr[i]): OIT(i);
            auto r = row(mat, rownum, unchecked);
            auto nnz = nonZeros(r);
            if(!nnz) continue;
            const typename std::conditional_t<(sizeof(VT) <= 4), float, double> invmul = 1. / rsums[rownum];
            for(size_t i = 0; i < r.n_; ++i) {
                auto idx = r.indices_[i];
                const RVT val = RVT(r.data_[i]) * invmul;
                auto it = nzfeatures.find(idx);
                if(it == nzfeatures.end()) {
                    it = nzfeatures.emplace(idx, std::vector<RVT>{val}).first;
                } else {
                    it->second.push_back(val);
                }
            }
        }
        const size_t nfeat = nzfeatures.size();
        (*ret).reset();
        (*ret).resize(mat.nc_);
        (*ret).reserve(nfeat);
        const bool nr_is_odd = nrows & 1;
        for(auto &pair: nzfeatures) {
            auto it = &pair.second;
            auto i = pair.first;
            auto ibeg = it->begin(), iend = it->end();
            shared::sort(ibeg, iend);
            size_t nel = it->size();
            if(nel > nrows / 2) {
                if(it->front() > 0.) {
#define append insert
                    if(nr_is_odd) {
                        auto mid = ibeg + nrows / 2 - (nrows - nel);
                        (*ret).append(i, *mid);
                    } else {
                        auto midm1 = ibeg + nrows / 2 - (nrows - nel), mid = midm1 + 1;
                        (*ret).append(i, .5 * (*mid + *midm1));
                    }
                } else if(it->back() < 0.) {
                    if(nr_is_odd) {
                        auto mid = iend - nrows / 2 + (nrows - nel);
                        (*ret).append(i, *mid);
                    } else {
                        auto midm1 = ibeg - nrows / 2 + (nrows - nel), mid = midm1 + 1;
                        (*ret).append(i, (.5) * (*mid + *midm1));
                    }
                } else {
                    it->resize(nrows, RVT(0));
                    shared::sort(it->begin(), it->end());
                    if(nr_is_odd) (*ret).append(i, (*it)[nrows / 2]);
                    else          (*ret).append(i, .5 * ((*it)[nrows / 2 - 1] + (*it)[nrows / 2]));
                }
            } else if((nrows & 1) && nel == nrows / 2) {
                if(it->front() > 0.) (*ret).append(i, .5 * it->front());
                else if(it->back() < 0.) (*ret).append(i, .5 * it->back());
            } // else it's zero.
#undef append
        }
    } else {
        throw std::runtime_error("Not implemented");
    }
}

template<typename VT, typename IT, typename IPtrT>
inline CSparseMatrix<VT, IT, IPtrT> make_csparse_matrix(VT *__restrict__ data, IT *__restrict__ indices, IPtrT *__restrict__ indptr, size_t nr, size_t nc, size_t nnz) {
    return CSparseMatrix<VT, IT, IPtrT>(data, indices, indptr, nr, nc, nnz);
}

template<typename VT, typename IT, typename IPtrT, bool checked>
inline auto row(const CSparseMatrix<VT, IT, IPtrT> &mat, size_t i, blaze::Check<checked>) {
    if constexpr(checked)  {
        if(unlikely(i > mat.rows())) throw std::out_of_range(std::string("Out of range: row ") + std::to_string(i) + " out of " + std::to_string(mat.rows()));
    }
    return mat.row(i);
}
template<typename VT, typename IT, typename IPtrT>
inline auto row(const CSparseMatrix<VT, IT, IPtrT> &mat, size_t i) {
    return row(mat, i, blaze::Check<true>());
}
template<typename VT, typename IT, typename IPtrT, bool checked>
inline auto row(CSparseMatrix<VT, IT, IPtrT> &mat, size_t i, blaze::Check<checked>) {
    if constexpr(checked)  {
        if(unlikely(i > mat.rows())) throw std::out_of_range(std::string("Out of range: row ") + std::to_string(i) + " out of " + std::to_string(mat.rows()));
    }
    return mat.row(i);
}
template<typename VT, typename IT, typename IPtrT>
inline auto row(CSparseMatrix<VT, IT, IPtrT> &mat, size_t i) {
    return row(mat, i, blaze::Check<true>());
}

template<typename VT, typename IT, typename IPtrT>
INLINE auto sum(const CSparseMatrix<VT, IT, IPtrT> &sm) {
    return sm.sum();
}

template<typename VT, typename IT>
INLINE auto sum(const ProdCSparseVector<VT, IT> &sm) {
    return sm.sum();
}
template<typename VT, typename IT>
INLINE auto sum(const CSparseVector<VT, IT> &sm) {
    return sm.sum();
}

template<bool SO, typename VT, typename IT, typename IPtrT, typename RVT=VT>
inline decltype(auto) sum(const CSparseMatrix<VT, IT, IPtrT> &sm) {
    if constexpr(SO == blz::rowwise) {
        return blaze::generate(
            sm.rows(),[smd=sm.data_, ip=sm.indptr_](auto x) {
                return blz::sum(blz::CustomVector<VT, blaze::unaligned, blaze::unpadded>(
                    smd + ip[x], ip[x + 1] - ip[x]));
            }
        );
    } else {
        blaze::DynamicVector<VT, blz::rowVector> sums(sm.columns());
        OMP_PFOR
        for(size_t i = 0; i < sm.rows(); ++i) {
            auto r = row(sm, i);
            #pragma GCC unroll 4
            for(size_t i = 0; i < r.n_; ++i) {
                OMP_ATOMIC
                sums[r.indices_[i]] += r.data_[i];
            }
        }
        return sums;
    }
}

template<typename VT, typename IT>
std::ostream& operator<< (std::ostream& out, const ProdCSparseVector<VT, IT> & item)
{
    auto it = item.begin();
    for(size_t i = 0; i < item.dim_; ++i) {
        if(it->index() > i) {
            out << 0.;
        } else {
            out << it->value();
            if(it != item.end()) ++it;
        }
        out << ' ';
    }
    out << '\n';
    return out;
}

template<typename VT, typename IT>
std::ostream& operator<< (std::ostream& out, const CSparseVector<VT, IT> & item)
{
    auto it = item.begin();
    for(size_t i = 0; i < item.dim_; ++i) {
        if(it == item.end() || it->index() > i) {
            out << 0.;
        } else {
            out << it->value();
            if(it != item.end()) ++it;
        }
        out << ' ';
    }
    out << '\n';
    return out;
}

template<typename VT, typename IT, typename IPtrT>
std::ostream& operator<< (std::ostream& out, const CSparseMatrix<VT, IT, IPtrT> & item)
{
    out << "( ";
    for(size_t i = 0; i < item.rows(); ++i) {
        out << row(item, i, blz::unchecked);
    }
    out << ")";
    return out;
}
template<typename VT, bool SO, typename SVT, typename SVI>
decltype(auto) assign(blaze::DenseVector<VT, SO> &lhs, const CSparseVector<SVT, SVI> &rhs) {
    if((*lhs).size() != rhs.size()) (*lhs).resize(rhs.size());
    *lhs = 0.;
    DBG_ONLY(size_t i = 0;)
    for(const auto &pair: rhs) {
        assert(rhs.data_[i] == pair.value());
        assert(rhs.indices_[i] == pair.index());
        (*lhs)[pair.index()] = pair.value();
        DBG_ONLY(++i;)
    }
    DBG_ONLY(std::cerr << *lhs;)
    return *lhs;
}

template<typename VT, bool SO, typename SVT, typename SVI>
decltype(auto) assign(blaze::SparseVector<VT, SO> &lhs, const CSparseVector<SVT, SVI> &rhs) {
    auto nnz = rhs.nnz();
    if((*lhs).size() != rhs.size()) (*lhs).resize(rhs.size());
    (*lhs).reset();
    if(!nnz) {
        return *lhs;
    }
    (*lhs).reserve(nnz);
    size_t i = 0;
    for(const auto &pair: rhs) {
        assert(rhs.data_[i] == pair.value());
        assert(rhs.indices_[i] == pair.index());
        (*lhs).append(pair.index(), pair.value());
        ++i;
    }
    assert((*lhs).size() == rhs.size());
    assert(i == rhs.nnz());
#ifndef NDEBUG
    std::cerr << *lhs;
    std::cerr << rhs;
#endif
    return *lhs;
}

template<typename VT, typename IT, typename IPtrT>
constexpr inline size_t nonZeros(const CSparseMatrix<VT, IT, IPtrT> &x) {
    return x.nnz();
}
template<typename VT, typename IT>
constexpr inline size_t nonZeros(const ProdCSparseVector<VT, IT> &x) {
    return x.nnz();
}
template<typename VT, typename IT>
constexpr inline size_t nonZeros(const CSparseVector<VT, IT> &x) {
    return x.nnz();
}
template<typename VT, typename IT>
constexpr inline size_t size(const CSparseVector<VT, IT> &x) {
    return x.size();
}

template<typename VT, typename IT, typename IPtrT, typename RetT, typename IT2=uint64_t, typename WeightT=blz::DV<VT>>
void geomedian(const CSparseMatrix<VT, IT, IPtrT> &mat, RetT &center, IT2 *ptr = static_cast<IT2 *>(nullptr), size_t nasn=0, WeightT *weights=static_cast<WeightT *>(nullptr), double eps=0.) {
    //if(weights) throw NotImplementedError("No weighted geometric median supported");
    double prevcost = std::numeric_limits<double>::max();
    size_t iternum = 0;
    assert(center.size() == mat.columns());
    const size_t npoints = ptr ? nasn: mat.rows();
    using index_t = std::common_type_t<IT2, size_t>;
    blz::DV<double> costs(npoints);
    for(;;) {
        static constexpr double MINVAL = 1e-80; // For the case of exactly lying on a current center
        costs = blaze::max(blaze::generate(npoints, [&](auto x) {return l2Dist(center, row(mat, ptr ? index_t(ptr[x]): index_t(x), blz::unchecked));}),
                           MINVAL);
        double current_cost = sum(costs);
        double dist = std::abs(prevcost - current_cost);
        if(dist <= eps) break;
        if(std::isnan(dist)) throw std::range_error("distance is nan");
        if(++iternum == 100000) {
            std::fprintf(stderr, "Failed to terminate: %g\n", dist);
            break;
        }
        prevcost = current_cost;
        if(weights) costs = *weights / costs;
        else costs = 1. / costs;
        costs /= sum(costs);
        blz::DV<double, blaze::TransposeFlag_v<RetT>> newcenter(mat.columns(), 0);
        OMP_PFOR
        for(size_t i = 0; i < npoints; ++i) {
            for(const auto &pair: row(mat, ptr ? index_t(ptr[i]): index_t(i))) {
                OMP_ATOMIC
                newcenter[pair.index()] += pair.value() * costs[i];
            }
        }
        assign(center, newcenter);
    }
}


template<typename FT=float, typename IndPtrType, typename IndicesType, typename DataType>
blz::SM<FT, blaze::rowMajor> csc2sparse(const CSCMatrixView<IndPtrType, IndicesType, DataType> &mat, bool skip_empty=false) {
    blz::SM<FT, blaze::rowMajor> ret(mat.n_, mat.nf_);
    ret.reserve(mat.nnz_);
    size_t used_rows = 0, i;
    for(i = 0; i < mat.n_; ++i) {
        auto col = mat.column(i);
        const unsigned cnnz = col.nnz();
        DBG_ONLY(std::fprintf(stderr, "%zu/%u (%u nnz) \r", i, mat.n_, cnnz);)
        if(skip_empty && 0u == cnnz) continue;
        for(auto s = col.start_; s < col.stop_; ++s) {
            ret.append(used_rows, mat.indices_[s], mat.data_[s]);
        }
        ret.finalize(used_rows++);
    }
    if(used_rows != i) {
        const auto nr = used_rows;
        std::fprintf(stderr, "Only used %zu/%zu rows, skipping empty rows\n", used_rows, i);
        while(used_rows < mat.n_) ret.finalize(used_rows++);
        ret.resize(nr, ret.columns(), /*preserve_values=*/true);
    }
    // Sort after, which is faster than ensuring that every row is sorted at the beginning
    OMP_PFOR
    for(size_t i = 0; i < ret.rows(); ++i) {
        auto cmp = [](auto &x, auto &y) {return x.index() < y.index();};
        auto rcmp = [](auto &x, auto &y) {return x.index() > y.index();};
        auto r = row(ret, i, blz::unchecked);
        switch(nonZeros(r)) {
            case 0: case 1: continue;
            default: ;
        }
        DBG_ONLY(std::fprintf(stderr, "sorting %zu/%zu\n", i, ret.rows());)
        if(!std::is_sorted(r.begin(), r.end(), cmp)) {
            if(std::is_sorted(r.begin(), r.end(), rcmp)) {
                std::reverse(r.begin(), r.end());
            } else {
                shared::sort(r.begin(), r.end(), cmp);
            }
        }
    }
    return ret;
}

template<typename FT=float, typename IndPtrType=uint64_t, typename IndicesType=uint64_t, typename DataType=uint32_t>
blz::SM<FT, blaze::rowMajor> csc2sparse(std::string prefix, bool skip_empty=false) {
    util::Timer t("csc2sparse load time");
    std::string indptrn  = prefix + "indptr.file";
    std::string indicesn = prefix + "indices.file";
    std::string datan    = prefix + "data.file";
    std::string shape    = prefix + "shape.file";
    if(!is_file(indptrn)) throw std::runtime_error(std::string("Missing indptr: ") + indptrn);
    if(!is_file(indicesn)) throw std::runtime_error(std::string("Missing indices: ") + indicesn);
    if(!is_file(datan)) throw std::runtime_error(std::string("Missing indptr: ") + datan);
    if(!is_file(shape)) throw std::runtime_error(std::string("Missing indices: ") + shape);
    std::FILE *ifp = std::fopen(shape.data(), "rb");
    uint32_t dims[2];
    if(std::fread(dims, sizeof(uint32_t), 2, ifp) != 2) throw std::runtime_error("Failed to read dims from file");
    uint32_t nfeat = dims[0], nsamples = dims[1];
    std::fprintf(stderr, "nfeat: %u. nsample: %u\n", nfeat, nsamples);
    std::fclose(ifp);
    using mmapper = mio::mmap_source;
    mmapper indptr(indptrn), indices(indicesn), data(datan);
    CSCMatrixView<IndPtrType, IndicesType, DataType>
        matview((IndPtrType *)indptr.data(), (IndicesType *)indices.data(),
                (DataType *)data.data(), indices.size() / sizeof(IndicesType),
                 nfeat, nsamples);
    std::fprintf(stderr, "[%s] indptr size: %zu\n", __PRETTY_FUNCTION__, indptr.size() / sizeof(IndPtrType));
    std::fprintf(stderr, "[%s] indices size: %zu\n", __PRETTY_FUNCTION__, indices.size() / sizeof(IndicesType));
    std::fprintf(stderr, "[%s] data size: %zu\n", __PRETTY_FUNCTION__, data.size() / sizeof(DataType));
#ifndef MADV_REMOVE
#  define MADV_FLAGS (MADV_DONTNEED | MADV_FREE)
#else
#  define MADV_FLAGS (MADV_DONTNEED | MADV_REMOVE)
#endif
    ::madvise((void *)indptr.data(), indptr.size(), MADV_FLAGS);
    ::madvise((void *)indices.data(), indices.size(), MADV_FLAGS);
    ::madvise((void *)data.data(), data.size(), MADV_FLAGS);
#undef MADV_FLAGS
    return csc2sparse<FT>(matview, skip_empty);
}


template<typename FT, typename IT=size_t, bool SO=blaze::rowMajor>
struct COOElement {
    IT x, y;
    FT z;
    bool operator<(const COOElement &o) const {
        if constexpr(SO == blaze::rowMajor) {
            return std::tie(x, y, z) < std::tie(o.x, o.y, o.z);
        } else {
            return std::tie(y, x, z) < std::tie(o.y, o.x, o.z);
        }
    }
};


template<typename FT=float, bool SO=blaze::rowMajor, typename IT=size_t>
blz::SM<FT, SO> mtx2sparse(std::string path, bool perform_transpose=false) {
#ifndef NDEBUG
    TimeStamper ts("Parse mtx metadata");
#define MNTSA(x) ts.add_event((x))
#else
#define MNTSA(x)
#endif
    std::string line;
    auto [ifsp, fp] = io::xopen(path);
    auto &ifs = *ifsp;
    do std::getline(ifs, line); while(line.front() == '%');
    char *s;
    size_t nr      = std::strtoull(line.data(), &s, 10),
           columns = std::strtoull(s, &s, 10),
           lines   = std::strtoull(s, nullptr, 10);
    MNTSA("Reserve space");
    blz::SM<FT> ret(nr, columns);
    ret.reserve(lines);
    std::unique_ptr<COOElement<FT, IT, SO>[]> items(new COOElement<FT, IT, SO>[lines]);
    MNTSA("Read lines");
    size_t i = 0;
    while(std::getline(ifs, line)) {
        if(unlikely(i >= lines)) {
            char buf[1024];
            std::sprintf(buf, "i (%zu) > nnz (%zu). malformatted mtxfile at %s?\n", i, lines, path.data());
            MN_THROW_RUNTIME(buf);
        }
        char *s;
        auto x = std::strtoull(line.data(), &s, 10) - 1;
        auto y = std::strtoull(s, &s, 10) - 1;
        items[i++] = {x, y, static_cast<FT>(std::atof(s))};
    }
    if(i != lines) {
        char buf[1024];
        std::sprintf(buf, "i (%zu) != expected nnz (%zu). malformatted mtxfile at %s?\n", i, lines, path.data());
        MN_THROW_RUNTIME(buf);
    }
    std::fprintf(stderr, "processed %zu lines\n", lines);
    MNTSA(std::string("Sort ") + std::to_string(lines) + "items");
    auto it = items.get(), e = items.get() + lines;
    shared::sort(it, e);
    MNTSA("Set final matrix");
    for(size_t ci = 0;;) {
        if(it != e) {
            static constexpr bool RM = SO == blaze::rowMajor;
            const auto xv = RM ? it->x: it->y;
            while(ci < xv) ret.finalize(ci++);
            auto nextit = std::find_if(it, e, [xv](auto x) {
                if constexpr(RM) return x.x != xv; else return x.y != xv;
            });
#ifdef VERBOSE_AF
            auto diff = nextit - it;
            std::fprintf(stderr, "x at %d has %zu entries\n", int(it->x), diff);
            for(std::ptrdiff_t i = 0; i < diff; ++i)
                std::fprintf(stderr, "item %zu has %zu for y and %g\n", i, (it + i)->y, (it + i)->z);
#endif
            ret.reserve(ci, nextit - it);
            for(;it != nextit; ++it) {
                if constexpr(RM) {
                    ret.append(it->x, it->y, it->z);
                } else {
                    ret.append(it->x, it->y, it->z);
                }
            }
        } else {
            for(const size_t end = SO == blaze::rowMajor ? nr: columns;ci < end; ret.finalize(ci++));
            break;
        }
    }
    if(perform_transpose) {
        MNTSA("Perform transpose");
        blz::transpose(ret);
    }
    return ret;
}
#undef MNTSA


template<typename MT, bool SO>
std::pair<std::vector<size_t>, std::vector<size_t>>
erase_empty(blaze::Matrix<MT, SO> &mat) {
    std::pair<std::vector<size_t>, std::vector<size_t>> ret;
    std::fprintf(stderr, "Before resizing, %zu/%zu\n", (*mat).rows(), (*mat).columns());
    size_t orn = (*mat).rows(), ocn = (*mat).columns();
    auto rs = blaze::evaluate(blaze::sum<blaze::rowwise>(*mat));
    auto rsn = blz::functional::indices_if([&rs](auto x) {return rs[x] > 0.;}, rs.size());
    ret.first.assign(rsn.begin(), rsn.end());
    std::fprintf(stderr, "Eliminating %zu empty rows\n", orn - rsn.size());
    *mat = rows(*mat, rsn);
    auto cs = blaze::evaluate(blaze::sum<blaze::columnwise>(*mat));
    auto csn = blz::functional::indices_if([&cs](auto x) {return cs[x] > 0.;}, cs.size());
    ret.second.assign(csn.begin(), csn.end());
    *mat = columns(*mat, csn);
    std::fprintf(stderr, "Eliminating %zu empty columns\n", ocn - rsn.size());
    return ret;
}

template<typename FT=float, bool SO=blaze::rowMajor, typename IndPtrType, typename IndicesType, typename DataType>
blz::SM<FT, SO> csc2sparse(const IndPtrType *indptr, const IndicesType *indices, const DataType *data,
                           size_t nnz, size_t nfeat, uint32_t nitems) {
    return csc2sparse<FT, SO>(CSCMatrixView<IndPtrType, IndicesType, DataType>(indptr, indices, data, nnz, nfeat, nitems));
}

} // namespace util
using util::csc2sparse;
using util::mtx2sparse;
using util::nonZeros;
using util::CSCMatrixView;
using util::is_csc_view;
using util::is_csc_view_v;

} // namespace minicore

#endif /* CSC_H__ */
