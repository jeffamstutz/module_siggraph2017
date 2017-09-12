## ======================================================================== ##
## Copyright 2009-2017 Intel Corporation                                    ##
##                                                                          ##
## Licensed under the Apache License, Version 2.0 (the "License");          ##
## you may not use this file except in compliance with the License.         ##
## You may obtain a copy of the License at                                  ##
##                                                                          ##
##     http://www.apache.org/licenses/LICENSE-2.0                           ##
##                                                                          ##
## Unless required by applicable law or agreed to in writing, software      ##
## distributed under the License is distributed on an "AS IS" BASIS,        ##
## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. ##
## See the License for the specific language governing permissions and      ##
## limitations under the License.                                           ##
## ======================================================================== ##

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_LIST_DIR})

SET(SOURCE_FILES
  CommandLineExport.h
  CommandLineParser.h
  CameraParser.cpp
  LightsParser.cpp
  RendererParser.cpp

  SceneParser/SceneParser.h

  Utility.h

  SceneParser/demo/DemoSceneParser.cpp
)

OSPRAY_CREATE_LIBRARY(ospray_commandline
  ${SOURCE_FILES}
LINK
  ospray_common
  ospray_minisg
  ospray_xml
  ospray_tfn
)
