#include "minicore/dist/applicator.h"
#include "minicore/util/csc.h"
#include "minicore/util/timer.h"
#include <getopt.h>
#include "blaze/util/Serialization.h"

#ifndef FT
#define FT double
#endif

#ifndef FLOAT_TYPE
#define FLOAT_TYPE FT
#endif

#ifndef INDICESTYPE
#define INDICESTYPE uint64_t
#endif
#ifndef INDPTRTYPE
#define INDPTRTYPE uint64_t
#endif
#ifndef DATATYPE
#define DATATYPE uint32_t
#endif

void usage(char *s) {
    std::fprintf(stderr, "Usage: %s <flags> <input=\"\"> <output>\n-r\tMax nrows [50000]\n-c\tmin count [50]\n-k\tk [50]\n-m\tkmc2 chain length [100]\n",
                 s);
    std::exit(1);
}

int main(int argc, char *argv[]) {
    unsigned maxnrows = 50000, mincount = 50, k = 50, m = 100;
    unsigned fmt = 0;
    for(int c;(c = getopt(argc, argv, "r:c:k:m:bMh?")) >= 0;) {
        switch(c) {
            case 'r': maxnrows = std::atoi(optarg); break;
            case 'c': mincount = std::atoi(optarg); break;
            case 'k': k        = std::atoi(optarg); break;
            case 'm': m        = std::atoi(optarg); break;
            case 'b': fmt = 1; break;
            case 'M': fmt = 2; break;
            case 'h': case '?': default:
                usage(argv[0]);
        }
    }
    std::string output = "/dev/stdout", input = "/dev/stdin";
    if(argc > optind) {
        input = argv[optind];
        if(argc > optind + 1) {
            output = argv[optind + 1];
        }
    } else usage(argv[0]);
    std::ofstream ofs(output);
    blz::DM<FT> sparsemat;
    if(fmt == 0) {
        sparsemat = minicore::csc2sparse<FLOAT_TYPE, INDPTRTYPE, INDICESTYPE, DATATYPE>(input, true);
    } else if(fmt == 1) {
        blaze::Archive<std::ifstream> arch(input);
        arch >> sparsemat;
    } else if(fmt == 2) {
        sparsemat = minicore::mtx2sparse<FT>(input);
    }
    std::fprintf(stderr, "gathering rows: %u\n", maxnrows);
    std::vector<unsigned> nonemptyrows;
    size_t i = 0;
    while(nonemptyrows.size() < maxnrows && i < sparsemat.rows()) {
        const auto nzc = blz::nonZeros(row(sparsemat, i));
        if(nzc >= mincount)
            nonemptyrows.push_back(i);
        else
            std::fprintf(stderr, "nzc of %u < min %u\n", unsigned(nzc), mincount);
        ++i;
    }
    std::fprintf(stderr, "Gathered %zu rows, with %zu columns\n", nonemptyrows.size(), sparsemat.columns());
#if 0
    blaze::DynamicMatrix<typename decltype(sparsemat)::ElementType> filtered_sparsemat = rows(sparsemat, nonemptyrows.data(), nonemptyrows.size());
#else
    decltype(sparsemat) filtered_sparsemat = rows(sparsemat, nonemptyrows.data(), nonemptyrows.size());
#endif
    auto full_jsm = minicore::jsd::make_probdiv_applicator(filtered_sparsemat, /*dis=*/minicore::distance::JSD, /*prior=*/minicore::distance::DIRICHLET);
    minicore::util::Timer timer("k-means");
    auto kmppdat = minicore::jsd::make_kmeanspp(full_jsm, k);
    timer.report();
    std::fprintf(stderr, "finished kmpp. Now getting cost\n");
    const auto &dat = std::get<2>(kmppdat);
    std::fprintf(stderr, "kmpp solution cost: %g. min/max/mean: %g/%g/%g\n", blz::sum(dat), blz::min(dat), blz::max(dat), blz::mean(dat));
    timer.restart("kmc");
    auto kmcdat = minicore::jsd::make_kmc2(full_jsm, k, m);
    timer.report();
    std::fprintf(stderr, "finished kmc2\n");
    timer.reset();
    auto kmc2cost = minicore::coresets::get_oracle_costs(full_jsm, full_jsm.size(), kmcdat);
    std::fprintf(stderr, "kmc2 solution cost: %g\n", blz::sum(kmc2cost.second));
    std::fprintf(stderr, "\n\nNumber of cells: %zu\n", nonemptyrows.size());
    auto &[centeridx, asn, costs] = kmppdat;
    std::vector<float> counts(centeridx.size());
    blaze::DynamicMatrix<typename decltype(filtered_sparsemat)::ElementType> centers(rows(filtered_sparsemat, centeridx.data(), centeridx.size()));
    auto oracle = [](const auto &x, const auto &y) {
        return minicore::jsd::multinomial_jsd(x, y);
    };
    std::fprintf(stderr, "About to start ll\n");
#if 0
    minicore::coresets::lloyd_loop(asn, counts, centers, filtered_sparsemat, 0., 50, oracle);
    std::fprintf(stderr, "About to make probdiv appl\n");
    filtered_sparsemat = rows(sparsemat, nonemptyrows.data(), nonemptyrows.size());
    auto full_jsd = minicore::jsd::make_probdiv_applicator(filtered_sparsemat, minicore::jsd::MKL, blz::distance::NONE);
    std::tie(centeridx, asn, costs) = minicore::jsd::make_kmeanspp(full_jsd, k);
    centers = rows(filtered_sparsemat, centeridx.data(), centeridx.size());
    minicore::coresets::lloyd_loop(asn, counts, centers, filtered_sparsemat, 1e-6, 250, [](const auto &x, const auto &y) {return minicore::jsd::multinomial_jsd(x, y);});
    auto coreset_sampler = minicore::jsd::make_d2_coreset_sampler(full_jsm, k, 13);
double mb_lloyd_loop(std::vector<IT> &assignments, std::vector<WFT> &counts,
                     CMatrixType &centers, MatrixType &data,
                     unsigned batch_size,
                     size_t maxiter=10000,
                     const Functor &func=Functor(),
                     uint64_t seed=137,
                     const WFT *weights=nullptr)
{
#else
#if 1
    minicore::coresets::lloyd_loop(asn, counts, centers, filtered_sparsemat, 1e-6, 50, oracle);
#else
    minicore::coresets::lloyd_loop(asn, counts, centers, filtered_sparsemat, 1e-6, 50, app);
#endif
    auto cpyasn = asn;
    auto cpycounts = counts;
    auto cpycenters = centers;
    std::mt19937_64 mt(1337);
    minicore::coresets::mb_lloyd_loop(cpyasn, cpycounts, cpycenters, filtered_sparsemat, 500, 50, oracle, mt());
    std::fprintf(stderr, "About to make probdiv appl\n");
    auto full_jsd = minicore::jsd::make_probdiv_applicator(filtered_sparsemat, minicore::jsd::MKL);
    std::tie(centeridx, asn, costs) = minicore::jsd::make_kmeanspp(full_jsd, k);
    centers = rows(filtered_sparsemat, centeridx.data(), centeridx.size());
    minicore::coresets::lloyd_loop(asn, counts, centers, filtered_sparsemat, 1e-6, 250, [](const auto &x, const auto &y) {return minicore::jsd::multinomial_jsd(x, y);});
    //auto coreset_sampler = minicore::jsd::make_d2_coreset_sampler(full_jsm, k, 13);
#endif
}
