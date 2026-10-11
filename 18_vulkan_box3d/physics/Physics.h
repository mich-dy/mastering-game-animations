#pragma once

#include <mutex>
#include <memory>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>
#include <box3d/box3d.h>

#include <TimerFunc.h>
#include <AssimpInstance.h>

#include <ModelInstanceCamData.h>
#include <VkRenderData.h>

class Physics {
  public:
    bool initBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstCamData);

    void createBox3dPhysicsObjects(VkRenderData &renderData, ModelInstanceCamData &modInstCamData);
    void createBox3dPhysicsObject(VkRenderData &renderData, ModelInstanceCamData &modInstCamData, std::shared_ptr<AssimpInstance> instance);

    void updateBox3dPhysics(VkRenderData &renderData, ModelInstanceCamData &modInstCamData, bool singleStep = false);

    void cleanupBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstCamData);

    void resetBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstCamData);

    std::unordered_map<int, std::vector<int>> getShapeToTriangleIndexMap();

  private:
    void doBox3dStep(VkRenderData &renderData, ModelInstanceCamData &modInstCamData);
    void updateObjectsFromBox3d(ModelInstanceCamData &modInstCamData);

    void createBox3dPhysicsObjectImpl(VkRenderData &renderData, std::shared_ptr<AssimpInstance> instance);

    void updateBox3dPhysicsImpl(VkRenderData &renderData, ModelInstanceCamData &modInstCamData, bool singleStep = false);

    std::unordered_map<int, std::vector<int>> mBox3dShapeToTriangleIndexMap{};

    std::mutex mBox3dShapeToTriangleIndexMapMutex;
};
