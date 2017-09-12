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

// O_LARGEFILE is a GNU extension.
#ifdef __APPLE__
#define  O_LARGEFILE  0
#endif

#include <memory>
#include <sys/mman.h>
#include <fcntl.h>
#include "ospcommon/FileName.h"
#include "../../../miniSG/miniSG.h"
#include "DemoSceneParser.h"

namespace commandline {

  using namespace ospray;
  using namespace ospcommon;

  DemoSceneParser::DemoSceneParser(cpp::Renderer renderer)
    : renderer(renderer)
  {
  }

  bool DemoSceneParser::parse(int ac, const char **&av)
  {
    for (int i = 1; i < ac; i++)
    {
      const std::string arg = av[i];
      if (arg == "--flatten")
        flatten = true;
      else
      {
        FileName fn = arg;
        if (fn.ext() == "xml")
          importXml(fn);
      }
    }

    finalize();
    sceneModel.commit();
    sceneModels.push_back(sceneModel);
    sceneBboxes.push_back(sceneBounds);
    return true;
  }

  void DemoSceneParser::importXml(const FileName& fileName)
  {
    std::shared_ptr<xml::XMLDoc> doc = xml::readXML(fileName);
    if (doc->child.size() != 1 || doc->child[0]->name != "scene") {
      throw std::runtime_error("invalid scene file");
    }

    path = fileName.path();
    if (path == "")
      path = ".";

    std::string binFileName = fileName.str() + ".bin";

    FILE *file = fopen(binFileName.c_str(), "rb");
    if (!file) {
      binFileName = fileName.str().substr(0, fileName.str().find_last_of('.')) + ".bin";
      file = fopen(binFileName.c_str(), "rb");
      if (!file)
        throw std::runtime_error("could not open binary file: " + binFileName);
    }
    fseek(file, 0, SEEK_END);
    ssize_t fileSize = ftell(file);
    fclose(file);

    int fd = ::open(binFileName.c_str(), O_LARGEFILE | O_RDONLY);
    if (fd == -1) {
      throw std::runtime_error("could not open file: " + binFileName);
    }

    binBasePtr = (char*)mmap(nullptr, fileSize, PROT_READ, MAP_SHARED, fd, 0);

    const xml::Node& root = *doc->child[0];
    parseScene(root);
  }

  void DemoSceneParser::parseScene(const xml::Node& node)
  {
    scene = std::make_shared<Scene>();

    defaultMaterial = ospNewMaterial(renderer.handle(), "OBJMaterial");
    vec3f kd(.7f);
    vec3f ks(.3f);
    ospSet3fv(defaultMaterial, "Kd", &kd.x);
    ospSet3fv(defaultMaterial, "Ks", &ks.x);
    ospSet1f(defaultMaterial, "Ns", 99.f);
    ospCommit(defaultMaterial);

    for (size_t i = 0; i < node.child.size(); i++)
    {
      const xml::Node& child = *node.child[i];
      if (child.name == "TriangleMesh")
      {
        std::shared_ptr<Object> object = std::make_shared<Object>();
        object->meshes.push_back(parseTriangleMesh(child));
        scene->objects.push_back(object);
      }
      else if (child.name == "Group")
      {
        scene->objects.push_back(parseGroup(child));
      }
      else if (child.name == "Transform")
      {
        parseTransform(child);
      }
      else if (child.name == "TransformAnimation")
      {
        parseTransform(child, true);
      }
      else if (child.name == "assign")
      {
        parseAssign(child);
      }
    }
  }

  void DemoSceneParser::parseTransform(const xml::Node& node, bool animated)
  {
    std::vector<affine3f> transforms;
    std::shared_ptr<Object> object;

    for (size_t i = 0; i < node.child.size(); i++)
    {
      const xml::Node& child = *node.child[i];
      if (child.name == "AffineSpace")
      {
        affine3f transform;
        int numRead = sscanf((char*)child.content.c_str(),
                             "%f %f %f %f\n%f %f %f %f\n%f %f %f %f",
                             &transform.l.vx.x,
                             &transform.l.vy.x,
                             &transform.l.vz.x,
                             &transform.p.x,
                             &transform.l.vx.y,
                             &transform.l.vy.y,
                             &transform.l.vz.y,
                             &transform.p.y,
                             &transform.l.vx.z,
                             &transform.l.vy.z,
                             &transform.l.vz.z,
                             &transform.p.z
                             );
        if (numRead != 12)
          throw std::runtime_error("invalid transform");
        transforms.push_back(transform);
      }
      else if (child.name == "TriangleMesh")
      {
        std::string id = child.getProp("id");
        object = std::make_shared<Object>();
        object->meshes.push_back(parseTriangleMesh(child));
        objectMap[id] = object;
        scene->objects.push_back(object);
      }
      else if (child.name == "Group")
      {
        std::string id = child.getProp("id");
        object = parseGroup(child);
        objectMap[id] = object;
        scene->objects.push_back(object);
      }
      else if (child.name == "ref")
      {
        std::string id = child.getProp("id");
        object = objectMap[id];
      }
      else
      {
        throw std::runtime_error("unsupported node");
      }
    }

    if (!object)
      throw std::runtime_error("unsupported geometry in transform");

    if (transforms.size() == 0)
      throw std::runtime_error("no affinespace in transform");

    object->transforms.push_back(transforms[0]);
    if (animated)
      object->animatedTransforms.push_back(transforms);
  }

  std::shared_ptr<DemoSceneParser::TriangleMesh> DemoSceneParser::parseTriangleMesh(const ospray::xml::Node& node)
  {
    std::shared_ptr<TriangleMesh> mesh = std::make_shared<TriangleMesh>();
    mesh->material = defaultMaterial;

    for (size_t i = 0; i < node.child.size(); i++)
    {
      const xml::Node& child = *node.child[i];
      if (child.name == "triangles")
      {
        size_t ofs = std::stoll(child.getProp("ofs"));
        size_t size = std::stoll(child.getProp("size"));
        mesh->triangles = (const vec3i*)(binBasePtr + ofs);
        mesh->numTriangles = size;
      }
      else if (child.name == "positions")
      {
        size_t ofs = std::stoll(child.getProp("ofs"));
        size_t size = std::stoll(child.getProp("size"));
        mesh->positions = (const vec3f*)(binBasePtr + ofs);
        mesh->numPositions = size;
      }
      else if (child.name == "normals")
      {
        size_t ofs = std::stoll(child.getProp("ofs"));
        size_t size = std::stoll(child.getProp("size"));
        mesh->normals = (const vec3f*)(binBasePtr + ofs);
        mesh->numNormals = size;
      }
      else if (child.name == "texcoords")
      {
        size_t ofs = std::stoll(child.getProp("ofs"));
        size_t size = std::stoll(child.getProp("size"));
        mesh->texcoords = (const vec2f*)(binBasePtr + ofs);
        mesh->numTexcoords = size;
      }
      else if (child.name == "material")
      {
        std::string id = child.getProp("id");
        if (child.child.size() > 0)
        {
          mesh->material = parseMaterial(child);
          materialMap[id] = mesh->material;
        }
        else
        {
          mesh->material = materialMap[id];
        }
      }
      else if (child.name == "animated_positions")
      {
        for (size_t j = 0; j < child.child.size(); j++)
        {
          const xml::Node& step = *child.child[j];
          if (step.name == "positions")
          {
            size_t ofs = std::stoll(step.getProp("ofs"));
            size_t size = std::stoll(step.getProp("size"));
            mesh->animatedPositions.push_back((const vec3f*)(binBasePtr + ofs));
            mesh->numPositions = size;
          }
        }
        mesh->positions = mesh->animatedPositions[0];
      }
      else if (child.name == "animated_normals")
      {
        for (size_t j = 0; j < child.child.size(); j++)
        {
          const xml::Node& step = *child.child[j];
          if (step.name == "normals")
          {
            size_t ofs = std::stoll(step.getProp("ofs"));
            size_t size = std::stoll(step.getProp("size"));
            mesh->animatedNormals.push_back((const vec3f*)(binBasePtr + ofs));
            mesh->numNormals = size;
          }
        }
        mesh->normals = mesh->animatedNormals[0];
      }
    }

    return mesh;
  }

  std::shared_ptr<DemoSceneParser::Object> DemoSceneParser::parseGroup(const ospray::xml::Node& node)
  {
    std::shared_ptr<Object> object = std::make_shared<Object>();

    for (size_t i = 0; i < node.child.size(); i++)
    {
      const xml::Node& child = *node.child[i];
      if (child.name == "TriangleMesh")
      {
        object->meshes.push_back(parseTriangleMesh(child));
      }
      else
      {
        throw std::runtime_error("only groups with triangle meshes are supported");
      }
    }

    return object;
  }

  cpp::Material DemoSceneParser::parseMaterial(const ospray::xml::Node& node)
  {
    std::string type = "\"OBJ\"";
    for (size_t i = 0; i < node.child.size(); i++)
    {
      const xml::Node& child = *node.child[i];
      if (child.name == "code")
        type = child.content;
    }

    char typeC[1024];
    sscanf(type.c_str(), "\"%[^\"]\"", typeC);

    std::string ospType = typeC;
    if (ospType == "OBJ")
      ospType = "OBJMaterial";

    cpp::Material mtl(ospNewMaterial(renderer.handle(), ospType.c_str()));

    for (size_t i = 0; i < node.child.size(); i++)
    {
      const xml::Node& child = *node.child[i];
      if (child.name == "parameters")
      {
        for (const auto& param : child.child)
        {
          std::string name = param->getProp("name");

          if (param->name == "float")
          {
            float v;
            int numRead = sscanf((char*)param->content.c_str(), "%f", &v);
            if (numRead != 1)
              throw std::runtime_error("invalid float");
            mtl.set(name, v);
          }
          else if (param->name == "float3")
          {
            vec3f v;
            int numRead = sscanf((char*)param->content.c_str(), "%f %f %f", &v.x, &v.y, &v.z);
            if (numRead != 3)
              throw std::runtime_error("invalid float3");
            mtl.set(name, v);
          }
          else if (param->name == "texture2d")
          {
            auto texture = miniSG::loadTexture(path, param->content);
            OSPTexture2D ospTexture = miniSG::createTexture2D(texture);
            mtl.set(name, ospTexture);
          }
          else if (param->name == "texture3d")
          {
            // Also a 2D texture, why is it called 3D??
            std::string fileName = param->getProp("src");
            auto texture = miniSG::loadTexture(path, fileName);
            OSPTexture2D ospTexture = miniSG::createTexture2D(texture);
            mtl.set(name, ospTexture);
          }
        }
      }
    }

    mtl.commit();
    return mtl;
  }

  void DemoSceneParser::parseAssign(const ospray::xml::Node& node)
  {
    std::string type = node.getProp("type");
    if (type == "material")
    {
      for (size_t i = 0; i < node.child.size(); i++)
      {
        const xml::Node& child = *node.child[i];
        if (child.name == "material")
        {
          std::string id = child.getProp("id");
          materialMap[id] = parseMaterial(child);
        }
      }
    }
  }

  void DemoSceneParser::finalize()
  {
    sceneBounds = empty;

    #if 0
    for (const auto& object : scene->objects)
    {
      box3f objectBounds = empty;
      for (const auto& mesh : object->meshes)
        objectBounds.extend(computeTriangleMeshBounds(mesh));

      if (flatten || object->transforms.size() == 0)
      {
        for (const auto& mesh : object->meshes)
        {
          if (object->animatedTransforms.size())
          {
            for (const auto& spaces : object->animatedTransforms) {
              auto tmesh = std::make_shared<TriangleMesh>(mesh,spaces);
              cpp::Geometry ospGeometry = createOspTriangleMesh(tmesh);
              sceneModel.addGeometry(ospGeometry);
            }
          }
          else if (object->transforms.size())
          {
            for (const auto& space : object->transforms) {
              auto tmesh = std::make_shared<TriangleMesh>(mesh,space);
              cpp::Geometry ospGeometry = createOspTriangleMesh(tmesh);
              sceneModel.addGeometry(ospGeometry);
            }
          }
          else
          {
            cpp::Geometry ospGeometry = createOspTriangleMesh(mesh);
            sceneModel.addGeometry(ospGeometry);
          }
        }

        sceneBounds.extend(objectBounds);
      }
      else
      {
        // Instancing
        cpp::Model ospModel;
        for (const auto& mesh : object->meshes)
        {
          cpp::Geometry ospGeometry = createOspTriangleMesh(mesh);
          ospModel.addGeometry(ospGeometry);
        }
        ospModel.commit();

        if (object->animatedTransforms.size() == 0)
        {
          for (const affine3f& transform : object->transforms)
          {
            cpp::Geometry ospInstance = ospNewInstance(ospModel.handle(), (osp::affine3f&)transform);
            sceneModel.addGeometry(ospInstance);
            sceneBounds.extend(computeInstanceBounds(objectBounds, transform));
          }
        }
        else
        {
          for (const std::vector<affine3f>& animatedTransforms : object->animatedTransforms)
          {
            cpp::Geometry ospInstance = ospNewInstance2(ospModel.handle(), (osp::affine3f*)animatedTransforms.data(), animatedTransforms.size());
            //cpp::Geometry ospInstance = ospNewInstance2(ospModel.handle(), (osp::affine3f*)animatedTransforms.data(), 1);
            sceneModel.addGeometry(ospInstance);
            for (const affine3f& transform : animatedTransforms)
              sceneBounds.extend(computeInstanceBounds(objectBounds, transform));
          }
        }
      }
    }
    #endif
  }

  cpp::Geometry DemoSceneParser::createOspTriangleMesh(const std::shared_ptr<TriangleMesh>& mesh)
  {
    bool istrain = true;
    ospray::cpp::Material pusher_material = nullptr;
    if (materialMap.find("train_pusher") != materialMap.end())
      pusher_material = materialMap["train_pusher"];
    istrain &= mesh->material.object() != pusher_material.object();

    ospray::cpp::Material wheel_material = nullptr;
    if (materialMap.find("train_wheel") != materialMap.end())
      wheel_material = materialMap["train_wheel"];
    istrain &= mesh->material.object() != wheel_material.object();

    cpp::Geometry ospGeometry("triangles");

    OSPData ospIndex = ospNewData(mesh->numTriangles, OSP_INT3, mesh->triangles);
    ospGeometry.set("index", ospIndex);

    if (mesh->animatedPositions.size() == 0)
    {
      OSPData ospPosition = ospNewData(mesh->numPositions, OSP_FLOAT3, mesh->positions);
      ospGeometry.set("position", ospPosition);
    }
    /* forces the train to two time steps only */
    else if (istrain)
    {
#if 1
      size_t t0 = 0, t1 = mesh->animatedPositions.size()-1;
      std::vector<vec3f> buffer(2 * mesh->numPositions);
      for (size_t i = 0; i < mesh->numPositions; i++)
        buffer[0*mesh->numPositions + i] = mesh->animatedPositions[t0][i];
      for (size_t i = 0; i < mesh->numPositions; i++)
        buffer[1*mesh->numPositions + i] = mesh->animatedPositions[t1][i];

      OSPData ospPosition = ospNewData(2 * mesh->numPositions, OSP_FLOAT3, buffer.data());
      ospGeometry.set("position", ospPosition);
      ospGeometry.set("positionTimeSteps", 2);
#else
      size_t numTimeSteps = mesh->animatedPositions.size();
      size_t numTimeStepsOut = (numTimeSteps-1)/4+1;
      std::vector<vec3f> buffer(numTimeStepsOut * mesh->numPositions);
      for (size_t t = 0; t < numTimeStepsOut; t++)
      {
        int tin = t*numTimeSteps/numTimeStepsOut;
        for (size_t i = 0; i < mesh->numPositions; i++)
          buffer[t*mesh->numPositions + i] = mesh->animatedPositions[tin][i];
      }

      OSPData ospPosition = ospNewData(numTimeStepsOut * mesh->numPositions, OSP_FLOAT3, buffer.data());
      ospGeometry.set("position", ospPosition);
      ospGeometry.set("positionTimeSteps", (int)numTimeStepsOut);
#endif
    }
    else
    {
      size_t numTimeSteps = mesh->animatedPositions.size();
      std::vector<vec3f> buffer(numTimeSteps * mesh->numPositions);
      for (size_t t = 0; t < numTimeSteps; t++)
      {
        for (size_t i = 0; i < mesh->numPositions; i++)
          buffer[t*mesh->numPositions + i] = mesh->animatedPositions[t][i];
      }

      OSPData ospPosition = ospNewData(numTimeSteps * mesh->numPositions, OSP_FLOAT3, buffer.data());
      ospGeometry.set("position", ospPosition);
      ospGeometry.set("positionTimeSteps", (int)numTimeSteps);
    }

    if (mesh->numNormals > 0)
    {
      if (mesh->animatedNormals.size() == 0)
      {
        OSPData ospNormal = ospNewData(mesh->numNormals, OSP_FLOAT3, mesh->normals);
        ospGeometry.set("normal", ospNormal);
      }
      else
      {
        size_t numTimeSteps = mesh->animatedNormals.size();
        std::vector<vec3f> buffer(numTimeSteps * mesh->numNormals);
        for (size_t t = 0; t < numTimeSteps; t++)
        {
          for (size_t i = 0; i < mesh->numNormals; i++)
            buffer[t*mesh->numNormals + i] = mesh->animatedNormals[t][i];
        }

        OSPData ospNormal = ospNewData(numTimeSteps * mesh->numNormals, OSP_FLOAT3, buffer.data());
        ospGeometry.set("normal", ospNormal);
        ospGeometry.set("normalTimeSteps", (int)numTimeSteps);
      }
    }

    if (mesh->numTexcoords > 0)
    {
      OSPData texcoord = ospNewData(mesh->numTexcoords, OSP_FLOAT2, mesh->texcoords);
      ospGeometry.set("texcoord", texcoord);
    }

    ospGeometry.setMaterial(mesh->material);

    ospGeometry.commit();
    return ospGeometry;
  }

  box3f DemoSceneParser::computeTriangleMeshBounds(const std::shared_ptr<TriangleMesh>& mesh)
  {
    box3f bbox = empty;
    for (size_t i = 0; i < mesh->numPositions; i++)
      bbox.extend(mesh->positions[i]);
    return bbox;
  }

  box3f DemoSceneParser::computeInstanceBounds(const box3f& bbox, const affine3f& transform)
  {
    const vec3f lo = bbox.lower;
    const vec3f hi = bbox.upper;
    box3f bounds = empty;
    bounds.extend(xfmPoint(transform,vec3f(lo.x,lo.y,lo.z)));
    bounds.extend(xfmPoint(transform,vec3f(hi.x,lo.y,lo.z)));
    bounds.extend(xfmPoint(transform,vec3f(lo.x,hi.y,lo.z)));
    bounds.extend(xfmPoint(transform,vec3f(hi.x,hi.y,lo.z)));
    bounds.extend(xfmPoint(transform,vec3f(lo.x,lo.y,hi.z)));
    bounds.extend(xfmPoint(transform,vec3f(hi.x,lo.y,hi.z)));
    bounds.extend(xfmPoint(transform,vec3f(lo.x,hi.y,hi.z)));
    bounds.extend(xfmPoint(transform,vec3f(hi.x,hi.y,hi.z)));
    return bounds;
  }

  std::deque<cpp::Model> DemoSceneParser::model() const
  {
    return sceneModels;
  }

  std::deque<box3f> DemoSceneParser::bbox() const
  {
    return sceneBboxes;
  }

} // ::commandline
