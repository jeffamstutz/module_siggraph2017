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

#undef NDEBUG

// ospray
#include "BlurInstance.h"
#include "common/Model.h"
// ispc exports
#include "BlurInstance_ispc.h"

namespace ospray {

  BlurInstance::BlurInstance()
  {
    this->ispcEquivalent = ispc::BlurInstanceGeometry_create(this);
  }

  std::string BlurInstance::toString() const
  {
    return "ospray::BlurInstance";
  }

  void BlurInstance::finalize(Model *model)
  {
    xfmData = getParamData("xfm");
    if (xfmData)
    {
      switch (xfmData->type) {
      case OSP_FLOAT:   numTimeSteps = xfmData->size() / 12; break;
      case OSP_FLOAT3:  numTimeSteps = xfmData->size() / 4;  break;
      default:
        throw std::runtime_error("unsupported trianglemesh.vertex data type");
      }

      xfms = (const AffineSpace3f*)xfmData->data;
      xfm = xfms[0];
    }
    else
    {
      xfm.l.vx = getParam3f("xfm.l.vx",vec3f(1.f,0.f,0.f));
      xfm.l.vy = getParam3f("xfm.l.vy",vec3f(0.f,1.f,0.f));
      xfm.l.vz = getParam3f("xfm.l.vz",vec3f(0.f,0.f,1.f));
      xfm.p   = getParam3f("xfm.p",vec3f(0.f,0.f,0.f));

      xfms = &xfm;
      numTimeSteps = 1;
    }

    instancedScene = (Model *)getParamObject("model", nullptr);
    assert(instancedScene);

    if (!instancedScene->embreeSceneHandle) {
      instancedScene->commit();
    }

    embreeGeomID = rtcNewBlurInstance2(model->embreeSceneHandle,
                                   instancedScene->embreeSceneHandle,
                                   numTimeSteps);

    const box3f b = instancedScene->bounds;
    if (b.empty()) {
      // for now, let's just issue a warning since not all ospray
      // geometries do properly set the boudning box yet. as soon as
      // this gets fixed we will actually switch to reporting an error
      static WarnOnce warning("creating an instance to a model that does not"
                              " have a valid bounding box. epsilons for"
                              " ray offsets may be wrong");
    }
    const vec3f v000(b.lower.x,b.lower.y,b.lower.z);
    const vec3f v001(b.upper.x,b.lower.y,b.lower.z);
    const vec3f v010(b.lower.x,b.upper.y,b.lower.z);
    const vec3f v011(b.upper.x,b.upper.y,b.lower.z);
    const vec3f v100(b.lower.x,b.lower.y,b.upper.z);
    const vec3f v101(b.upper.x,b.lower.y,b.upper.z);
    const vec3f v110(b.lower.x,b.upper.y,b.upper.z);
    const vec3f v111(b.upper.x,b.upper.y,b.upper.z);

    bounds = empty;

    for (int t = 0; t < numTimeSteps; t++)
    {
      bounds.extend(xfmPoint(xfms[t],v000));
      bounds.extend(xfmPoint(xfms[t],v001));
      bounds.extend(xfmPoint(xfms[t],v010));
      bounds.extend(xfmPoint(xfms[t],v011));
      bounds.extend(xfmPoint(xfms[t],v100));
      bounds.extend(xfmPoint(xfms[t],v101));
      bounds.extend(xfmPoint(xfms[t],v110));
      bounds.extend(xfmPoint(xfms[t],v111));

      rtcSetTransform2(model->embreeSceneHandle,embreeGeomID,
                       RTC_MATRIX_COLUMN_MAJOR,
                       (const float *)&xfms[t], t);
    }

    AffineSpace3f rcp_xfm = rcp(xfm);
    areaPDF.resize(instancedScene->geometry.size());
    ispc::BlurInstanceGeometry_set(getIE(),
                               (ispc::AffineSpace3f&)xfm,
                               (ispc::AffineSpace3f&)rcp_xfm,
                               (ispc::AffineSpace3f*)xfms,
                               numTimeSteps,
                               instancedScene->getIE(),
                               &areaPDF[0]);
  }

  OSP_REGISTER_GEOMETRY(BlurInstance, blur_instance);

} // ::ospray
