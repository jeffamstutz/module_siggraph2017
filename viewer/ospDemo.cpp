// ======================================================================== //
// Copyright 2009-2016 Intel Corporation                                    //
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

#include "ospray/ospray_cpp/Device.h"
#include "ospray/ospray_cpp/FrameBuffer.h"
#include "ospray/ospray_cpp/Renderer.h"
#include "common/commandline/Utility.h"
#include "ospcommon/networking/Socket.h"

#include "widgets/imguiViewer.h"

#include "common/commandline/SceneParser/demo/DemoSceneParser.h"

namespace exampleViewer {

  using namespace commandline;

  ospcommon::vec3f translate;
  ospcommon::vec3f scale;
  bool lockFirstFrame = false;
  bool fullscreen = false;

  void parseExtraParametersFromComandLine(int ac, const char **&av)
  {
    for (int i = 1; i < ac; i++) {
      const std::string arg = av[i];
      if (arg == "--translate") {
        translate.x = atof(av[++i]);
        translate.y = atof(av[++i]);
        translate.z = atof(av[++i]);
      } else if (arg == "--scale") {
        scale.x = atof(av[++i]);
        scale.y = atof(av[++i]);
        scale.z = atof(av[++i]);
      } else if (arg == "--lockFirstFrame") {
        lockFirstFrame = true;
      } else if (arg == "--fullscreen") {
        fullscreen = true;
      }
    }
  }

  extern "C" int main(int ac, const char **av)
  {
    int init_error = ospInit(&ac, av);
    if (init_error != OSP_NO_ERROR) {
      std::cerr << "FATAL ERROR DURING INITIALIZATION!" << std::endl;
      return init_error;
    }

    auto device = ospGetCurrentDevice();
    ospDeviceSetStatusFunc(device,
                           [](const char *msg) { std::cout << msg; });

    ospDeviceSetErrorFunc(device,
                          [](OSPError e, const char *msg) {
                            std::cout << "OSPRAY ERROR [" << e << "]: "
                                      << msg << std::endl;
                            std::exit(1);
                          });

    ospray::imgui3D::init(&ac,av);

    auto ospObjs = parseCommandLine<DefaultRendererParser, DefaultCameraParser,
      DemoSceneParser, DefaultLightsParser>(ac, av);

    std::deque<ospcommon::box3f>   bbox;
    std::deque<ospray::cpp::Model> model;
    ospray::cpp::Renderer renderer;
    ospray::cpp::Renderer rendererDW;
    ospray::cpp::Camera   camera;
    ospray::cpp::FrameBuffer frameBufferDW;

    std::tie(bbox, model, renderer, camera) = ospObjs;

    parseExtraParametersFromComandLine(ac, av);

    ospray::ImGuiViewer window(bbox, model, renderer, camera);
    window.setScale(scale);
    window.setLockFirstAnimationFrame(lockFirstFrame);
    window.setTranslation(translate);
    window.create("OSPRay Demo", fullscreen);

    ospray::imgui3D::run();
    return 0;
  }

}
