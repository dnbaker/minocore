#define BLAZE_USE_SLEEF 1
#include "minicore/util/macros.h"
#include "libsimdsampling/simdsampling.ho.h"
#include "sleef.h"
#include "blaze/Math.h"
#include "aesctr/wy.h"
#include <chrono>
#include <x86intrin.h>
#include <cstdint>
#include <getopt.h>
#include <thread>
using std::uint64_t;

auto gett() {return std::chrono::high_resolution_clock::now();}

int usage() {
    std::fprintf(stderr, "Usage: parvecsample <flags>\nFlags:\n"
                         "-r: Number of rows. Default: 100000\n"
                         "-d: Number of dimensions of generated data. Default: 100\n"
                         "-p: Number of threads to use. Default: OMP_NUM_THREADS if set, 1 otherwise; If set to 0, use all hardware threads.\n"
                         "-h: Emit usage and exit.\n");
    return EXIT_FAILURE;
}

#ifndef FT
#define FT float
#endif

int main(int argc, char **argv) {
    size_t nr = 100000;
    size_t nd = 100;
    int numthreads = -1;
    for(int c;(c = getopt(argc, argv, "r:d:p:h?")) >= 0;) {
    switch(c) {
        case 'r': nr = std::strtoull(optarg, nullptr, 10); break;
        case 'd': nd = std::strtoull(optarg, nullptr, 10); break;
        case 'p': numthreads = std::atoi(optarg); break;
        case 'h': case '?': default: return usage();
    }
    }
    if(numthreads < 0) {
        if(const char *env_p = std::getenv("OMP_NUM_THREADS"))
            numthreads = std::atoi(env_p);
        else
            numthreads = 1;
    } else if(numthreads == 0) {
        numthreads = std::thread::hardware_concurrency();
    }
    OMP_ONLY(omp_set_num_threads(numthreads));
    FT rmi = 1. / RAND_MAX;
    blaze::DynamicMatrix<FT> mat = blaze::generate(nr, nd, [rmi](auto, auto) {return std::rand() * rmi;});
    auto start = gett();
    blaze::DynamicVector<FT> cdf(nr);
    wy::WyRand<uint64_t> rng;
    std::vector<uint32_t> ind1, ind2, ind3;
    std::cauchy_distribution<FT> dist;
    //blaze::DynamicVector<FT> weights = blaze::generate(nr, [&](auto x) {return std::abs(dist(rng));});
    blaze::DynamicVector<FT> vals(nr);
    blaze::DynamicMatrix<FT> v = blaze::generate(nd, nr, [&](auto x, auto y) {return blaze::l1Norm(row(mat, y) - row(mat, x, blaze::unchecked));});

    // Pass through the data once to ensure we aren't counting cache coherence
    blaze::DynamicVector<FT> rsums = blaze::sum<blaze::rowwise>(v);

    // Now time partial sum usage
    for(size_t i = 0; i < v.rows(); ++i) {
        std::partial_sum(v.begin(i), v.end(i), cdf.begin());
        auto ind = std::lower_bound(cdf.begin(), cdf.end(), cdf[nd - 1] * std::uniform_real_distribution<FT>()(rng)) - cdf.begin();
        ind1.push_back(ind);
    }
    auto stop = gett();
    long long unsigned t1 = (stop - start).count();
    std::fprintf(stderr, "Compute + cdf time: %llu\n", t1);
    //long long dgt = 0, dgt0 = 0, dgt1 = 0;
    start = gett();
    for(size_t i = 0; i < v.rows(); ++i) {
        vals = blaze::generate(nr, [](auto x) {wy::WyRand<uint64_t> rng(x); return std::uniform_real_distribution<FT>()(rng);});
        //vals = log(vals) / (row(v, i, blaze::unchecked) * weights);
        vals = log(vals) / trans(row(v, i, blaze::unchecked));
        uint32_t bestind = 0;
        FT mv = vals[0];
        #pragma omp parallel for
        for(size_t i = 1; i < vals.size(); ++i) {
            if(vals[i] > mv) {
                #pragma omp critical
                {
                    if(vals[i] > mv) {bestind = i, mv = vals[i];}
                }
            }
        }
        ind2.push_back(bestind);
    }
    stop = gett();
#if GTTIMERS
    std::fprintf(stderr, "Times of %lld, %lld, %lld (%g, %g, %g of max)\n", dgt, dgt0, dgt1, FT(dgt) / mx, FT(dgt0) / mx, FT(dgt1) / mx);
#endif
    long long unsigned t2 = (stop - start).count();
    FT ratio = FT(t1) / t2;
    std::fprintf(stderr, "Generate + log + div + max: %llu; Some blaze used, some manual parallelization. ratio: %g\n", t2, ratio);
    start = gett();
    auto func = [](auto x) {wy::WyRand<uint64_t> rng(x); return std::uniform_real_distribution<FT>()(rng);};
    for(size_t i = 0; i < v.rows(); ++i) {
        uint32_t bestind = argmax(log(blaze::generate(v.columns(), func)) / trans(row(v, i, blaze::unchecked)));
        ind3.push_back(bestind);
    }
    stop = gett();
#if GTTIMERS
    auto mx = std::max(std::max(dgt, dgt0), dgt1);
    std::fprintf(stderr, "Times of %lld, %lld, %lld (%g, %g, %g of max)\n", dgt, dgt0, dgt1, FT(dgt) / mx, FT(dgt0) / mx, FT(dgt1) / mx);
#endif
    long long unsigned t3 = (stop - start).count();
    FT ratio3 = FT(t1) / t3;
    std::fprintf(stderr, "argmax from log/generate in a single pass: %llu [no vectorization enabled] ratio: %g\n", t3, ratio3);
    start = gett();
    for(size_t i = 0; i < v.rows(); ++i) {
        uint32_t bestind = reservoir_simd::sample(vals, i, WITH_REPLACEMENT);
        ind2.push_back(bestind);
    }
    stop = gett();
    long long unsigned t4 = (stop - start).count();
    FT ratio4 = FT(t1) / t4;
    std::fprintf(stderr, "argmax from manual avx2, plus serial scan for best value: %llu. ratio: %g\n", t4, ratio4);
}
