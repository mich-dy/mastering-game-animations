#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <box3d/box3d.h>

#include <TimerFunc.h>
#include <AssimpInstance.h>

#include <ModelInstanceCamData.h>
#include <VkRenderData.h>

class Physics {
  public:
    bool initBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstCamData);

    void updateBox3dPhysics(VkRenderData &renderData, ModelInstanceCamData &modInstCamData);
    void doBox3dStep(ModelInstanceCamData &modInstCamData);

    void createBox3dPhysicsObjects(VkRenderData &renderData, ModelInstanceCamData &modInstCamData);
    void createBox3dPhysicsObject(VkRenderData &renderData, std::shared_ptr<AssimpInstance> instance);
    void updateObjectsFromBox3d(ModelInstanceCamData &modInstCamData);

    void cleanupBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstCamData);

    void resetBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstCamData);

  private:
    TimerFunc mPhysicsTimer{};
    b3WorldId mBox3DWorldId;

    void createBox3dPhysicsObjectImpl(VkRenderData &renderData, std::shared_ptr<AssimpInstance> instance);
};
