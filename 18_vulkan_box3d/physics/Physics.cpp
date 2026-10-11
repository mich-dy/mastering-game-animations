#include <Physics.h>

#include <condition_variable>
#include <typeinfo>
#include <unordered_set>

#include <AssimpLevel.h>

#include <Tools.h>

bool Physics::initBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstCamData) {
  b3WorldDef worldDef = b3DefaultWorldDef();
  worldDef.gravity = b3Vec3{ 0.0f, -10.0f, 0.0f };
  renderData.rdBox3DWorldId = b3CreateWorld(&worldDef);

  renderData.rdPhysicsTimerFunc.registerFunction([&, this]() { updateBox3dPhysicsImpl(renderData, modInstCamData); } );
  renderData.rdPhysicsTimerFunc.startTimer();

  return true;
}

void Physics::createBox3dPhysicsObject(VkRenderData &renderData, ModelInstanceCamData &modInstCamData, std::shared_ptr<AssimpInstance> instance) {
  // needs a lock to avoid object updates during physics step
  renderData.rdPhysicsTimerFunc.callExtFunctionLocked([&, this]() {
    createBox3dPhysicsObjectImpl(renderData, instance);
  });
}

void Physics::createBox3dPhysicsObjectImpl(VkRenderData &renderData, std::shared_ptr<AssimpInstance> instance) {
  InstanceSettings instSettings = instance->getInstanceSettings();
  std::shared_ptr<AssimpModel> model = instance->getModel();
  ModelSettings modelSettings = model->getModelSettings();

  Logger::log(1, "%s: updating index %i\n", __FUNCTION__, instSettings.isInstanceIndexPosition);

  if (b3Body_IsValid(instSettings.isPhysicsBodyId)) {
    Logger::log(1, "%s: removing object body index %i\n", __FUNCTION__,
      instSettings.isPhysicsBodyId.index1);
    b3DestroyBody(instSettings.isPhysicsBodyId);
    instSettings.isPhysicsBodyId = b3_nullBodyId;
  }

  b3BodyDef bodyDef = b3DefaultBodyDef();
  switch (modelSettings.msPhysicsBodyType) {
    case physicsBodyType::staticBody:
      bodyDef.type = b3_staticBody;
      break;
    case physicsBodyType::kinematicBody:
      bodyDef.type = b3_kinematicBody;
      break;
    case physicsBodyType::dynamicBody:
      bodyDef.type = b3_dynamicBody;
      break;
    default:
      Logger::log(1, "%s error: unknown physics body type\n", __FUNCTION__);
      break;
  }
  bodyDef.position = Tools::glmToBox3d(instSettings.isWorldPosition);
  bodyDef.rotation = Tools::glmToBox3d(glm::quat(glm::radians(instSettings.isWorldRotation)));
  instSettings.isPhysicsBodyId = b3CreateBody(renderData.rdBox3DWorldId, &bodyDef);
  Logger::log(1, "%s: created object body index %i\n", __FUNCTION__,
    instSettings.isPhysicsBodyId.index1);

  b3ShapeDef shapeDef = b3DefaultShapeDef();
  shapeDef.density = 1.0f;
  shapeDef.baseMaterial.friction = modelSettings.msPhysicsFrictionCoeff;
  shapeDef.baseMaterial.restitution = modelSettings.msPhysicsRestitutionCoeff;
  shapeDef.baseMaterial.rollingResistance = modelSettings.msPhysicsRollingResistance;
  shapeDef.enableContactEvents = true;

  b3Transform hullTransform{};
  hullTransform.p = Tools::glmToBox3d(modelSettings.msPhysicsHullOffset * instSettings.isScale);
  hullTransform.q = Tools::glmToBox3d(glm::quat(glm::radians(modelSettings.msPhysicsHullRotation)));

  //b3BoxHull dynamicBox = b3MakeScaledBoxHull((b3Vec3) { 1.0f, 1.0f, 1.0f }, hullTransform, Tools::glmToBox3d(modelSettings.msPhysicsHullScale * instSettings.isScale));

  switch (modelSettings.msPhysicsHullType) {
    case physicsHullType::box:
    case physicsHullType::cube:
      {
        // do a pre-scale instead of post-scale
        b3Vec3 h = Tools::glmToBox3d(modelSettings.msPhysicsHullSize);
        b3Transform xf{};
        b3ScaleBox( &h, &xf, Tools::glmToBox3d(modelSettings.msPhysicsHullScale * instSettings.isScale), 4.0f * B3_LINEAR_SLOP );
        b3BoxHull dynamicBox = b3MakeTransformedBoxHull( h.x, h.y, h.z, hullTransform );

        b3CreateHullShape(instSettings.isPhysicsBodyId, &shapeDef, &dynamicBox.base);

      }
      break;

    case physicsHullType::sphere:
      {
        b3Sphere sphere{};
        sphere.radius = modelSettings.msPhysicsHullSize.x * modelSettings.msPhysicsHullScale.x * instSettings.isScale;
        sphere.center = Tools::glmToBox3d(modelSettings.msPhysicsHullOffset * instSettings.isScale);

        b3CreateSphereShape(instSettings.isPhysicsBodyId, &shapeDef, &sphere);
      }
      break;

    default:
      Logger::log(1, "%s error: invalid hull type \n", __FUNCTION__);
      break;
  }

  // store orig position and rotation
  if (!renderData.rdPhysicsRunning) {
    instSettings.isOrigWorldPosition = instSettings.isWorldPosition;
    instSettings.isOrigWorldRotation = instSettings.isWorldRotation;
    instSettings.isOrigScale = instSettings.isScale;
  }

  instance->setInstanceSettings(instSettings);
}

void Physics::createBox3dPhysicsObjects(VkRenderData &renderData, ModelInstanceCamData &modInstCamData) {
  for (const auto& model : modInstCamData.micModelList) {
    ModelSettings modelSettings = model->getModelSettings();
    if (modelSettings.msPhysicsEnabled) {
      std::vector<std::shared_ptr<AssimpInstance>> instances = modInstCamData.micAssimpInstancesPerModel[model->getModelFileName()];
      for (auto instance : instances) {
        createBox3dPhysicsObject(renderData, modInstCamData, instance);
      }
    }
  }
  for (const auto &level : modInstCamData.micLevels) {
    if (level->getTriangleCount() > 0) {
      level->recalculateBox3dMesh(renderData);
    }
  }
}

void Physics::doBox3dStep(VkRenderData &renderData, ModelInstanceCamData &modInstCamData) {
  // TODO: make configurable or move to class header as constants
  float timeStep = 1.0f / 60.0f;
  int subStepCount = 4;

  if (b3World_IsValid(renderData.rdBox3DWorldId)) {
    b3World_Step(renderData.rdBox3DWorldId, timeStep, subStepCount);
  }

  // always update
  updateObjectsFromBox3d(modInstCamData);
}

void Physics::updateObjectsFromBox3d(ModelInstanceCamData &modInstCamData) {

  for (const auto& model : modInstCamData.micModelList) {
    ModelSettings modelSettings = model->getModelSettings();
    if (modelSettings.msPhysicsEnabled) {
      std::vector<std::shared_ptr<AssimpInstance>> instances = modInstCamData.micAssimpInstancesPerModel[model->getModelFileName()];

      for (auto instance : instances) {
        if (!instance) {
          Logger::log(1, "%s error: found invalid instance\n", __FUNCTION__);
          continue;
        }

        InstanceSettings instSettings = instance->getInstanceSettings();

        if (b3Body_IsValid(instSettings.isPhysicsBodyId)) {
          b3Vec3 position = b3Body_GetPosition(instSettings.isPhysicsBodyId);
          b3Quat rotation = b3Body_GetRotation(instSettings.isPhysicsBodyId);

          instSettings.isWorldPosition = Tools::box3dToGlm(position);

          glm::quat newRotation = Tools::box3dToGlm(rotation);
          instSettings.isWorldRotation = glm::degrees(glm::eulerAngles(newRotation));

          instance->setInstanceSettings(instSettings);
        }
      }
    }
  }

  std::lock_guard<std::mutex> lock(mBox3dShapeToTriangleIndexMapMutex);
  mBox3dShapeToTriangleIndexMap.clear();

  for (const auto &level : modInstCamData.micLevels) {
    if (level->getTriangleCount() > 0) {
      b3BodyId levelBody = level->getBox3dBodyId();
      if (b3Body_IsValid(levelBody)) {
        std::unordered_set<int> levelShapes = level->getBox3dShapeIds();

        int bodyContactCapacity = b3Body_GetContactCapacity(levelBody);
        std::vector<b3ContactData> contactData(bodyContactCapacity);
        int numContacts = b3Body_GetContactData(levelBody, contactData.data(), bodyContactCapacity);
        for (int i = 0; i < numContacts; ++i) {
          auto contact = contactData[i];
          b3ShapeId shapeA = contact.shapeIdA;
          b3ShapeId shapeB = contact.shapeIdB;

          int shapeId = B3_NULL_INDEX;
          if (levelShapes.contains(shapeA.index1)) {
            shapeId = shapeA.index1;
          } else if (levelShapes.contains(shapeB.index1)) {
            shapeId = shapeB.index1;
          } else {
            Logger::log(1, "%s error: shape index not found in body\n", __FUNCTION__);
            continue;
          }

          for (int j = 0; j < contact.manifoldCount; ++j) {
            b3Manifold manifold = contact.manifolds[j];
            for (int k = 0; k < manifold.pointCount; ++k) {
              b3ManifoldPoint point = manifold.points[k];
              Logger::log(2, "%s: shape %i, contact %i / manifold %i / point %i has triangle index %i\n",
                __FUNCTION__, shapeId, i, j, k, point.triangleIndex);
              mBox3dShapeToTriangleIndexMap[shapeId].push_back(point.triangleIndex);
            }
          }
        }
      }
    }
  }
}

void Physics::updateBox3dPhysics(VkRenderData &renderData, ModelInstanceCamData &modInstCamData, bool singleStep) {
  renderData.rdPhysicsTimerFunc.callExtFunctionLocked([&, this]() {
    updateBox3dPhysicsImpl(renderData, modInstCamData, singleStep);
  });
}

void Physics::updateBox3dPhysicsImpl(VkRenderData &renderData, ModelInstanceCamData &modInstCamData, bool singleStep) {
  renderData.rdPhysicsTime = 0.0f;
  renderData.rdPhysicsTimer.start();

  if (renderData.rdPhysicsRunning || singleStep) {
    doBox3dStep(renderData, modInstCamData);
  }

  renderData.rdPhysicsTime = renderData.rdPhysicsTimer.stop();
}

void Physics::cleanupBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstCamData) {
  renderData.rdPhysicsTimerFunc.stopTimer();
  renderData.rdPhysicsTimerFunc.cleanupFunctions();

  if (b3World_IsValid(renderData.rdBox3DWorldId)) {
    b3DestroyWorld(renderData.rdBox3DWorldId);
    renderData.rdBox3DWorldId = b3_nullWorldId;
  }

  // cleanup body ids of instances too
  for (const auto& model : modInstCamData.micModelList) {
    ModelSettings modelSettings = model->getModelSettings();
    if (modelSettings.msPhysicsEnabled) {
      std::vector<std::shared_ptr<AssimpInstance>> instances = modInstCamData.micAssimpInstancesPerModel[model->getModelFileName()];

      for (auto instance : instances) {
        InstanceSettings instSettings = instance->getInstanceSettings();
        instSettings.isPhysicsBodyId = b3_nullBodyId;

        // restore orig position and rotation
        if (!renderData.rdPhysicsRunning) {
          instSettings.isWorldPosition = instSettings.isOrigWorldPosition;
          instSettings.isWorldRotation = instSettings.isOrigWorldRotation;
          instSettings.isScale = instSettings.isOrigScale;
        }

        instance->setInstanceSettings(instSettings);
      }
    }
  }
}

void Physics::resetBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstCamData) {
  cleanupBox3d(renderData, modInstCamData);
  initBox3d(renderData, modInstCamData);
  createBox3dPhysicsObjects(renderData, modInstCamData);

  std::lock_guard<std::mutex> lock(mBox3dShapeToTriangleIndexMapMutex);
  mBox3dShapeToTriangleIndexMap.clear();
}

std::unordered_map<int, std::vector<int>> Physics::getShapeToTriangleIndexMap() {
  std::lock_guard<std::mutex> lock(mBox3dShapeToTriangleIndexMapMutex);
  return mBox3dShapeToTriangleIndexMap;
}
