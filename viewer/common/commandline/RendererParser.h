// ======================================================================== //
// Copyright 2009-2017 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "CommandLineExport.h"
#include "CommandLineParser.h"
#include "ospray/ospray_cpp/Renderer.h"

#include <string>

namespace commandline {
  class OSPRAY_COMMANDLINE_INTERFACE RendererParser : public CommandLineParser
  {
  public:
    virtual ~RendererParser() = default;
    virtual ospray::cpp::Renderer create() = 0;
  };

  class OSPRAY_COMMANDLINE_INTERFACE DefaultRendererParser : public RendererParser
  {
  public:
    DefaultRendererParser() = default;
    bool parse(int ac, const char **&av) override;
    ospray::cpp::Renderer create() override;

  protected:

    std::string           rendererType;
    ospray::cpp::Renderer parsedRenderer;

    int spp{1};
    int maxDepth{5};
    int shadows{1};
    int aoSamples{1};

    float maxRadiance{ospcommon::pos_inf};

    float toneGain{1.0f};
    float toneBurn{0.0f};

  private:

    void finalize();
  };
}
