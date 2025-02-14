#include "cfanucbotsocket.h"

#include <QTimer>
#include <QSettings>
#include <QFile>
#include <QTextStream>
#include <QStringList>

#include <Precision.hxx>

#include "../PartReference/pointpairspartreferencer.h"
#include "../log/loguru.hpp"

CFanucBotSocket::CFanucBotSocket() :
    CAbstractBotSocket(),
    lastTaskDelay(0),
    calibWaitCounter(0),
    bNeedCalib(false)
{
    VLOG_CALL;


    QSettings settings("fanuc.ini", QSettings::IniFormat);

    if(settings.contains("world2user") && settings.contains("user2world"))
    {
        QList<QVariant> w2u_v = settings.value("world2user").toList();
        QList<QVariant> u2w_v = settings.value("user2world").toList();

        if(w2u_v.size() >= 12 && u2w_v.size() >= 12)
        {
            QList<double> w2u_d, u2w_d;
            for(QVariant v : w2u_v)
                w2u_d.push_back(v.toDouble());
            for(QVariant v : u2w_v)
                u2w_d.push_back(v.toDouble());

            world2user_.SetValues(w2u_d[0],w2u_d[1],w2u_d[2],w2u_d[3],
                                  w2u_d[4],w2u_d[5],w2u_d[6],w2u_d[7],
                                  w2u_d[8],w2u_d[9],w2u_d[10],w2u_d[11]);
            user2world_.SetValues(u2w_d[0],u2w_d[1],u2w_d[2],u2w_d[3],
                                  u2w_d[4],u2w_d[5],u2w_d[6],u2w_d[7],
                                  u2w_d[8],u2w_d[9],u2w_d[10],u2w_d[11]);
        }
    }
    flip_ = settings.value("flip", flip_).toBool();
    up_ = settings.value("up", up_).toBool();
    top_ = settings.value("top", top_).toBool();
    camDelay_ = settings.value("cam_delay", 3000).toInt();

    connect(&fanuc_state_, &FanucStateSocket::xyzwpr_position_received, this, &CFanucBotSocket::updatePosition);

    connect(&fanuc_state_, &FanucStateSocket::connection_state_changed, this, &CFanucBotSocket::updateConnectionState);

//    connect(&fanuc_state_, &FanucStateSocket::status_received, [&](bool moving, bool ready_to_move, bool error){
//        moving_ = moving;
//    });

    connect(&fanuc_relay_, &FanucRelaySocket::connection_state_changed, this, &CFanucBotSocket::updateConnectionState);
    // TODO: another signal / monitor position
    connect(&fanuc_relay_, &FanucRelaySocket::trajectory_enqueue_finished, this, [&](){
        completePath(BotSocket::ENWR_OK);
    });
    connect(&fanuc_relay_, &FanucRelaySocket::trajectory_xyzwpr_point_enqueue_fail, this, [&](){
        completePath(BotSocket::ENWR_ERROR);
    });
}


xyzwpr_data transform(const xyzwpr_data &p, const gp_Trsf &t)
{
    gp_Trsf pos_src;
    gp_Quaternion r;

    r.SetEulerAngles(gp_Extrinsic_XYZ, p.xyzwpr[3] * M_PI/180, p.xyzwpr[4] * M_PI/180, p.xyzwpr[5] * M_PI/180);
    pos_src.SetRotation(r);
    pos_src.SetTranslationPart(gp_Vec(p.xyzwpr[0], p.xyzwpr[1], p.xyzwpr[2]));

    gp_Trsf pos_result = t * pos_src;

    xyzwpr_data p_result = p;
    p_result.xyzwpr[0] = pos_result.TranslationPart().X();
    p_result.xyzwpr[1] = pos_result.TranslationPart().Y();
    p_result.xyzwpr[2] = pos_result.TranslationPart().Z();

    r = pos_result.GetRotation().Normalized();

    r.GetEulerAngles(gp_Extrinsic_XYZ, p_result.xyzwpr[3], p_result.xyzwpr[4], p_result.xyzwpr[5]);

    p_result.xyzwpr[3] *= 180/M_PI, p_result.xyzwpr[4] *= 180/M_PI, p_result.xyzwpr[5] *= 180/M_PI;

    return p_result;
}

BotSocket::SBotPosition xyzwpr2botposition(const xyzwpr_data &pos, const gp_Trsf &world2user)
{
    xyzwpr_data p = transform(pos, world2user);
    return BotSocket::SBotPosition(p.xyzwpr[0], p.xyzwpr[1], p.xyzwpr[2], p.xyzwpr[3], p.xyzwpr[4], p.xyzwpr[5]);
}

xyzwpr_data botposition2xyzwpr(const GUI_TYPES::SVertex &globalPos, const GUI_TYPES::SRotationAngle &angle, const GUI_TYPES::SVertex &normal, const gp_Trsf &user2world)
{
    xyzwpr_data p;

    //normal represent as the ZAxis
    gp_Dir zDir(normal.x, normal.y, normal.z);

    //normal rotation angles from global coord system
    gp_Quaternion normal_q(gp_Vec(gp_Dir(0., 0., 1.)), gp_Vec(zDir));

    //delta rotation from global coord system
    gp_Quaternion delta;
    delta.SetEulerAngles(gp_Extrinsic_XYZ,
                         angle.x * M_PI / 180.,
                         angle.y * M_PI / 180.,
                         angle.z * M_PI / 180.);

    //final rotation from global coord system
    gp_Quaternion finalRotationZAxis = normal_q * delta;

    double ax = 0, ay = 0, az = 0;
    finalRotationZAxis.GetEulerAngles(gp_Extrinsic_XYZ,
                         ax,
                         ay,
                         az);

    p.xyzwpr = {globalPos.x, globalPos.y, globalPos.z, ax * 180/M_PI, ay * 180/M_PI, az * 180/M_PI};

    return transform(p, user2world);
}

xyzwpr_data botposition2xyzwpr(const GUI_TYPES::STaskPoint &pos, const gp_Trsf &user2world)
{
    return botposition2xyzwpr(pos.globalPos, pos.angle, pos.normal, user2world);
}

xyzwpr_data botposition2xyzwpr(const GUI_TYPES::SHomePoint &pos, const gp_Trsf &user2world)
{
    return botposition2xyzwpr(pos.globalPos, pos.angle, pos.normal, user2world);
}

void CFanucBotSocket::updatePosition(const xyzwpr_data &pos)
{
    laserHeadPositionChanged(xyzwpr2botposition(pos, world2user_));
}

void CFanucBotSocket::prepare(const std::vector <GUI_TYPES::STaskPoint> &)
{
    prepareComplete(BotSocket::EN_PrepareResult::ENPR_OK);
}

void CFanucBotSocket::updateConnectionState()
{
    bool ok = fanuc_state_.connected() && fanuc_relay_.connected();
    if(!fanuc_state_.connected() && fanuc_relay_.connected())
    {
        fanuc_relay_.disconnectFromHost();
    }
    socketStateChanged(ok ? BotSocket::ENBS_NOT_ATTACHED
                          : BotSocket::ENBS_FALL);
}

void CFanucBotSocket::completePath(const BotSocket::EN_WorkResult result)
{
    VLOG_CALL;

    if (result != BotSocket::ENWR_OK)
    {
        curTask.clear();
        tasksComplete(result);
        return;
    }

    if (bNeedCalib)
    {
        LOG_F(INFO, "need calibration");
        bNeedCalib = false;
        QFile calibResFile("calib_result.txt");
        if (calibResFile.exists())
            calibResFile.remove();

        QTimer::singleShot(camDelay_, this, [this](){
            makePartSnapshot("snapshot.bmp");
        });

        calibWaitCounter = 0;
        QTimer::singleShot(camDelay_ + 1000, this, &CFanucBotSocket::slCalibWaitTimeout);
    }
    else if(lastTaskDelay > 0)
    {
        LOG_F(INFO, "task delay %d", lastTaskDelay);
        QTimer::singleShot(lastTaskDelay, this, [&]() {
            completePath(BotSocket::ENWR_OK);
        });
        lastTaskDelay = 0;
    }
    else if (curTask.empty())
    {
        LOG_F(INFO, "finish");
        tasksComplete(result); //result == BotSocket::ENWR_OK
        return;
    }
    else
    {
        GUI_TYPES::STaskPoint &p = curTask.front();
        LOG_F(INFO, "Task %d (home=%d, calib=%d): xyz = %f %f %f normal = %f %f %f angle = %f %f %f",
              p.taskType, p.bUseHomePnt, p.bNeedCalib,
              p.globalPos.x, p.globalPos.y, p.globalPos.z,
              p.normal.x, p.normal.y, p.normal.z,
              p.angle.x, p.angle.y, p.angle.z);

        if(p.bUseHomePnt && !homePoints.empty())
        {
            LOG_F(INFO, "home point");
            xyzwpr_data point = botposition2xyzwpr(homePoints[0], user2world_);
            point.flip = flip_;
            point.up = up_;
            point.top = top_;
            p.bUseHomePnt = false;
            fanuc_relay_.move_point(point);
        }
        else
        {
            xyzwpr_data point = botposition2xyzwpr(p, user2world_);
            point.flip = flip_;
            point.up = up_;
            point.top = top_;
            lastTaskDelay = static_cast <int> (p.delay * 1000.);
            bNeedCalib = p.bNeedCalib;
            // if point needs calibration, move to it after calibration
            if(bNeedCalib)
                p.bNeedCalib = false;
            else
                curTask.erase(curTask.begin());
            fanuc_relay_.move_point(point);
        }
    }
}

void CFanucBotSocket::calibFinish(const gp_Vec &delta)
{
    VLOG_CALL;

    if (!delta.IsEqual(gp_Vec(), Precision::Confusion(), Precision::Angular()))
    {
        //Current task correction
        gp_Trsf lHeadPos = getShapeTransform(BotSocket::ENST_LSRHEAD);
        gp_Quaternion quatLsrHead = lHeadPos.GetRotation();
        const gp_Vec rotatedDelta = quatLsrHead.Multiply(delta);

        LOG_F(INFO, "Delta: %f %f %f; Rotated delta: %f %f %f", delta.X(), delta.Y(), delta.Z(), rotatedDelta.X(), rotatedDelta.Y(), rotatedDelta.Z());

        for (auto &p : curTask)
        {
            p.globalPos.x += rotatedDelta.X();
            p.globalPos.y += rotatedDelta.Y();
            p.globalPos.z += rotatedDelta.Z();
        }
        snapshotCalibrationDataRecieved(rotatedDelta);
    }
    completePath(BotSocket::ENWR_OK);
}

void CFanucBotSocket::slCalibWaitTimeout()
{
    VLOG_CALL;

    static const int CALIB_ATTEMP_COUNT = 3;
    QFile calibResFile("calib_result.txt");
    if (!calibResFile.exists())
    {
        LOG_F(INFO, "Waiting for file; %d time", calibWaitCounter);
        if (calibWaitCounter < CALIB_ATTEMP_COUNT)
        {
            ++calibWaitCounter;
            QTimer::singleShot(1000, this, &CFanucBotSocket::slCalibWaitTimeout);
        }
        else
        {
            LOG_F(WARNING, "No file after timeout");
            calibWaitCounter = 0;
            if (!execSnapshotCalibrationWarning())
            {
                curTask.clear();
                completePath(BotSocket::ENWR_ERROR);
                return;
            }
            else
            {
                calibFinish(gp_Vec());
            }
        }
    }
    else
    {
        LOG_F(INFO, "Calib file found");
        gp_Vec delta;
        if (calibResFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&calibResFile);
            QStringList line = in.readLine().split(";");
            calibResFile.close();

            if (line.size() == 4)
            {
                if (line.at(3).toInt() == 0)
                {
                    LOG_F(INFO, "Delta ok");
                    delta = gp_Vec(line.at(0).toDouble(),
                                   line.at(1).toDouble(),
                                   line.at(2).toDouble());
                }
                else if (!execSnapshotCalibrationWarning())
                {
                    curTask.clear();
                    completePath(BotSocket::ENWR_ERROR);
                    return;
                }
            }
        }
        calibFinish(delta);
    }
}

BotSocket::EN_CalibResult CFanucBotSocket::execCalibration(const std::vector<GUI_TYPES::SCalibPoint> &points)
{
    VLOG_CALL;

    BotSocket::EN_CalibResult result = BotSocket::ENCR_FALL;
    if(points.size() >= 3)
    {
        std::vector<PointPairsPartReferencer::point_pair_t> point_pairs;
        point_pairs.reserve(points.size());
        for(GUI_TYPES::SCalibPoint p : points) {
            point_pairs.emplace_back(PointPairsPartReferencer::point_pair_t{
                                         gp_Vec(p.globalPos.x, p.globalPos.y, p.globalPos.z),
                                         gp_Vec(p.botPos.x, p.botPos.y, p.botPos.z)
                                     });
        }

        PointPairsPartReferencer point_pair_part_referencer;
        point_pair_part_referencer.setPointPairs(point_pairs);

        bool ok = point_pair_part_referencer.referencePart();
        if(!ok)
            return BotSocket::ENCR_FALL;

        gp_Trsf transform = point_pair_part_referencer.getPartToRobotTransformation();

        // This one is simpler, but cannot change values in GUI settings
//        CAbstractBotSocket::shapeTransformChanged(BotSocket::ENST_PART, transform);

        gp_Vec translation = transform.TranslationPart();
        gp_Quaternion rotation = transform.GetRotation();

        GUI_TYPES::SRotationAngle r;
        rotation.GetEulerAngles(gp_Extrinsic_XYZ, r.x, r.y, r.z);
        r.x *= 180. / M_PI;
        r.y *= 180. / M_PI;
        r.z *= 180. / M_PI;

        BotSocket::SBotPosition part_position(translation.X(), translation.Y(), translation.Z(), r.x, r.y, r.z);
        shapeCalibrationChanged(BotSocket::ENST_PART, part_position);

        return BotSocket::ENCR_OK;
    }
    return result;
}

void CFanucBotSocket::startTasks(const std::vector<GUI_TYPES::SHomePoint> &homePoints_,
                                 const std::vector<GUI_TYPES::STaskPoint> &taskPoints)
{
    VLOG_CALL;

    curTask = taskPoints;
    homePoints = homePoints_;
    bNeedCalib = false;
    lastTaskDelay = 0;
    calibWaitCounter = 0;
    completePath(BotSocket::ENWR_OK);
}

void CFanucBotSocket::stopTasks()
{
    VLOG_CALL;
    curTask.clear();
    fanuc_relay_.stop();
}

void CFanucBotSocket::shapeTransformChanged(const BotSocket::EN_ShapeType)
{}
