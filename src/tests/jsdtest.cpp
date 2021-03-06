#include "minicore/dist/applicator.h"
#include "minicore/utility.h"
using namespace minicore;
using namespace blz;

#ifndef FLOAT_TYPE
#define FLOAT_TYPE double
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

int main(int argc, char *argv[]) {
    if(std::find_if(argv, argv + argc, [](auto x) {return std::strcmp(x, "-h") == 0 || std::strcmp(x, "--help") == 0;})
       != argv + argc) {
        std::fprintf(stderr, "Usage: %s <max rows[1000]> <mincount[50]>\n", *argv);
        std::exit(1);
    }
    unsigned maxnrows = argc == 1 ? 1000: std::atoi(argv[1]);
    unsigned mincount = argc <= 2 ? 50: std::atoi(argv[2]);
    std::string input;
    if(argc > 2)
        input = argv[3];
    std::ofstream ofs("output.txt");
    auto sparsemat = input.size() ? minicore::mtx2sparse<FLOAT_TYPE>(input)
                                  : minicore::csc2sparse<FLOAT_TYPE, INDPTRTYPE, INDICESTYPE, DATATYPE>("", true);
    std::vector<unsigned> nonemptyrows;
    size_t i = 0;
    while(nonemptyrows.size() < 25) {
        if(sum(row(sparsemat, i)) >= mincount)
            nonemptyrows.push_back(i);
        ++i;
    }
    blz::SM<FLOAT_TYPE> first25 = rows(sparsemat, nonemptyrows.data(), nonemptyrows.size());
    auto jsd = minicore::cmp::make_probdiv_applicator(first25, distance::JSM, distance::Prior::DIRICHLET);
    //auto jsddistmat = jsd.make_distance_matrix();
    dm::DistanceMatrix<FLOAT_TYPE> utdm(first25.rows());
    jsd.set_distance_matrix(utdm);
    std::cout << utdm << '\n';
    blz::DynamicMatrix<FLOAT_TYPE> jsd_bnj(first25.rows(), first25.rows(), 0.);
    jsd.set_distance_matrix(jsd_bnj, distance::UWLLR, true);
    ofs << jsd_bnj << '\n' << blz::min(jsd_bnj) << '\n' << blaze::max(jsd_bnj) << '\n';
    std::fprintf(stderr, "min/max UWLLR: %g/%g\n", blz::min(jsd_bnj), blz::max(jsd_bnj));
    jsd.set_distance_matrix(jsd_bnj, distance::RSIS, true);
    ofs << jsd_bnj << '\n' << blz::min(jsd_bnj) << '\n' << blaze::max(jsd_bnj) << '\n';
    std::fprintf(stderr, "min/max RSIS: %g/%g\n", blz::min(jsd_bnj), blz::max(jsd_bnj));
    ofs.flush();
    i = 25;
    while(nonemptyrows.size() < maxnrows && i < sparsemat.rows()) {
        const auto nzc = blz::nonZeros(row(sparsemat, i));
        //std::fprintf(stderr, "nzc: %zu\n", nzc);
        if(nzc > mincount) {
            //std::fprintf(stderr, "nzc: %zu vs min %u\n", nzc, mincount);
            nonemptyrows.push_back(i);
        }
        ++i;
        //std::fprintf(stderr, "sparsemat rows: %zu. current i: %zu\n", sparsemat.rows(), i);
    }
    std::fprintf(stderr, "Gathered %zu rows\n", nonemptyrows.size());
    first25 = rows(sparsemat, nonemptyrows.data(), nonemptyrows.size());
    std::fprintf(stderr, "Assigned to sparse matrix\n");

    jsd_bnj.resize(nonemptyrows.size(), nonemptyrows.size(), false);
    jsd_bnj = 0.;
    std::fprintf(stderr, "Assigned return matrix to 0.\n");
    auto jsd2 = minicore::cmp::make_probdiv_applicator(first25, distance::JSD, distance::Prior::DIRICHLET);
    std::fprintf(stderr, "made probdiv\n");
    auto jsd3 = minicore::cmp::make_probdiv_applicator(first25, distance::L1);
    std::fprintf(stderr, "made l1\n");
    minicore::util::Timer timer("1ksparsejsd");
    std::fprintf(stderr, "calling set_distance_matrix\n");
    jsd2.set_distance_matrix(jsd_bnj, distance::JSD);
    std::fprintf(stderr, "called set_distance_matrix\n");
    timer.report();
    std::fprintf(stderr, "bnj after larger minv: %g. maxv: %g\n", blz::min(jsd_bnj), blz::max(jsd_bnj));
    timer.restart("1ksparseL2");
    jsd2.set_distance_matrix(jsd_bnj, distance::L2);
    timer.report();
    std::fprintf(stderr, "bnj after larger minv: %g. maxv: %g\n", blz::min(jsd_bnj), blz::max(jsd_bnj));
    timer.restart("1ksparsewllr");
    jsd2.set_distance_matrix(jsd_bnj, distance::WLLR, true);
    timer.report();
    timer.restart("1ksparsekl");
    jsd2.set_distance_matrix(jsd_bnj, distance::MKL, true);
    timer.report();
    //std::cout << jsd_bnj << '\n';
    timer.restart("1ksparseL1");
    jsd2.set_distance_matrix(jsd_bnj, distance::L1, true);
    timer.report();
#if 0
    timer.restart("1ldensejsd");
    blz::DM<FLOAT_TYPE> densefirst25 = first25;
    minicore::make_probdiv_applicator(densefirst25).set_distance_matrix(jsd_bnj);
    timer.report();
#endif
    //ofs << "JS Divergence: \n";
    //ofs << jsd_bnj << '\n';
    ofs.flush();
    std::fprintf(stderr, "Starting jsm\n");
    timer.restart("1ksparsetvd");
    jsd2.set_distance_matrix(jsd_bnj, distance::TVD);
    timer.report();
    timer.reset();
    ofs << "JS Metric: \n";
    ofs << jsd_bnj << '\n';
    ofs.flush();
    std::fprintf(stderr, "Starting llr\n");
    timer.restart("1ksparsellr");
    jsd2.set_distance_matrix(jsd_bnj, distance::LLR);
    timer.report();
    timer.reset();
    ofs << "Hicks-Dyjack LLR \n";
    ofs << jsd_bnj << '\n';
    ofs.flush();
    timer.reset();
    std::fprintf(stderr, "\n\nNumber of cells: %zu\n", nonemptyrows.size());
}
