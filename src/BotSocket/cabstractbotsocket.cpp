#include "cabstractbotsocket.h"

#include <TopoDS_Shape.hxx>

#include "cabstractui.h"

static class CEmptyUi : public CAbstractUi
{
    friend class CAbstractBotSocket;

protected:
    void prepareComplete(const BotSocket::EN_PrepareResult) final { }
    void tasksComplete(const BotSocket::EN_WorkResult) final { }
    void socketStateChanged(const BotSocket::EN_BotState) final { }
    void laserHeadPositionChanged(const BotSocket::SBotPosition &) final { }
    void gripPositionChanged(const BotSocket::SBotPosition &) final { }
    const TopoDS_Shape& getShape(const BotSocket::EN_ShapeType) const final { return shape; }
    const gp_Trsf& getShapeTransform(const BotSocket::EN_ShapeType) const final { return transform; }
    void shapeCalibrationChanged(const BotSocket::EN_ShapeType, const BotSocket::SBotPosition &) final {}
    void shapeTransformChanged(const BotSocket::EN_ShapeType, const gp_Trsf &) final {}

    void makePartSnapshot(const char *) final { }
    void snapshotCalibrationDataRecieved(const gp_Vec &) final { }
    bool execSnapshotCalibrationWarning() final { return false; }
private:
    TopoDS_Shape shape;
    gp_Trsf transform;
} emptyUi;



CAbstractBotSocket::~CAbstractBotSocket()
{

}

CAbstractBotSocket::CAbstractBotSocket() :
    ui(&emptyUi)
{

}

void CAbstractBotSocket::prepareComplete(const BotSocket::EN_PrepareResult result)
{
    ui->prepareComplete(result);
}

void CAbstractBotSocket::tasksComplete(const BotSocket::EN_WorkResult result)
{
    ui->tasksComplete(result);
}

void CAbstractBotSocket::socketStateChanged(const BotSocket::EN_BotState state)
{
    ui->socketStateChanged(state);
}

void CAbstractBotSocket::laserHeadPositionChanged(const BotSocket::SBotPosition &pos)
{
    ui->laserHeadPositionChanged(pos);
}

void CAbstractBotSocket::gripPositionChanged(const BotSocket::SBotPosition &pos)
{
    ui->gripPositionChanged(pos);
}

void CAbstractBotSocket::shapeCalibrationChanged(const BotSocket::EN_ShapeType shType, const BotSocket::SBotPosition &pos)
{
    ui->shapeCalibrationChanged(shType, pos);
}

void CAbstractBotSocket::shapeTransformChanged(const BotSocket::EN_ShapeType shType, const gp_Trsf &transform)
{
    ui->shapeTransformChanged(shType, transform);
}

const TopoDS_Shape &CAbstractBotSocket::getShape(const BotSocket::EN_ShapeType shType) const
{
    return ui->getShape(shType);
}

const gp_Trsf &CAbstractBotSocket::getShapeTransform(const BotSocket::EN_ShapeType shType) const
{
    return ui->getShapeTransform(shType);
}

void CAbstractBotSocket::makePartSnapshot(const char *fname)
{
    ui->makePartSnapshot(fname);
}

void CAbstractBotSocket::snapshotCalibrationDataRecieved(const gp_Vec &globalDelta)
{
    ui->snapshotCalibrationDataRecieved(globalDelta);
}

bool CAbstractBotSocket::execSnapshotCalibrationWarning()
{
    return ui->execSnapshotCalibrationWarning();
}
