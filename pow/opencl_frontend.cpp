// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "opencl_frontend.h"
#include "utility/logger.h"
#include <boost/dll.hpp>
#include <boost/function.hpp>
#include <boost/exception/diagnostic_information.hpp>

namespace beam {

class OpenCLFrontend::Impl {
public:
    Impl() {
        fn_GetSupportedCards = boost::dll::import_alias<std::vector<GpuInfo>()>(OPENCL_POW_LIBRARY_NAME, "GetSupportedCards");
        fn_create_opencl_solver = boost::dll::import_alias<IExternalPOW*(const std::vector<int32_t>&)>(OPENCL_POW_LIBRARY_NAME, "create_opencl_solver");
    }

    ~Impl() = default;

    std::vector<GpuInfo> GetSupportedCards() {
        return fn_GetSupportedCards();
    }

    std::unique_ptr<IExternalPOW> create_opencl_solver(const std::vector<int32_t>& devices) {
        return std::unique_ptr<IExternalPOW>(fn_create_opencl_solver(devices));
    }

private:
    boost::function<std::vector<GpuInfo>()> fn_GetSupportedCards;
    boost::function<IExternalPOW*(const std::vector<int32_t>&)> fn_create_opencl_solver;
};

OpenCLFrontend::OpenCLFrontend() : _impl(nullptr) {
    try {
        _impl = new Impl();
    } catch (const std::exception& e) {
        LOG_ERROR() << OPENCL_POW_LIBRARY_NAME << " " << e.what();
    } catch (const boost::exception& e) {
        LOG_ERROR() << boost::diagnostic_information(e);
    } catch (...) {
        LOG_ERROR() << "unknown exception while loading " << OPENCL_POW_LIBRARY_NAME;
    }
}

OpenCLFrontend::~OpenCLFrontend() {
    if (_impl) delete _impl;
}

std::vector<GpuInfo> OpenCLFrontend::GetSupportedCards() {
    if (_impl) {
        return _impl->GetSupportedCards();
    }
    return std::vector<GpuInfo>();
}

std::unique_ptr<IExternalPOW> OpenCLFrontend::create_opencl_solver(const std::vector<int32_t>& devices) {
    if (_impl) {
        return _impl->create_opencl_solver(devices);
    }
    return std::unique_ptr<IExternalPOW>();
}

} //namespace
