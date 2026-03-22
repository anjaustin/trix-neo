/*
 * trix_python.cpp - Python bindings for TriX runtime
 *
 * Build:
 *   cd build
 *   cmake .. -DPYTHONBindings=ON
 *   make
 *
 * Or manually:
 *   c++ -O3 -shared -std=c++17 -fPIC \
 *       -I$(python3 -c "import sysconfig; print(sysconfig.get_path('include'))") \
 *       -I../../zor/include \
 *       ../../zor/src/runtime.c \
 *       -o trix_cffi.so \
 *       -lstdc++ -lm
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/buffer_info.h>

#include "../include/trixc/runtime.h"
#include <string>
#include <vector>

namespace py = pybind11;

class PyTriXChip {
public:
    PyTriXChip(const std::string& filename) {
        int error = 0;
        chip = trix_load(filename.c_str(), &error);
        if (!chip) {
            throw std::runtime_error("Failed to load chip: " + std::to_string(error));
        }
        
        trix_chip_info_t info;
        if (trix_info(chip, &info) != 0) {
            throw std::runtime_error("Failed to get chip info");
        }
        name = info.name;
        version = info.version;
        state_bits = info.state_bits;
        num_signatures = info.num_signatures;
    }
    
    ~PyTriXChip() {
        if (chip) {
            trix_chip_free(chip);
        }
    }
    
    std::string infer(const std::string& data) {
        if (data.size() < 64) {
            throw std::runtime_error("Input must be at least 64 bytes");
        }
        
        uint8_t input[64];
        memcpy(input, data.data(), 64);
        
        trix_result_t result = trix_infer(chip, input);
        
        if (result.match >= 0) {
            return std::string(result.label) + ":" + std::to_string(result.distance);
        } else {
            return "no_match:" + std::to_string(result.distance);
        }
    }
    
    trix_chip_t* chip;
    std::string name;
    std::string version;
    int state_bits;
    int num_signatures;
};

PYBIND11_MODULE(trix_cffi, m) {
    m.doc() = "TriX Runtime Python Bindings";
    
    py::class_<PyTriXChip>(m, "Chip")
        .def(py::init<const std::string&>())
        .def_readonly("name", &PyTriXChip::name)
        .def_readonly("version", &PyTriXChip::version)
        .def_readonly("state_bits", &PyTriXChip::state_bits)
        .def_readonly("num_signatures", &PyTriXChip::num_signatures)
        .def("infer", &PyTriXChip::infer,
             "Run inference on input data",
             py::arg("data"));
    
    // Module-level functions
    m.def("load", [](const std::string& filename) {
        return std::make_unique<PyTriXChip>(filename);
    }, "Load a .trix chip file");
}