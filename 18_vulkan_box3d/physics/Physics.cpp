#include <Physics.h>

#include <Tools.h>

bool Physics::initBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstCamData) {
  b3WorldDef worldDef = b3DefaultWorldDef();
  worldDef.gravity = b3Vec3{ 0.0f, -10.0f, 0.0f };
  mBox3DWorldId = b3CreateWorld(&worldDef);

  mPhysicsTimer.registerFunction([&, this]() { updateBox3dPhysics(renderData, modInstCamData); } );
  mPhysicsTimer.startTimer();

  return true;
}

void Physics::createBox3dPhysicsObject(VkRenderData &renderData, std::shared_ptr<AssimpInstance> instance) {
  InstanceSettings instSettings = instance->getInstanceSettings();
  std::shared_ptr<AssimpModel> model = instance->getModel();
  ModelSettings modelSettings = model->getModelSettings();

  if (B3_IS_NON_NULL(instSettings.isPhysicsBodyId)) {
    b3DestroyBody(instSettings.isPhysicsBodyId);
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
  instSettings.isPhysicsBodyId = b3CreateBody(mBox3DWorldId, &bodyDef);

  b3ShapeDef shapeDef = b3DefaultShapeDef();
  shapeDef.density = 1.0f;
  shapeDef.baseMaterial.friction = modelSettings.msPhysicsFrictionCoeff;
  shapeDef.baseMaterial.restitution = modelSettings.msPhysicsRestitutionCoeff;

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
        // needs a lock to avoid object updates during physics step
        mPhysicsTimer.callExtFunctionLocked([&, this]() {
          createBox3dPhysicsObject(renderData, instance);
        });
      }
    }
  }
}

void Physics::doBox3dStep(ModelInstanceCamData &modInstCamData) {
  // TODO: make configurable or move to class header as constants
  float timeStep = 1.0f / 60.0f;
  int subStepCount = 4;

  b3World_Step(mBox3DWorldId, timeStep, subStepCount);
  updateObjectsFromBox3d(modInstCamData);
}

void Physics::updateObjectsFromBox3d(ModelInstanceCamData &modInstCamData) {
  for (const auto& model : modInstCamData.micModelList) {
    ModelSettings modelSettings = model->getModelSettings();
    if (modelSettings.msPhysicsEnabled) {
      std::vector<std::shared_ptr<AssimpInstance>> instances = modInstCamData.micAssimpInstancesPerModel[model->getModelFileName()];

      for (auto instance : instances) {
        InstanceSettings instSettings = instance->getInstanceSettings();

        if (B3_IS_NON_NULL(instSettings.isPhysicsBodyId)) {
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
}

void Physics::updateBox3dPhysics(VkRenderData &renderData, ModelInstanceCamData &modInstCamData) {
  renderData.rdPhysicsTime = 0.0f;
  renderData.rdPhysicsTimer.start();

  if (renderData.rdPhysicsRunning) {
    doBox3dStep(modInstCamData);
  }

  renderData.rdPhysicsTime = renderData.rdPhysicsTimer.stop();
}

void Physics::cleanupBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstCamData) {
  mPhysicsTimer.stopTimer();
  mPhysicsTimer.cleanupFunctions();

  if (B3_IS_NON_NULL(mBox3DWorldId)) {
    b3DestroyWorld(mBox3DWorldId);
  }

  // cleanup body ids of instances too
  for (const auto& model : modInstCamData.micModelList) {
    ModelSettings modelSettings = model->getModelSettings();
    if (modelSettings.msPhysicsEnabled) {
      std::vector<std::shared_ptr<AssimpInstance>> instances = modInstCamData.micAssimpInstancesPerModel[model->getModelFileName()];

      for (auto instance : instances) {
        InstanceSettings instSettings = instance->getInstanceSettings();
        instSettings.isPhysicsBodyId = b3BodyId{};

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

void Physics::resetBox3d(VkRenderData &renderData, ModelInstanceCamData &modInstaCamData) {
  cleanupBox3d(renderData, modInstaCamData);
  initBox3d(renderData, modInstaCamData);
  createBox3dPhysicsObjects(renderData, modInstaCamData);
}
