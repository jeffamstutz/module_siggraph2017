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

#include "../../CommandLineExport.h"
#include "../SceneParser.h"

#include "ospray/ospray_cpp/Renderer.h"

#include "apps/common/xml/XML.h"

namespace commandline {

  /* FIXME: data never freed */
  const ospcommon::vec3f* transform_positions ( const ospcommon::vec3f* in, size_t N,  const ospcommon::affine3f& space)
  {
    ospcommon::vec3f* out = new ospcommon::vec3f[N];
    for (size_t i=0; i<N; i++)
      out[i] = xfmPoint(space,in[i]);
    return out;
  }

  /* FIXME: data never freed */
  const ospcommon::vec3f* transform_normals ( const ospcommon::vec3f* in, size_t N,  const ospcommon::affine3f& space)
  {
    ospcommon::vec3f* out = new ospcommon::vec3f[N];
    for (size_t i=0; i<N; i++)
      out[i] = xfmNormal(space,in[i]);
    return out;
  }

  class OSPRAY_COMMANDLINE_INTERFACE DemoSceneParser : public SceneParser
  {
  public:
    DemoSceneParser(ospray::cpp::Renderer);

    bool parse(int ac, const char **&av) override;

    std::deque<ospray::cpp::Model> model() const override;
    std::deque<ospcommon::box3f>   bbox()  const override;

  protected:

    ospray::cpp::Renderer renderer;
    std::deque<ospray::cpp::Model>    sceneModels;
    std::deque<ospcommon::box3f>      sceneBboxes;

  private:

    struct TriangleMesh
    {
      const ospcommon::vec3i* triangles;
      size_t numTriangles;
      const ospcommon::vec3f* positions;
      size_t numPositions;
      const ospcommon::vec3f* normals;
      size_t numNormals;
      const ospcommon::vec2f* texcoords;
      size_t numTexcoords;
      ospray::cpp::Material material;

      std::vector<const ospcommon::vec3f*> animatedPositions;
      std::vector<const ospcommon::vec3f*> animatedNormals;

      TriangleMesh()
        : triangles(nullptr),
          numTriangles(0),
          positions(nullptr),
          numPositions(0),
          normals(nullptr),
          numNormals(0),
          texcoords(nullptr),
          numTexcoords(0)
      {
      }

      TriangleMesh (const std::shared_ptr<TriangleMesh>& other, const ospcommon::affine3f& space)
      {
        triangles = other->triangles;
        numTriangles = other->numTriangles;
        positions = transform_positions(other->positions,other->numPositions,space);
        for (size_t t=0; t<other->animatedPositions.size(); t++)
          animatedPositions.push_back(transform_positions(other->animatedPositions[t],other->numPositions,space));
        numPositions = other->numPositions;
        normals = transform_normals(other->normals,other->numNormals,space);
        for (size_t t=0; t<other->animatedNormals.size(); t++)
          animatedNormals.push_back(transform_normals(other->animatedNormals[t],other->numNormals,space));
        numNormals = other->numNormals;
        texcoords = other->texcoords;
        numTexcoords = other->numTexcoords;
        material = other->material;
      }

      TriangleMesh (const std::shared_ptr<TriangleMesh>& other, const std::vector<ospcommon::affine3f>& spaces)
      {
        triangles = other->triangles;
        numTriangles = other->numTriangles;

        positions = transform_positions(other->positions,other->numPositions,spaces[0]);
        if (other->animatedPositions.size())
        {
          if (other->animatedPositions.size() != spaces.size())
            throw std::runtime_error("mismatch in temporal resolution of transforms and geometry is not supported");

          for (size_t t=0; t<spaces.size(); t++)
            animatedPositions.push_back(transform_positions(other->animatedPositions[t],other->numPositions,spaces[t]));
        }
        else
        {
          for (size_t t=0; t<spaces.size(); t++)
            animatedPositions.push_back(transform_positions(other->positions,other->numPositions,spaces[t]));
        }
        numPositions = other->numPositions;

        normals = transform_normals(other->normals,other->numNormals,spaces[0]);
        if (other->animatedNormals.size())
        {
          if (other->animatedPositions.size() != spaces.size())
            throw std::runtime_error("mismatch in temporal resolution of transforms and geometry is not supported");

          for (size_t t=0; t<spaces.size(); t++)
            animatedNormals.push_back(transform_normals(other->animatedNormals[t],other->numNormals,spaces[t]));
        }
        else
        {
          for (size_t t=0; t<spaces.size(); t++)
            animatedNormals.push_back(transform_normals(other->normals,other->numNormals,spaces[t]));
        }
        numNormals = other->numNormals;

        texcoords = other->texcoords;
        numTexcoords = other->numTexcoords;
        material = other->material;
      }
    };

    struct Object
    {
      std::vector<std::shared_ptr<TriangleMesh>> meshes;
      std::vector<ospcommon::affine3f> transforms;
      std::vector<std::vector<ospcommon::affine3f>> animatedTransforms;
    };

    struct Scene
    {
      std::vector<std::shared_ptr<Object>> objects;
    };

    bool flatten{false};
    ospray::cpp::Model sceneModel;
    ospcommon::box3f sceneBounds;

    std::shared_ptr<Scene> scene;
    std::map<std::string, std::shared_ptr<Object>> objectMap;
    std::map<std::string, ospray::cpp::Material> materialMap;
    OSPMaterial defaultMaterial;
    char* binBasePtr;
    std::string path;

    void importXml(const ospcommon::FileName& fileName);
    void parseScene(const ospray::xml::Node& node);
    void parseTransform(const ospray::xml::Node& node, bool animated = false);
    std::shared_ptr<TriangleMesh> parseTriangleMesh(const ospray::xml::Node& node);
    std::shared_ptr<Object> parseGroup(const ospray::xml::Node& node);
    ospray::cpp::Material parseMaterial(const ospray::xml::Node& node);
    void parseAssign(const ospray::xml::Node& node);

    void finalize();
    ospray::cpp::Geometry createOspTriangleMesh(const std::shared_ptr<TriangleMesh>& mesh);
    ospcommon::box3f computeTriangleMeshBounds(const std::shared_ptr<TriangleMesh>& mesh);
    ospcommon::box3f computeInstanceBounds(const ospcommon::box3f& bbox, const ospcommon::affine3f& transform);
  };

} // ::commandline
