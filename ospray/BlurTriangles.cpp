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
#include "BlurTriangles.h"
#include "common/Model.h"
#include "../include/ospray/ospray.h"
// ispc exports
#include "BlurTriangles_ispc.h"
#include <cmath>

#define RTC_INVALID_ID RTC_INVALID_GEOMETRY_ID

namespace ospray {

  inline bool inRange(int64 i, int64 i0, int64 i1)
  {
    return i >= i0 && i < i1;
  }

  BlurTriangles::BlurTriangles()
    : eMesh(RTC_INVALID_ID)
  {
    this->ispcMaterialPtrs = nullptr;
    this->ispcEquivalent = ispc::BlurTriangles_create(this);
  }

  std::string BlurTriangles::toString() const
  {
    return "ospray::BlurTriangles";
  }

  void BlurTriangles::finalize(Model *model)
  {
    static int numPrints = 0;
    numPrints++;
    if (numPrints == 5) {
      postStatusMsg(2) << "(all future printouts for triangle mesh creation "
                       << "will be omitted)";
    }

    if (numPrints < 5)
      postStatusMsg(2) << "ospray: finalizing trianglemesh ...";

    Assert(model && "invalid model pointer");

    RTCScene embreeSceneHandle = model->embreeSceneHandle;

    vertexData = getParamData("vertex",getParamData("position"));
    normalData = getParamData("vertex.normal",getParamData("normal"));
    colorData  = getParamData("vertex.color",getParamData("color"));
    texcoordData = getParamData("vertex.texcoord",getParamData("texcoord"));
    indexData  = getParamData("index",getParamData("triangle"));
    prim_materialIDData = getParamData("prim.materialID");
    materialListData = getParamData("materialList");
    geom_materialID = getParam1i("geom.materialID",-1);

    numTimeSteps = getParam1i("vertex.timesteps", getParam1i("positionTimeSteps", 1));
    int numNormalTimeSteps = getParam1i("normal.timesteps", getParam1i("normalTimeSteps", 1));

    if (!vertexData)
      throw std::runtime_error("triangle mesh must have 'vertex' array");
    if (!indexData)
      throw std::runtime_error("triangle mesh must have 'index' array");
    if (colorData && colorData->type != OSP_FLOAT4 && colorData->type != OSP_FLOAT3A)
      throw std::runtime_error("vertex.color must have data type OSP_FLOAT4 or OSP_FLOAT3A");

    // check whether we need 64-bit addressing
    bool huge_mesh = false;
    if (indexData->numBytes > INT32_MAX)
      huge_mesh = true;
    if (vertexData->numBytes > INT32_MAX)
      huge_mesh = true;
    if (normalData && normalData->numBytes > INT32_MAX)
      huge_mesh = true;
    if (colorData && colorData->numBytes > INT32_MAX)
      huge_mesh = true;
    if (texcoordData && texcoordData->numBytes > INT32_MAX)
      huge_mesh = true;

    this->index = (int*)indexData->data;
    //this->vertex = (float*)vertexData->data;
    //this->normal = normalData ? (float*)normalData->data : nullptr;
    this->color  = colorData ? (vec4f*)colorData->data : nullptr;
    this->texcoord = texcoordData ? (vec2f*)texcoordData->data : nullptr;
    this->prim_materialID  = prim_materialIDData ? (uint32_t*)prim_materialIDData->data : nullptr;
    this->materialList  = materialListData ? (ospray::Material**)materialListData->data : nullptr;

    if (materialList && !ispcMaterialPtrs) {
      const int num_materials = materialListData->numItems;
      ispcMaterialPtrs = new void*[num_materials];
      for (int i = 0; i < num_materials; i++) {
        assert(this->materialList[i] != nullptr && "Materials in list should never be NULL");
        this->ispcMaterialPtrs[i] = this->materialList[i]->getIE();
      }
    }

    size_t numTris  = -1;
    size_t numVerts = -1;

    size_t numCompsInTri = 0;
    size_t numCompsInVtx = 0;
    size_t numCompsInNor = 0;
    switch (indexData->type) {
    case OSP_INT:
    case OSP_UINT:  numTris = indexData->size() / 3; numCompsInTri = 3; break;
    case OSP_INT3:
    case OSP_UINT3: numTris = indexData->size(); numCompsInTri = 3; break;
    case OSP_UINT4:
    case OSP_INT4:  numTris = indexData->size(); numCompsInTri = 4; break;
    default:
      throw std::runtime_error("unsupported trianglemesh.index data type");
    }

    switch (vertexData->type) {
    case OSP_FLOAT:   numVerts = vertexData->size() / 4; numCompsInVtx = 4; break;
    case OSP_FLOAT3:  numVerts = vertexData->size(); numCompsInVtx = 3; break;
    case OSP_FLOAT3A: numVerts = vertexData->size(); numCompsInVtx = 4; break;
    case OSP_FLOAT4 : numVerts = vertexData->size(); numCompsInVtx = 4; break;
    default:
      throw std::runtime_error("unsupported trianglemesh.vertex data type");
    }

    numVerts /= numTimeSteps;

    for (int t = 0; t < numTimeSteps; t++)
      this->vertex.push_back((float*)vertexData->data + t*numVerts*numCompsInVtx);

    if (normalData) switch (normalData->type) {
    case OSP_FLOAT3:  numCompsInNor = 3; break;
    case OSP_FLOAT:
    case OSP_FLOAT3A: numCompsInNor = 4; break;
    default:
      throw std::runtime_error("unsupported trianglemesh.vertex.normal data type");
    }

    if (normalData) {
      for (int t = 0; t < numNormalTimeSteps; t++)
        this->normal.push_back((float*)normalData->data + t*numVerts*numCompsInNor);
    }

    eMesh = rtcNewBlurTriangles(embreeSceneHandle,RTC_GEOMETRY_STATIC,
                               numTris,numVerts,numTimeSteps);

    for (int t = 0; t < numTimeSteps; t++)
    {
      rtcSetBuffer(embreeSceneHandle,eMesh,(RTCBufferType)(RTC_VERTEX_BUFFER+t),
                   (void*)(this->vertex[t]),0,
                   sizeOf(vertexData->type));
    }

    rtcSetBuffer(embreeSceneHandle,eMesh,RTC_INDEX_BUFFER,
                 (void*)this->index,0,
                 sizeOf(indexData->type));

    bounds = empty;

    for (int t = 0; t < numTimeSteps; t++) {
      for (uint32_t i = 0; i < numVerts*numCompsInVtx; i+=numCompsInVtx)
        bounds.extend(*(vec3f*)(vertex[t] + i));
    }

    if (numPrints < 5) {
      postStatusMsg(2) << "  created triangle mesh (" << numTris << " tris "
                       << ", " << numVerts << " vertices)\n"
                       << "  mesh bounds " << bounds;
    }

    ispc::BlurTriangles_set(getIE(),model->getIE(),eMesh,
                           numTris,
                           numTimeSteps,
                           numCompsInTri,
                           numCompsInVtx,
                           numCompsInNor,
                           (int*)index,
                           (float**)vertex.data(),
                           normalData ? (float**)normal.data() : nullptr,
                           (ispc::vec4f*)color,
                           (ispc::vec2f*)texcoord,
                           geom_materialID,
                           getMaterial()?getMaterial()->getIE():nullptr,
                           ispcMaterialPtrs,
                           (uint32_t*)prim_materialID,
                           colorData && colorData->type == OSP_FLOAT4,
                           huge_mesh);
  }

  OSP_REGISTER_GEOMETRY(BlurTriangles,triangles);
  OSP_REGISTER_GEOMETRY(BlurTriangles,trianglemesh);

} // ::ospray
