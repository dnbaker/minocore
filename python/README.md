## Minocore in Python

Much of the documentation is in doc-strings/type annotations.
Here, I am trying to provide more of a user guide.

# Coreset generation
We have exposed a subset of functionality to Python. The `CoresetSampler` generates a coreset from a set of costs and a construction method,
while the more involved clustering code is also exposed.

# Clsutering
For clustering Bregman divergences (squared distance, Itakura-Saito, and KL-divergence, for instance), kmeans++ sampling (via `kmeanspp`) provides accurate fast initial
centers, while `cluster_from_centers` performs EM from an initial set of points.

Since we're using the blaze linear algebra library, we need to create a sparse matrix for clustering from CSR format.


Example:
```python
import minocore mc
import numpy as np
import scipy.sparse as sp

data = # array of float32/float64s/int64/int32...
indices = # array of integers of 4 or 8 bytes
indptr = # array of integers of 4 or 8 bytes
shape = # np array or tuple

assert len(data) == len(indices)

mcmat = mc.SparseMatrixWrapper(mc.csr_tuple(data=data, indices=indices, indptr=indptr, shape=shape, nnz=len(data)))

k = 50
beta = 0.01  # Pseudocount prior for Bregman divergences
             # smoothes some functions, and quantifies the "surprise" of having
             # unique features. The smaller, the more surprising.
             # See Witten, 2011

ntimes = 5   # Perform kmeans++ sampling %i times, use the best-scoring set of centers
             # defaults to 1

seed = 0     # if seed is not set, defaults to 0. Results will be identical with the same seed.

measure = "MKL" # See https://github.com/dnbaker/minocore/blob/main/docs/msr.md for examples/integer codes
                # value can be integral or be the short string description
                # MKL = categorical KL divergence 

weights = None  # Set weights to be a 1d numpy array containing weights of type (float32, float64, int, unsigned)
                # If none, unused (uniform)
                # otherwise, weights are used in both sampling and optimizing

centers, assignments, costs = mc.kmeanspp(mcmat, msr=measure, k=k, betaprior=beta, ntimes=ntimes,
                                          seed=seed, weights=weights)


lspprounds = 2 # Perform %i rounds of localsearch++. Yields a higher quality set of centers at the expense of more runtime

# cluster_from_centers(smw: pyfgc.SparseMatrixWrapper, centers: object, betaprior: float = -1.0, msr: object = 5, weights: object = None, eps: float = 1e-10, maxiter: int = 1000, kmcrounds: int = 10000, ntimes: int = 1, lspprounds: int = 1, seed: int = 0)

ctr_rows = mc.rowsel(centers)

res = mc.cluster_from_centers(mcmat, ctr_rows, betaprior=beta, msr=measure,
                              weights=weights, lspprounds=lspprounds, seed=seed)


# res is a dictionary with the following keys:
#{"initcost": initial_cost, "finalcost": final_cost, "numiter": numiter,
# "centers": centers, # in paired sparse array format (data, idx), where idx is integral and data is floating-point
# "costs": costs  # cost for each point in the dataset
# "asn": assignments # integral, determining which center a point is assigned to.
#}
```

For measures that are not Bregman divergences (for which kmeans++ sampling may not work as well),
we can also use some discrete metric solvers for initial sets of points, but these are significantly slower.

We can also try greedy farthest-point sampling for initial centers. This is supported in the `minocore.greedy_select`, which uses a k-center approximation algorithm.

The options for this are governed by the minocore.SumOpts object, which holds basic metadata about a clustering problem.
If you set its `outlier_fraction` field to be nonzero, then this will use a randomized selection technique that is robust
to outliers and can also be used to generate a coreset, if the measure is a doubling metric.


## LSH

We also support a range of LSH functions.