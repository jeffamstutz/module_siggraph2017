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

// ospray
#include "math/vec.ih"
#include "math/box.ih"
#include "common/Ray.ih"
#include "BlurInstance.ih"

static inline AffineSpace3f
calculate_interpolated_space(uniform BlurInstance* uniform instance, float gtime)
{
  /* calculate time segment itime and fractional time ftime */
  const int time_segments = instance->numTimeSteps-1;
  const float time = gtime*(float)(time_segments);
  const int itime = clamp((int)(floor(time)),(varying int)0,time_segments-1);
  const float ftime = time - (float)(itime);
  return (1.0f-ftime)*(instance->xfms[itime+0]) + ftime*(instance->xfms[itime+1]);
}

static void BlurInstance_postIntersect(uniform Geometry *uniform _self,
                                   uniform Model *uniform parentModel,
                                   varying DifferentialGeometry &dg,
                                   const varying Ray &ray,
                                   uniform int64 flags)
{
  uniform BlurInstance *uniform self = (uniform BlurInstance *uniform)_self;
  uniform Model *uniform instancedModel =
    (uniform Model *uniform)self->model;
  foreach_unique(geomID in ray.geomID) {
    uniform Geometry *uniform instancedGeometry =
      (uniform Geometry *uniform)instancedModel->geometry[geomID];

    dg.geometry = instancedGeometry;
    dg.material = instancedGeometry->material;

    instancedGeometry->postIntersect(instancedGeometry,instancedModel,
                                     dg,ray,flags);
  }

  AffineSpace3f xfm, rcp_xfm;
  if (self->numTimeSteps == 1)
  {
    xfm = self->xfm;
    rcp_xfm = self->rcp_xfm;
  }
  else
  {
    xfm = calculate_interpolated_space(self, ray.time);
    rcp_xfm = rcp(xfm);
  }

  dg.Ns = xfmVector(transposed(self->rcp_xfm.l), dg.Ns);
  dg.Ng = xfmVector(transposed(self->rcp_xfm.l), dg.Ng);

  if (flags & DG_TANGENTS) {
    dg.dPds = xfmVector(self->xfm,dg.dPds);
    dg.dPdt = xfmVector(self->xfm,dg.dPdt);
  }
}

export void *uniform BlurInstanceGeometry_create(void *uniform cppE)
{
  BlurInstance *uniform self = uniform new BlurInstance;
  self->geometry.cppEquivalent = cppE;
  self->geometry.postIntersect = BlurInstance_postIntersect;
  self->areaPDF = NULL;

  return self;
}

export void BlurInstanceGeometry_set(void *uniform _self,
                                 const uniform AffineSpace3f &xfm,
                                 const uniform AffineSpace3f &rcp_xfm,
                                 uniform AffineSpace3f *uniform xfms,
                                 uniform int numTimeSteps,
                                 void *uniform _model,
                                 float *uniform areaPDF)
{
  BlurInstance *uniform self = (BlurInstance *uniform)_self;
  self->model   = (uniform Model *uniform)_model;
  self->xfm     = xfm;
  self->rcp_xfm = rcp_xfm;
  self->xfms    = xfms;
  self->numTimeSteps = numTimeSteps;
  self->areaPDF = areaPDF;
}
