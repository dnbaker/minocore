#pragma once
#include "pybind11/pybind11.h"
#include "pybind11/numpy.h"
#include "aesctr/wy.h"
#include "minocore/minocore.h"
using namespace minocore;
namespace py = pybind11;
void init_smw(py::module &);
void init_merge(py::module &);
void init_coreset(py::module &);
void init_centroid(py::module &);
void init_hashers(py::module &);
void init_omp_helpers(py::module &m);

using CSType = coresets::CoresetSampler<float, uint32_t>;
using FNA =  py::array_t<float, py::array::c_style | py::array::forcecast>;
using DNA =  py::array_t<double, py::array::c_style | py::array::forcecast>;
using INA =  py::array_t<uint32_t, py::array::c_style | py::array::forcecast>;
using SMF = blz::SM<float>;
using SMD = blz::SM<double>;
namespace dist = minocore::distance;
