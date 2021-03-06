
#include "pyfgc.h"
#include "smw.h"
#include "pycsparse.h"
#include "pyhelpers.h"
using blaze::unaligned;
using blaze::unpadded;
using blaze::rowwise;
using blaze::unchecked;

using minicore::util::sum;
using minicore::util::row;
using blaze::row;

void init_cmp(py::module &m) {
    m.def("cmp", [](const SparseMatrixWrapper &lhs, py::array arr, py::object msr, py::object betaprior, py::object reverse) {
        auto inf = arr.request();
        const bool revb = reverse.cast<bool>();
        const double priorv = betaprior.cast<double>(), priorsum = priorv * lhs.columns();
        if(inf.format.size() != 1) throw std::invalid_argument("Invalid dtype");
        const char dt = inf.format[0];
        const size_t nr = lhs.rows();
        const auto ms = assure_dm(msr);
        blz::DV<float> rsums(lhs.rows());
        blz::DV<double> priorc({priorv});
        lhs.perform([&](const auto &x){rsums = blz::sum<rowwise>(x);});
        if(inf.ndim == 1) {
            if(inf.size != py::ssize_t(lhs.columns())) throw std::invalid_argument("Array must be of the same dimensionality as the matrix");
            py::array_t<float> ret(nr);
            auto v = blz::make_cv((float *)ret.request().ptr, nr);
            lhs.perform([&](auto &matrix) {
                using ET = typename std::decay_t<decltype(matrix)>::ElementType;
                using MsrType = std::conditional_t<std::is_floating_point_v<ET>, ET, std::conditional_t<(sizeof(ET) <= 4), float, double>>;
                switch(dt) {
#define CASE_F(char, type) \
                    case char: {\
                        blz::SV<float> sv(blz::make_cv((type *)inf.ptr, inf.size));\
                        const auto vsum = blz::sum(sv);\
                        v = blz::generate(nr, [vsum,priorsum,ms,&matrix,&rsums,&sv,&priorc,revb](auto x) {\
                            return revb ? cmp::msr_with_prior<MsrType>(ms, row(matrix, x), sv, priorc, priorsum, rsums[x], vsum)\
                                        : cmp::msr_with_prior<MsrType>(ms, sv, row(matrix, x), priorc, priorsum, vsum, rsums[x]);\
                        });\
                    } break;
                    CASE_F('f', float)
                    CASE_F('d', double)
                    case 'i': CASE_F('I', unsigned)
#undef CASE_F
                    default: throw std::invalid_argument("dtypes supported: d, f, i, I");
                }
            });
            return ret;
        } else if(inf.ndim == 2) {
            const py::ssize_t nc = inf.shape[1], ndr = inf.shape[0];
            if(nc != py::ssize_t(lhs.columns()))
                throw std::invalid_argument("Array must be of the same dimensionality as the matrix");
            py::array_t<float> ret(std::vector<py::ssize_t>{py::ssize_t(nr), ndr});
            blz::CustomMatrix<float, unaligned, unpadded, blz::rowMajor> cm((float *)ret.request().ptr, nr, ndr);
            lhs.perform([&](auto &matrix) {
                using ET = typename std::decay_t<decltype(matrix)>::ElementType;
                using MsrType = std::conditional_t<std::is_floating_point_v<ET>, ET, std::conditional_t<(sizeof(ET) <= 4), float, double>>;
#define CASE_F(char, type) \
                        case char: {\
                            blaze::CustomMatrix<type, unaligned, unpadded> ocm(static_cast<type *>(inf.ptr), ndr, nc);\
                            const auto cmsums = blz::evaluate(blz::sum<blz::rowwise>(ocm));\
                            blz::SM<float> sv = ocm;\
                            cm = blz::generate(nr, ndr, [&](auto x, auto y) -> float {\
                                return revb ? cmp::msr_with_prior<MsrType>(ms, \
                                        blz::row(matrix, x, unchecked), \
                                        blz::row(sv, y, unchecked), \
                                        priorc, priorsum, rsums[x], cmsums[y])\
                            : cmp::msr_with_prior<MsrType>(ms, \
                                        blz::row(sv, y, unchecked), \
                                        blz::row(matrix, x, unchecked), \
                                        priorc, priorsum, cmsums[y], rsums[x]);\
                            });\
                        } break;
                    switch(dt) {
                        CASE_F('f', float)
                        CASE_F('d', double)
                        CASE_F('i', int)
                        CASE_F('I', unsigned)
                        CASE_F('h', int16_t)
                        CASE_F('H', uint16_t)
                        CASE_F('b', int16_t)
                        CASE_F('B', uint16_t)
                        CASE_F('l', int64_t)
                        CASE_F('L', uint64_t)
#undef CASE_F
                        default: throw std::invalid_argument("dtypes supported: d, f, i, I, h, H, b, B, l, L");
                    }
                    return 0.;
            });
            return ret;
        } else {
            throw std::invalid_argument("NumPy array expected to have 1 or two dimensions.");
        }
        __builtin_unreachable();
        return py::array_t<float>();
    }, py::arg("matrix"), py::arg("data"), py::arg("msr") = 2, py::arg("prior") = 0., py::arg("reverse") = false);
    m.def("cmp", [](const SparseMatrixWrapper &lhs, const SparseMatrixWrapper &rhs, py::object msr, py::object betaprior, bool reverse, int use_float=-1) {
        if(use_float < 0) use_float = lhs.is_float() || rhs.is_float();
        const double priorv = betaprior.cast<double>(), priorsum = priorv * lhs.columns();
        const auto ms = assure_dm(msr);
        blz::DV<float> lrsums(lhs.rows());
        blz::DV<float> rrsums(lhs.rows());
        blz::DV<double> priorc({priorv});
        if(lhs.columns() != rhs.columns()) throw std::invalid_argument("mismatched # columns");
        lhs.perform([&](const auto &x){lrsums = blz::sum<rowwise>(x);});
        rhs.perform([&](const auto &x){rrsums = blz::sum<rowwise>(x);});
        const py::ssize_t nr = lhs.rows(), nc = rhs.rows();
        py::array ret(py::dtype(use_float ? "f": "d"), std::vector<py::ssize_t>{nr, nc});
        auto retinf = ret.request();
        blz::CustomMatrix<float, unaligned, unpadded, blz::rowMajor> cm((float *)retinf.ptr, nr, nc, nc);
        blz::CustomMatrix<double, unaligned, unpadded, blz::rowMajor> cmd((double *)retinf.ptr, nr, nc, nc);
        const SparseMatrixWrapper *lhp = &lhs, *rhp = &rhs;
        if(lhs.is_float() != rhs.is_float() && rhs.is_float()) {
            std::swap(lhp, rhp);
        }
#define __FUNC\
        auto func = [&](auto lh, auto rh) -> double {\
            auto lsum = lrsums[lh], rsum = rrsums[rh];\
            auto lrow(row(lhr, lh, unchecked));\
            auto rrow(row(rhr, rh, unchecked));\
            return use_float ?\
                ( reverse\
                    ? cmp::msr_with_prior<float>(ms, lrow, rrow, priorc, priorsum, lsum, rsum)\
                    : cmp::msr_with_prior<float>(ms, rrow, lrow, priorc, priorsum, rsum, lsum))\
                : reverse\
                    ? cmp::msr_with_prior<double>(ms, lrow, rrow, priorc, priorsum, lsum, rsum)\
                    : cmp::msr_with_prior<double>(ms, rrow, lrow, priorc, priorsum, rsum, lsum);\
        };
#define DO_GEN(mat) mat = blaze::generate(nr, nc, func)
#define DO_GEN_IF {__FUNC if(use_float) {DO_GEN(cm);} else {DO_GEN(cmd);}}
        if(lhs.is_float() && rhs.is_float()) {
            auto &lhr = lhs.getfloat(), &rhr = rhs.getfloat();
            DO_GEN_IF
        } else if(lhs.is_double() && rhs.is_double()) {
            auto &lhr = lhs.getdouble(); auto &rhr = rhs.getdouble();
            DO_GEN_IF
        } else {
            auto &lhr = lhp->getfloat(); auto &rhr = rhp->getdouble();
            DO_GEN_IF
        }
#undef DO_GEN
        return ret;
    }, py::arg("matrix"), py::arg("data"), py::arg("msr") = 2, py::arg("prior") = 0., py::arg("reverse") = false, py::arg("use_float") = -1);
    m.def("cmp", [](const PyCSparseMatrix &lhs, py::array arr, py::object msr, py::object betaprior, py::object reverse) {
        const bool revb = reverse.cast<bool>();
        auto inf = arr.request();
        const double priorv = betaprior.cast<double>(), priorsum = priorv * lhs.columns();
        if(inf.format.size() != 1) throw std::invalid_argument("Invalid dtype");
        const char dt = inf.format[0];
        const size_t nr = lhs.rows();
        const auto ms = assure_dm(msr);
        blz::DV<float> rsums(lhs.rows());
        blz::DV<double> priorc({priorv});
        lhs.perform([&](const auto &x){rsums = sum<rowwise>(x);});
        if(inf.ndim == 1) {
            if(inf.size != py::ssize_t(lhs.columns())) throw std::invalid_argument("Array must be of the same dimensionality as the matrix");
            py::array_t<float> ret(nr);
            auto v = blz::make_cv((float *)ret.request().ptr, nr);
            lhs.perform([&](auto &matrix) {
                using ET = typename std::decay_t<decltype(matrix)>::ElementType;
                using MsrType = std::conditional_t<std::is_floating_point_v<ET>, ET, std::conditional_t<(sizeof(ET) <= 4), float, double>>;
                switch(dt) {
#define CASE_F(char, type) \
                    case char: {\
                        blz::SV<float> sv(blz::make_cv((type *)inf.ptr, inf.size));\
                        const auto vsum = blz::sum(sv);\
                        v = blz::generate(nr, [vsum,priorsum,ms,&matrix,&rsums,&sv,&priorc,revb](auto x) {\
                            return revb ? cmp::msr_with_prior<MsrType>(ms, row(matrix, x), sv, priorc, priorsum, rsums[x], vsum)\
                                        : cmp::msr_with_prior<MsrType>(ms, sv, row(matrix, x), priorc, priorsum, vsum, rsums[x]);\
                        });\
                    } break;
                    CASE_F('f', float)
                    CASE_F('d', double)
                    case 'i': CASE_F('I', unsigned)
#undef CASE_F
                    default: throw std::invalid_argument("dtypes supported: d, f, i, I");
                }
            });
            return ret;
        } else if(inf.ndim == 2) {
            const py::ssize_t nc = inf.shape[1], ndr = inf.shape[0];
            if(nc != py::ssize_t(lhs.columns()))
                throw std::invalid_argument("Array must be of the same dimensionality as the matrix");
            py::array_t<float> ret(std::vector<py::ssize_t>{py::ssize_t(nr), ndr});
            blz::CustomMatrix<float, unaligned, unpadded, blz::rowMajor> cm((float *)ret.request().ptr, nr, ndr);
            lhs.perform([&](auto &matrix) {
                using ET = typename std::decay_t<decltype(matrix)>::ElementType;
                using MsrType = std::conditional_t<std::is_floating_point_v<ET>, ET, std::conditional_t<(sizeof(ET) <= 4), float, double>>;
#define CASE_F(char, type) \
                        case char: {\
                            blaze::CustomMatrix<type, unaligned, unpadded> ocm(static_cast<type *>(inf.ptr), ndr, nc);\
                            const auto cmsums = blz::evaluate(blz::sum<blz::rowwise>(ocm));\
                            blz::SM<float> sv = ocm;\
                            cm = blz::generate(nr, ndr, [&](auto x, auto y) -> float {\
                                return cmp::msr_with_prior<MsrType>(ms, row(sv, y, unchecked), row(matrix, x, unchecked), priorc, priorsum, cmsums[y], rsums[x]);\
                            });\
                        } break;
                    switch(dt) {
                        CASE_F('f', float)
                        CASE_F('d', double)
                        CASE_F('i', int)
                        CASE_F('I', unsigned)
                        CASE_F('h', int16_t)
                        CASE_F('H', uint16_t)
                        CASE_F('b', int16_t)
                        CASE_F('B', uint16_t)
                        CASE_F('l', int64_t)
                        CASE_F('L', uint64_t)
#undef CASE_F
                        default: throw std::invalid_argument("dtypes supported: d, f, i, I, h, H, b, B, l, L");
                    }
                    return 0.;
            });
            return ret;
        } else {
            throw std::invalid_argument("NumPy array expected to have 1 or two dimensions.");
        }
        __builtin_unreachable();
        return py::array_t<float>();
    }, py::arg("matrix"), py::arg("data"), py::arg("msr") = 2, py::arg("prior") = 0., py::arg("reverse") = false);
    m.def("cmp", [](const PyCSparseMatrix &lhs, const PyCSparseMatrix &rhs, py::object msr, py::object betaprior) {
        if(lhs.data_t_ != rhs.data_t_ || lhs.indices_t_ != rhs.indices_t_ || lhs.indptr_t_ != rhs.indptr_t_) {
            std::string lmsg = std::string("lhs ") + lhs.data_t_ + "," + lhs.indices_t_ + "," + lhs.indptr_t_;
            std::string rmsg = std::string("rhs ") + rhs.data_t_ + "," + rhs.indices_t_ + "," + rhs.indptr_t_;
            throw std::invalid_argument(std::string("mismatched types: ") + lmsg + rmsg);
        }
        const double priorv = betaprior.cast<double>(), priorsum = priorv * lhs.columns();
        const auto ms = assure_dm(msr);
        blz::DV<float> lrsums(lhs.rows());
        blz::DV<float> rrsums(lhs.rows());
        blz::DV<double> priorc({priorv});
        if(lhs.columns() != rhs.columns()) throw std::invalid_argument("mismatched # columns");
        lhs.perform([&](const auto &x){lrsums = sum<rowwise>(x);});
        rhs.perform([&](const auto &x){rrsums = sum<rowwise>(x);});
        const py::ssize_t nr = lhs.rows(), nc = rhs.rows();
        py::array ret(py::dtype("f"), std::vector<py::ssize_t>{nr, nc});
        auto retinf = ret.request();
        blz::CustomMatrix<float, unaligned, unpadded, blz::rowMajor> cm((float *)retinf.ptr, nr, nc, nc);
        lhs.perform(rhs, [&](auto &mat, auto &rmat) {
            cm = blz::generate(nr, nc, [&](auto lhid, auto rhid) -> float {
                return cmp::msr_with_prior<float>(ms, row(rmat, rhid), row(mat, lhid), priorc, priorsum, rrsums[rhid], lrsums[lhid]);
            });
        });
        return ret;
    }, py::arg("matrix"), py::arg("data"), py::arg("msr") = 2, py::arg("prior") = 0.);
    m.def("pcmp", [](const PyCSparseMatrix &lhs, py::object msr, py::object betaprior, py::ssize_t use_float) {
        const double priorv = betaprior.cast<double>(), priorsum = priorv * lhs.columns();
        const auto ms = assure_dm(msr);
        blz::DV<float> lrsums(lhs.rows());
        blz::DV<double> priorc({priorv});
        lhs.perform([&](const auto &x){lrsums = sum<rowwise>(x);});
        const py::ssize_t nr = lhs.rows(), nc2 = (nr * (nr - 1)) / 2;
        py::array ret(py::dtype("f"), std::vector<py::ssize_t>{nc2});
        auto retinf = ret.request();
        blz::CustomVector<float, unaligned, unpadded, blz::rowMajor> cm((float *)retinf.ptr, nc2);
        lhs.perform([&](auto &mat) {
            const bool luf = use_float < 0 ? sizeof(typename std::decay_t<decltype(mat)>::ElementType) <= 4: bool(use_float);
            for(py::ssize_t i = 0; i < nr - 1; ++i) {
                auto retoff = &cm[nr * i - (i * (i + 1) / 2)];
                auto lr(row(mat, i));
                OMP_PFOR_DYN
                for(py::ssize_t j = i + 1; j < nr; ++j) {
                    retoff[j - i - 1] = luf ? cmp::msr_with_prior<float>(ms, lr, row(mat, j), priorc, priorsum, lrsums[i], lrsums[j])
                                            : cmp::msr_with_prior<double>(ms, lr, row(mat, j), priorc, priorsum, lrsums[i], lrsums[j]);
                }
            }
        });
        return ret;
    }, py::arg("matrix"), py::arg("msr") = 2, py::arg("prior") = 0., py::arg("use_float") = -1);
    m.def("pcmp", [](py::array mat, py::object msr, py::object betaprior, py::ssize_t use_float) {
        const double priorv = betaprior.cast<double>();
        const auto ms = assure_dm(msr);
        py::buffer_info bi = mat.request();
        py::object cobj = py::none();
        blz::DV<float> lrsums(bi.shape[0]);
        if(bi.shape.size() != 2) throw std::invalid_argument("pcmp expects a 2-d numpy matrix");
        void *mptr = nullptr;
        std::vector<py::ssize_t> mshape;
        py::ssize_t m_itemsize;
        std::string m_fmt;
        if(bi.format.front() == 'f') {
            blz::CustomMatrix<float, unaligned, unpadded, blz::rowMajor> cm((float *)bi.ptr, bi.shape[0], bi.shape[1]);
            mptr = bi.ptr; mshape = bi.shape; m_itemsize = bi.itemsize; m_fmt = bi.format;
            lrsums = blz::evaluate(blz::sum<blz::rowwise>(cm));
        } else if(bi.format[0] == 'd') {
            blz::CustomMatrix<double, unaligned, unpadded, blz::rowMajor> cm((double *)bi.ptr, bi.shape[0], bi.shape[1]);
            mptr = bi.ptr; mshape = bi.shape; m_itemsize = bi.itemsize; m_fmt = bi.format;
            lrsums = blz::evaluate(blz::sum<blz::rowwise>(cm));
        } else {
            py::array_t<float, py::array::c_style | py::array::forcecast> cmat(mat);
            py::buffer_info mbi = cmat.request();
            blz::CustomMatrix<float, unaligned, unpadded, blz::rowMajor> cm((float *)mbi.ptr, mbi.shape[0], mbi.shape[1]);
            mptr = mbi.ptr; mshape = mbi.shape; m_itemsize = mbi.itemsize; m_fmt = mbi.format;
            lrsums = blz::evaluate(blz::sum<blz::rowwise>(cm));
            cobj = cmat;
        }
        blz::DV<double> priorc({priorv});
        const py::ssize_t nr = mshape[0], nc = mshape[1], nc2 = (nr * (nr - 1)) / 2;
        const double priorsum = priorv * nc;
        const bool luf = use_float < 0 ? m_itemsize <= 4 : bool(use_float);
        py::array ret(py::dtype(luf ? "f": "d"), std::vector<py::ssize_t>{nc2});
        auto retinf = ret.request();
        blz::CustomVector<float, unaligned, unpadded, blz::rowMajor> cm((float *)retinf.ptr, nc2);
        blz::CustomVector<double, unaligned, unpadded, blz::rowMajor> cmd((double *)retinf.ptr, nc2);
        for(py::ssize_t i = 0; i < nr - 1; ++i) {
            const void *retoff = luf ? (const void *)&cm[nr * i - (i * (i + 1) / 2)]: (const void *)&cmd[nr * i - (i * (i + 1) / 2)];
            const void *lrstart = (const void *)((const uint8_t *)mptr + i * nc * m_itemsize);
            OMP_PFOR_DYN
            for(py::ssize_t j = i + 1; j < nr; ++j) {
                const void *rstart = (const void *)((const uint8_t *)mptr + j * nc * m_itemsize);
                double tmpv;
                auto makec = [&](auto x) {return blz::CustomVector<std::remove_pointer_t<decltype(x)>, unaligned, unpadded>(x, nc);};
                if(m_fmt[0] == 'f') {
                    if(luf) {
                        tmpv = cmp::msr_with_prior<float>(ms, makec((float *)lrstart), makec((float *)rstart), priorc, priorsum, lrsums[i], lrsums[j]);
                    } else {
                        tmpv = cmp::msr_with_prior<double>(ms, makec((float *)lrstart), makec((float *)rstart), priorc, priorsum, lrsums[i], lrsums[j]);
                    }
                } else if(m_fmt[0] == 'd') {
                    if(luf) {
                        tmpv = cmp::msr_with_prior<float>(ms, makec((double *)lrstart), makec((double *)rstart), priorc, priorsum, lrsums[i], lrsums[j]);
                    } else {
                        tmpv = cmp::msr_with_prior<double>(ms, makec((double *)lrstart), makec((double *)rstart), priorc, priorsum, lrsums[i], lrsums[j]);
                    }
                } else {
                    throw std::invalid_argument("m_fmt is not double or float");
                }
                if(luf)
                    ((float *)retoff)[j - i - 1] = tmpv;
                else
                    ((double *)retoff)[j - i - 1] = tmpv;
            }
        }
        return ret;
    }, py::arg("matrix"), py::arg("msr") = 2, py::arg("prior") = 0., py::arg("use_float") = -1);
}
