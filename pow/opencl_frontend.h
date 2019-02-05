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

#pragma once
#include "external_pow.h"
#include "opencl_pow.h"
#include <vector>
#include <string>

namespace beam
{
    class OpenCLFrontend {
    public:
        // loads library
        OpenCLFrontend();

        // unloads library
        ~OpenCLFrontend();

        bool loaded() const { return _impl != nullptr; }

        std::vector<GpuInfo> GetSupportedCards();

        std::unique_ptr<IExternalPOW> create_opencl_solver(const std::vector<int32_t>& devices);

    private:
        class Impl;
        Impl* _impl;
    };
}