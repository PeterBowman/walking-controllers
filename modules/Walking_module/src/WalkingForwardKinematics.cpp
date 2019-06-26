/**
 * @file WalkingForwardKinematics.cpp
 * @authors Giulio Romualdi <giulio.romualdi@iit.it>
 * @copyright 2018 iCub Facility - Istituto Italiano di Tecnologia
 *            Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 * @date 2018
 */

// std
#include <cmath>

// YARP
#include <yarp/os/LogStream.h>
#include <yarp/os/Value.h>

// iDynTree
#include <iDynTree/Core/EigenHelpers.h>
#include <iDynTree/yarp/YARPConversions.h>
#include <iDynTree/yarp/YARPEigenConversions.h>
#include <iDynTree/Model/Model.h>

#include "WalkingForwardKinematics.hpp"
#include "Utils.hpp"

bool WalkingFK::setRobotModel(const iDynTree::Model& model)
{
    if(!m_kinDyn.loadRobotModel(model))
    {
        yError() << "[WalkingFK::setRobotModel] Error while loading into KinDynComputations object.";
        return false;
    }

    m_kinDyn.setFrameVelocityRepresentation(iDynTree::MIXED_REPRESENTATION);

    m_generalizedBiasForces.resize(model);

    // initialize some quantities needed for the first step
    m_prevContactLeft = false;

    return true;
}


bool WalkingFK::setBaseFrame(const std::string& baseFrame, const std::string& name)
{
    if(!m_kinDyn.isValid())
    {
        yError() << "[setBaseFrame] Please set the Robot model before calling this method.";
        return false;
    }

    iDynTree::FrameIndex frameBaseIndex = m_kinDyn.model().getFrameIndex(baseFrame);
    if(frameBaseIndex == iDynTree::FRAME_INVALID_INDEX)
    {
        yError() << "[setBaseFrames] Unable to find the frame named: " << baseFrame;
        return false;
    }
    // get the link where the desired baseFrame is attached
    iDynTree::LinkIndex linkBaseIndex = m_kinDyn.model().getFrameLink(frameBaseIndex);

    // get the main frame of the link. In iDynTree the base frame has to be the main frame
    // of the link
    std::string baseFrameName = m_kinDyn.model().getLinkName(linkBaseIndex);

    m_baseFrames.insert({name, std::make_pair(baseFrameName,
                                              m_kinDyn.getRelativeTransform(frameBaseIndex,
                                                                            linkBaseIndex))});
    return true;
}


bool WalkingFK::initialize(const yarp::os::Searchable& config,
                           const iDynTree::Model& model)
{
    // check if the config is empty
    if(!setRobotModel(model))
    {
        yError() << "[WalkingFK::initialize] Unable to set the robot model.";
        return false;
    }

    if(config.isNull())
    {
        yError() << "[WalkingFK::initialize] Empty configuration for fk solver.";
        return false;
    }

    // set the left foot frame
    std::string lFootFrame;
    if(!YarpHelper::getStringFromSearchable(config, "left_foot_frame", lFootFrame))
    {
        yError() << "[WalkingFK::initialize] Unable to get the string from searchable.";
        return false;
    }
    m_frameLeftIndex = m_kinDyn.model().getFrameIndex(lFootFrame);
    if(m_frameLeftIndex == iDynTree::FRAME_INVALID_INDEX)
    {
        yError() << "[WalkingFK::initialize] Unable to find the frame named: " << lFootFrame;
        return false;
    }

    // set the right foot frame
    std::string rFootFrame;
    if(!YarpHelper::getStringFromSearchable(config, "right_foot_frame", rFootFrame))
    {
        yError() << "[WalkingFK::initialize] Unable to get the string from searchable.";
        return false;
    }
    m_frameRightIndex = m_kinDyn.model().getFrameIndex(rFootFrame);
    if(m_frameRightIndex == iDynTree::FRAME_INVALID_INDEX)
    {
        yError() << "[WalkingFK::initialize] Unable to find the frame named: " << rFootFrame;
        return false;
    }

    // set the left hand frame
    std::string lHandFrame;
    if(!YarpHelper::getStringFromSearchable(config, "left_hand_frame", lHandFrame))
    {
        yError() << "[WalkingFK::initialize] Unable to get the string from searchable.";
        return false;
    }
    m_frameLeftHandIndex = m_kinDyn.model().getFrameIndex(lHandFrame);
    if(m_frameLeftHandIndex == iDynTree::FRAME_INVALID_INDEX)
    {
        yError() << "[WalkingFK::initialize] Unable to find the frame named: " << lHandFrame;
        return false;
    }

    // set the right hand frame
    std::string rHandFrame;
    if(!YarpHelper::getStringFromSearchable(config, "right_hand_frame", rHandFrame))
    {
        yError() << "[WalkingFK::initialize] Unable to get the string from searchable.";
        return false;
    }
    m_frameRightHandIndex = m_kinDyn.model().getFrameIndex(rHandFrame);
    if(m_frameRightHandIndex == iDynTree::FRAME_INVALID_INDEX)
    {
        yError() << "[WalkingFK::initialize] Unable to find the frame named: " << rHandFrame;
        return false;
    }

    std::string headFrame;
    if(!YarpHelper::getStringFromSearchable(config, "head_frame", headFrame))
    {
        yError() << "[WalkingFK::initialize] Unable to get the string from searchable.";
        return false;
    }
    m_frameHeadIndex = m_kinDyn.model().getFrameIndex(headFrame);
    if(m_frameHeadIndex == iDynTree::FRAME_INVALID_INDEX)
    {
        yError() << "[WalkingFK::initialize] Unable to find the frame named: " << headFrame;
        return false;
    }

    std::string rootFrame;
    if(!YarpHelper::getStringFromSearchable(config, "root_frame", rootFrame))
    {
        yError() << "[WalkingFK::initialize] Unable to get the string from searchable.";
        return false;
    }
    m_frameRootIndex = m_kinDyn.model().getFrameIndex(rootFrame);
    if(m_frameRootIndex == iDynTree::FRAME_INVALID_INDEX)
    {
        yError() << "[WalkingFK::initialize] Unable to find the frame named: " << rootFrame;
        return false;
    }

    std::string torsoFrame;
    if(!YarpHelper::getStringFromSearchable(config, "torso_frame", torsoFrame))
    {
        yError() << "[WalkingFK::initialize] Unable to get the string from searchable.";
        return false;
    }
    m_frameNeckIndex = m_kinDyn.model().getFrameIndex(torsoFrame);
    if(m_frameNeckIndex == iDynTree::FRAME_INVALID_INDEX)
    {
        yError() << "[WalkingFK::initialize] Unable to find the frame named: " << torsoFrame;
        return false;
    }

    m_useExternalRobotBase = config.check("use_external_robot_base", yarp::os::Value("False")).asBool();

    if(!m_useExternalRobotBase)
    {
        if(!setBaseFrame(lFootFrame, "leftFoot"))
        {
            yError() << "[initialize] Unable to set the leftFootFrame.";
            return false;
        }

        if(!setBaseFrame(rFootFrame, "rightFoot"))
        {
            yError() << "[initialize] Unable to set the rightFootFrame.";
            return false;
        }

        // Since the base is attached to the stance foot its velocity is always equal to zero
        // (stable contact hypothesis)
        m_baseTwist.zero();
    }
    else
    {
        if(!setBaseFrame(rootFrame, "root"))
        {
            yError() << "[initialize] Unable to set the rightFootFrame.";
            return false;
        }

        // in this specific case the base is always the root link
        if(!m_kinDyn.setFloatingBase(m_baseFrames["root"].first))
        {
            yError() << "[initialize] Unable to set the floating base";
            return false;
        }
    }

    double comHeight;
    if(!YarpHelper::getNumberFromSearchable(config, "com_height", comHeight))
    {
        yError() << "[WalkingFK::initialize] Unable to get the double from searchable.";
        return false;
    }
    double gravityAcceleration = config.check("gravity_acceleration", yarp::os::Value(9.81)).asDouble();
    m_omega = sqrt(gravityAcceleration / comHeight);

    // init filters
    double samplingTime;
    if(!YarpHelper::getNumberFromSearchable(config, "sampling_time", samplingTime))
    {
        yError() << "[WalkingFK::initialize] Unable to get the double from searchable.";
        return false;
    }

    double cutFrequency;
    if(!YarpHelper::getNumberFromSearchable(config, "cut_frequency", cutFrequency))
    {
        yError() << "[WalkingFK::initialize] Unable to get the double from searchable.";
        return false;
    }

    m_comPositionFiltered.zero();
    m_comVelocityFiltered.zero();
    m_comPositionFiltered(2) = comHeight;


    m_comPositionFilter = std::make_unique<iCub::ctrl::FirstOrderLowPassFilter>(cutFrequency, samplingTime);
    m_comVelocityFilter = std::make_unique<iCub::ctrl::FirstOrderLowPassFilter>(cutFrequency, samplingTime);

    // TODO this is wrong, we shold initialize the filter with a meaningful value;
    yarp::sig::Vector buff(3);
    iDynTree::toYarp(m_comPositionFiltered, buff);
    m_comPositionFilter->init(buff);

    iDynTree::toYarp(m_comVelocityFiltered, buff);
    m_comVelocityFilter->init(buff);

    m_useFilters = config.check("use_filters", yarp::os::Value(false)).asBool();
    m_firstStep = true;
    return true;
}

void WalkingFK::evaluateWorldToBaseTransformation(const iDynTree::Transform& rootTransform,
                                                  const iDynTree::Twist& rootTwist)
{
    if(!m_useExternalRobotBase)
    {
        yWarning() << "[evaluateWorldToBaseTransformation] The base position is not retrieved from external. There is no reason to call this function.";
        return;
    }

    auto& base = m_baseFrames["root"];
    m_worldToBaseTransform = rootTransform * base.second;
    m_baseTwist = rootTwist;

    m_comEvaluated = false;
    m_dcmEvaluated = false;
    return;
}

bool WalkingFK::evaluateWorldToBaseTransformation(const iDynTree::Transform& leftFootTransform,
                                                  const iDynTree::Transform& rightFootTransform,
                                                  const bool& isLeftFixedFrame)
{
    if(m_useExternalRobotBase)
    {
        yWarning() << "[evaluateWorldToBaseTransformation] The base position is retrieved from external. There is no reason on using odometry.";
        return true;
    }

    if(isLeftFixedFrame)
    {
        // evaluate the new world to base transformation only if the previous fixed frame was
        // the right foot
        if(!m_prevContactLeft || m_firstStep)
        {
            auto& base = m_baseFrames["leftFoot"];
            m_worldToBaseTransform = leftFootTransform * base.second;
            if(!m_kinDyn.setFloatingBase(base.first))
            {
                yError() << "[evaluateWorldToBaseTransformation] Error while setting the floating "
                         << "base on link " << base.first;
                return false;
            }
            m_prevContactLeft = true;
        }
    }
    else
    {
        // evaluate the new world to base transformation only if the previous if the previous fixed frame was
        // the left foot
        if(m_prevContactLeft || m_firstStep)
        {
            auto base = m_baseFrames["rightFoot"];
            m_worldToBaseTransform = rightFootTransform * base.second;
            if(!m_kinDyn.setFloatingBase(base.first))
            {
                yError() << "[evaluateWorldToBaseTransformation] Error while setting the floating "
                         << "base on link " << base.first;
                return false;
            }
            m_prevContactLeft = false;
        }
    }

    if(m_firstStep)
        m_firstStep = false;

    m_comEvaluated = false;
    m_dcmEvaluated = false;

    return true;
}


bool WalkingFK::setInternalRobotState(const iDynTree::VectorDynSize& positionFeedbackInRadians,
                                      const iDynTree::VectorDynSize& velocityFeedbackInRadians)
{
    iDynTree::Vector3 gravity;
    gravity.zero();
    gravity(2) = -9.81;

    if(!m_kinDyn.setRobotState(m_worldToBaseTransform, positionFeedbackInRadians,
                               m_baseTwist, velocityFeedbackInRadians,
                               gravity))
    {
        yError() << "[WalkingFK::setInternalRobotState] Error while updating the state.";
        return false;
    }

    m_comEvaluated = false;
    m_dcmEvaluated = false;

    return true;
}

void WalkingFK::evaluateCoM()
{
    if(m_comEvaluated)
        return;

    m_comPosition = m_kinDyn.getCenterOfMassPosition();
    m_comVelocity = m_kinDyn.getCenterOfMassVelocity();

    yarp::sig::Vector temp;
    temp.resize(3);

    iDynTree::toYarp(m_comPosition, temp);
    iDynTree::toEigen(m_comPositionFiltered) = iDynTree::toEigen(m_comPositionFilter->filt(temp));

    iDynTree::toYarp(m_comVelocity, temp);
    iDynTree::toEigen(m_comVelocityFiltered) = iDynTree::toEigen(m_comVelocityFilter->filt(temp));

    m_comEvaluated = true;

    return;
}

void WalkingFK::evaluateDCM()
{
    if(m_dcmEvaluated)
        return;

    evaluateCoM();

    // evaluate the 3D-DCM
    if(m_useFilters)
        iDynTree::toEigen(m_dcm) = iDynTree::toEigen(m_comPositionFiltered) +
            iDynTree::toEigen(m_comVelocityFiltered) / m_omega;
    else
        iDynTree::toEigen(m_dcm) = iDynTree::toEigen(m_comPosition) +
            iDynTree::toEigen(m_comVelocity) / m_omega;

    m_dcmEvaluated = true;

    return;
}

const iDynTree::Vector3& WalkingFK::getDCM()
{
    evaluateDCM();
    return m_dcm;
}

const iDynTree::Position& WalkingFK::getCoMPosition()
{
    evaluateCoM();

    if(m_useFilters)
        return m_comPositionFiltered;
    else
        return m_comPosition;
}

const iDynTree::Vector3& WalkingFK::getCoMVelocity()
{
    evaluateCoM();

    if(m_useFilters)
        return m_comVelocityFiltered;
    else
        return m_comVelocity;
}

bool WalkingFK::setBaseOnTheFly()
{
    // TODO understand how to change this function when the base is provided by an external module
    if(m_useExternalRobotBase)
    {
        yError() << "[setBaseOnTheFly] If the base comes from the external you cannot use the onthefly. (This feature will be implemented in a second moment)";
        return false;
    }

    auto base = m_baseFrames["leftFoot"];
    m_worldToBaseTransform = base.second;
    if(!m_kinDyn.setFloatingBase(base.first))
    {
        yError() << "[setBaseOnTheFly] Error while setting the floating base on link "
                 << base.first;
        return false;
    }

    return true;
}

iDynTree::Transform WalkingFK::getLeftFootToWorldTransform()
{
    return m_kinDyn.getWorldTransform(m_frameLeftIndex);
}

iDynTree::Transform WalkingFK::getRightFootToWorldTransform()
{
    return m_kinDyn.getWorldTransform(m_frameRightIndex);
}

iDynTree::Transform WalkingFK::getLeftHandToWorldTransform()
{
    return m_kinDyn.getWorldTransform(m_frameLeftHandIndex);
}

iDynTree::Transform WalkingFK::getRightHandToWorldTransform()
{
    return m_kinDyn.getWorldTransform(m_frameRightHandIndex);
}

iDynTree::Transform WalkingFK::getHeadToWorldTransform()
{
    return m_kinDyn.getWorldTransform(m_frameHeadIndex);
}

iDynTree::Transform WalkingFK::getRootLinkToWorldTransform()
{
    return m_kinDyn.getWorldTransform(m_frameRootIndex);
}

iDynTree::Twist WalkingFK::getRootLinkVelocity()
{
    return m_kinDyn.getFrameVel(m_frameRootIndex);
}

iDynTree::Twist WalkingFK::getLeftFootVelocity()
{
    return m_kinDyn.getFrameVel(m_frameLeftIndex);
}

iDynTree::Twist WalkingFK::getRightFootVelocity()
{
    return m_kinDyn.getFrameVel(m_frameRightIndex);
}

iDynTree::Rotation WalkingFK::getNeckOrientation()
{
    return m_kinDyn.getWorldTransform(m_frameNeckIndex).getRotation();
}

iDynTree::Twist WalkingFK::getNeckVelocity()
{
    return m_kinDyn.getFrameVel(m_frameNeckIndex);
}

bool WalkingFK::getLeftFootJacobian(iDynTree::MatrixDynSize &jacobian)
{
    return m_kinDyn.getFrameFreeFloatingJacobian(m_frameLeftIndex, jacobian);
}

bool WalkingFK::getRightFootJacobian(iDynTree::MatrixDynSize &jacobian)
{
    return m_kinDyn.getFrameFreeFloatingJacobian(m_frameRightIndex, jacobian);
}

bool WalkingFK::getRightHandJacobian(iDynTree::MatrixDynSize &jacobian)
{
    return m_kinDyn.getFrameFreeFloatingJacobian(m_frameRightHandIndex, jacobian);
}

bool WalkingFK::getLeftHandJacobian(iDynTree::MatrixDynSize &jacobian)
{
    return m_kinDyn.getFrameFreeFloatingJacobian(m_frameLeftHandIndex, jacobian);
}

bool WalkingFK::getNeckJacobian(iDynTree::MatrixDynSize &jacobian)
{
    return m_kinDyn.getFrameFreeFloatingJacobian(m_frameNeckIndex, jacobian);
}

bool WalkingFK::getCoMJacobian(iDynTree::MatrixDynSize &jacobian)
{
    return m_kinDyn.getCenterOfMassJacobian(jacobian);
}

iDynTree::Vector3 WalkingFK::getCoMBiasAcceleration()
{
    return m_kinDyn.getCenterOfMassBiasAcc();
}

iDynTree::Vector6 WalkingFK::getLeftFootBiasAcceleration()
{
    return m_kinDyn.getFrameBiasAcc(m_frameLeftIndex);
}

iDynTree::Vector6 WalkingFK::getRightFootBiasAcceleration()
{
    return m_kinDyn.getFrameBiasAcc(m_frameRightIndex);
}

iDynTree::Vector6 WalkingFK::getNeckBiasAcceleration()
{
    return m_kinDyn.getFrameBiasAcc(m_frameNeckIndex);
}

bool WalkingFK::getFreeFloatingMassMatrix(iDynTree::MatrixDynSize &freeFloatingMassMatrix)
{
    return m_kinDyn.getFreeFloatingMassMatrix(freeFloatingMassMatrix);
}

bool WalkingFK::getGeneralizedBiasForces(iDynTree::VectorDynSize &generalizedBiasForces)
{
    if(!m_kinDyn.generalizedBiasForces(m_generalizedBiasForces))
    {
        yError() << "[WalkingFK::generalizedBiasForces] Unable to get the generalized bias forces";
        return false;
    }

    iDynTree::toEigen(generalizedBiasForces).block(0,0,6,1) = iDynTree::toEigen(m_generalizedBiasForces.baseWrench());
    iDynTree::toEigen(generalizedBiasForces).block(6,0,m_generalizedBiasForces.jointTorques().size(), 1) = iDynTree::toEigen(m_generalizedBiasForces.jointTorques());
    return true;
}

iDynTree::SpatialMomentum WalkingFK::getCentroidalTotalMomentum()
{
    return m_kinDyn.getCentroidalTotalMomentum();
}

bool WalkingFK::getLinearAngularMomentumJacobian(iDynTree::MatrixDynSize& linAngMomentumJacobian)
{
    return m_kinDyn.getLinearAngularMomentumJacobian(linAngMomentumJacobian);
}
