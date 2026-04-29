#pragma once
#include <QDialog>
#include <cstdint>

extern "C" {
#include "config.h"
}

class DeviceLink;
class QLabel;
class QPushButton;
class QProgressBar;
class QTimer;
class QSpinBox;

// Modal calibration wizard for a single axis.
//
// Flow:
//   1. User selects an ADC source for the axis.
//   2. "Capture extremes" — user moves the axis to both ends; we record
//      min/max of the live raw stream.
//   3. "Capture center" — user releases to neutral; we average the stream.
//   4. User reviews values, optionally tweaks deadzone, hits Apply.
//
// The dialog mutates a copy of axis_cfg_t which the caller reads via
// resultAxis() on accept().
class AxisCalibrator : public QDialog {
    Q_OBJECT
public:
    AxisCalibrator(DeviceLink *link, int axisIndex, const axis_cfg_t &initial,
                   QWidget *parent = nullptr);

    axis_cfg_t resultAxis() const { return result_; }

private slots:
    void onTick();
    void onCaptureExtremesClicked();
    void onCaptureCenterClicked();
    void onAccept();

private:
    enum class Phase { Idle, Extremes, Center, Done };
    void setPhase(Phase p);
    void updateInstruction();

    DeviceLink *link_;
    int         axisIndex_;
    axis_cfg_t  result_;

    QTimer *timer_;
    Phase   phase_ = Phase::Idle;

    // Aggregates during capture.
    uint16_t seenMin_ = 4095;
    uint16_t seenMax_ = 0;
    uint64_t centerSum_ = 0;
    uint32_t centerCnt_ = 0;

    QLabel       *instruction_ = nullptr;
    QLabel       *liveLabel_   = nullptr;
    QProgressBar *liveBar_     = nullptr;
    QLabel       *minLabel_    = nullptr;
    QLabel       *maxLabel_    = nullptr;
    QLabel       *centerLabel_ = nullptr;
    QSpinBox     *deadzoneSpin_= nullptr;
    QPushButton  *btnExtremes_ = nullptr;
    QPushButton  *btnCenter_   = nullptr;
    QPushButton  *btnApply_    = nullptr;
};
