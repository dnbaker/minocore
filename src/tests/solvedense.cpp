#undef NDEBUG
#include "include/minicore/clustering/solve.h"
#include "blaze/util/Serialization.h"
//#include "src/tests/solvetestdata.cpp"


namespace clust = minicore::clustering;
using namespace minicore;

#define FLOAT_TYPE float
#define OTHER_FLOAT_TYPE double

#ifdef USE_DOUBLES
#undef FLOAT_TYPE
#undef OTHER_FLOAT_TYPE
#define FLOAT_TYPE double
#define OTHER_FLOAT_TYPE float
#endif

int main(int argc, char *argv[]) {
    int NUMITER = 100;
    if(const char *s = std::getenv("NUMITER")) {
        NUMITER = std::atoi(s) > 0 ? std::atoi(s): 1;
    }
    blz::DM<FLOAT_TYPE> x;
    const bool with_replacement = std::getenv("WITHREPS");
    //const bool use_importance_sampling = std::getenv("USE_IMPORTANCE_SAMPLING");
    std::srand(0);
    std::ios_base::sync_with_stdio(false);
    int nthreads = 1;
    unsigned int k = 10;
    //FLOAT_TYPE temp = 1.;
    dist::DissimilarityMeasure msr = dist::MKL;
    blz::DV<FLOAT_TYPE> prior{FLOAT_TYPE(1)};
    bool loaded_blaze = false;
    wy::WyRand<uint64_t> rng(13);
    bool skip_empty = false, transpose = true;
    for(int c;(c = getopt(argc, argv, "M:z:m:p:P:k:TEh?")) >= 0;) {switch(c) {
        case 'T': transpose = false; break;
        case 'm': msr = (dist::DissimilarityMeasure)std::atoi(optarg); break;
        case 'P': prior[0] = std::atof(optarg); break;
        case 'p': nthreads = std::atoi(optarg); break;
        case 'k': k = std::atoi(optarg); break;
        case 'E': skip_empty = true; break;
#if 0
        case 'C': {
            x = minicore::util::csc2sparse<FLOAT_TYPE>(optarg, skip_empty); break;
        }
        case 'M': {
            x = minicore::util::mtx2sparse<FLOAT_TYPE>(optarg, transpose); break;
        }
        case 'z': {
            blaze::Archive<std::ifstream> arch(optarg);
            x = blaze::CompressedMatrix<FLOAT_TYPE>();
            try {
                arch >> x;
                std::fprintf(stderr, "Shape of loaded blaze matrix: %zu/%zu\n", x.rows(), x.columns());
            } catch(const std::runtime_error &ex) {
                blaze::CompressedMatrix<OTHER_FLOAT_TYPE> cm;
                try {
                arch >> cm;
                x = cm;
                } catch(...) {
                    std::fprintf(stderr, "Could not get x from arch using >>. Error msg: %s\n", ex.what());
                    throw;
                }
            } catch(const std::exception &ex) {
                std::fprintf(stderr, "unknown failure. msg: %s\n", ex.what());
                throw;
            }
            loaded_blaze = true;
            break;
        }
#endif
        case '?':
        case 'h':dist::print_measures();
                std::fprintf(stderr, "Usage: %s <flags> \n-z: load blaze matrix from path\n-P: set prior (1.)\n-T set temp [1.]\n-p set num threads\n-m Set measure (MKL, 5)\n-k: set k [10]\t-T transpose mtx file\t-M parse mtx file from argument\n", *argv);
                return EXIT_FAILURE;
    }}
    int nrows = 50, ncols = 5;
    if(char *s = std::getenv("NCOLS")) {
        ncols = std::atoi(s);
        if(ncols == 0) ncols = 5;
    }
    if(char *s = std::getenv("NROWS")) {
        nrows = std::atoi(s);
        if(nrows == 0) nrows = 50;
    }
    if(!x.rows() && !x.columns()) {
        x.resize(nrows, ncols);
        for(size_t xi = 0; xi < nrows; ++xi)
            for(size_t y = 0; y < ncols; ++y)
                { wy::WyRand<uint64_t> mt((uint64_t(xi) << 32) | y); x(xi, y) = std::uniform_real_distribution<double>()(mt) * (xi + 1);}
    }
    OMP_ONLY(omp_set_num_threads(nthreads);)
    if(std::find_if(argv, argc + argv, [](auto x) {return std::strcmp(x, "-h") == 0;}) != argc + argv) {
        dist::print_measures();
        std::exit(1);
    }
    const size_t nr = x.rows(), nc = x.columns();
    std::fprintf(stderr, "prior: %g\n", prior[0]);
    std::fprintf(stderr, "msr: %d/%s\n", (int)msr, dist::msr2str(msr));
    std::vector<blaze::DynamicVector<FLOAT_TYPE, blaze::rowVector>> centers;
    std::vector<blaze::DynamicVector<FLOAT_TYPE, blaze::rowVector>> ocenters;
    const FLOAT_TYPE psum = prior[0] * nc;
    blz::DV<FLOAT_TYPE> rowsums = blaze::sum<blz::rowwise>(x);
    blz::DV<FLOAT_TYPE> centersums(k);
    blz::DV<FLOAT_TYPE> hardcosts;
    blz::DV<uint32_t> asn(nr);
    std::vector<uint64_t> ids;
    blz::DM<FLOAT_TYPE> complete_hardcost;
    if(loaded_blaze == true) {
        auto fp = x.rows() <= 0xFFFFFFFFu ? size_t(std::rand() % x.rows()): size_t(((uint64_t(std::rand()) << 32) | std::rand()) % x.rows());
        ids = {fp};
        centers.emplace_back(row(x, fp));
        centersums[0] = blz::sum(centers[0]);
        hardcosts.resize(nr);
        for(size_t i = 0; i < nr; ++i) hardcosts[i] = cmp::msr_with_prior<FLOAT_TYPE>(msr, row(x, i, blz::unchecked), centers[0], prior, psum, rowsums[i], centersums[0]);
        while(centers.size() < k) {
            size_t index = reservoir_simd::sample(hardcosts.data(), nr, rng());
            std::fprintf(stderr, "Selected point %zu with cost %g\n", index, hardcosts[index]);
            const auto cid = centers.size();
            hardcosts[index] = 0;
            centers.emplace_back(row(x, index));
            centersums[cid] = rowsums[index];
            for(size_t id = 0; id < nr; ++id) {
                if(id == index) {
                    asn[id] = cid; hardcosts[id] = 0.;
                } else {
                    auto v = cmp::msr_with_prior<FLOAT_TYPE>(msr, row(x, id, blz::unchecked), centers[cid], prior, psum, rowsums[id], centersums[cid]);
                    if(v < hardcosts[id]) hardcosts[id] = v, asn[id] = cid;
                }
            }
        }
        complete_hardcost.resize(nr, k);
        for(size_t i = 0; i < nr; ++i)
            for(size_t j = 0; j < k; ++j)
                complete_hardcost(i, j) = cmp::msr_with_prior<FLOAT_TYPE>(msr, row(x, i, blz::unchecked), centers[j], prior, psum, rowsums[i], centersums[j]);
        //assert(blaze::min<blaze::rowwise>(complete_hardcost) ==
    } else {
        while(ids.size() < k) {
            auto rid = std::rand() % x.rows();
            if(std::find(ids.begin(), ids.end(), rid) == ids.end())
                ids.emplace_back(rid);
        }
        for(const auto id: ids) {
            centers.emplace_back(row(x, id));
        }
        centersums.resize(centers.size());
        for(size_t i = 0; i < centers.size(); ++i) centersums[i] = blz::sum(centers[i]);
        complete_hardcost.resize(nr, k);
        for(size_t i = 0; i < nr; ++i) {
            auto r(row(complete_hardcost, i));
            assert(r.size() == k);
            const auto rs = rowsums[i];
            OMP_PFOR
            for(size_t j = 0; j < k; ++j)
                r[j] = cmp::msr_with_prior<FLOAT_TYPE>(msr, row(x, i), centers[j], prior, psum, rs, centersums[j]);
        }
        std::cerr << "full costs range: " << min(complete_hardcost) << " -> " << max(complete_hardcost) << '\n';
        hardcosts.resize(nr);
        for(size_t i = 0; i < nr; ++i) {
            auto r = row(complete_hardcost, i, blaze::unchecked);
            auto it = std::min_element(r.begin(), r.end());
            asn[i] = it - r.begin();
            hardcosts[i] = *it;
        }
        int cid = 0;
        for(const auto id: ids) {
            complete_hardcost(id, cid) = 0.;
            asn[id] = cid++;
        }
    }
    while(ocenters.size() < centers.size()) ocenters.emplace_back(centers[ocenters.size()]);
    assert(rowsums.size() == x.rows());
    assert(centersums.size() == centers.size());
    auto mnc = blz::min(hardcosts);
    std::fprintf(stderr, "Total cost: %g. max cost: %g. min cost: %g. mean cost:%g\n", blz::sum(hardcosts), blz::max(hardcosts), mnc, blz::mean(hardcosts));
    std::vector<uint32_t> counts(k);
    for(const auto v: asn) ++counts[v];
    for(unsigned i = 0; i < k; ++i) {
        std::fprintf(stderr, "Center %d with sum %g has %u supporting, with total cost of assigned points %g\n", i, blz::sum(centers[i]), counts[i],
                     blz::sum(blz::generate(nr, [&](auto id) { return asn[id] == i ? hardcosts[id]: 0.;})));
    }
    assert(min(asn) == 0);
    assert(max(asn) == centers.size() - 1);
    clust::perform_hard_minibatch_clustering(x, msr, prior, centers, asn, hardcosts, (std::vector<FLOAT_TYPE> *)nullptr, 500, 10, 10, 1, /*with_replacement=*/1, /*seed=*/rng());
    auto t1 = std::chrono::high_resolution_clock::now();
    clust::perform_hard_clustering(x, msr, prior, centers, asn, hardcosts);
    auto t2 = std::chrono::high_resolution_clock::now();
    std::fprintf(stderr, "Wall time for clustering: %gms\n", std::chrono::duration<FLOAT_TYPE, std::milli>(t2 - t1).count());
}
