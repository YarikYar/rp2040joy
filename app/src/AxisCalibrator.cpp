#include "AxisCalibrator.h"
#include "DeviceLink.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

AxisCalibrator::AxisCalibrator(DeviceLink *link, int axisIndex,
                                const axis_cfg_t &initial, QWidget *parent)
    : QDialog(parent), link_(link), axisIndex_(axisIndex), result_(initial) {
    setWindowTitle(tr("Calibrate axis %1").arg(axisIndex));
    setModal(true);

    timer_ = new QTimer(this);
    timer_->setInterval(33);  // ~30 Hz
    connect(timer_, &QTimer::timeout, this, &AxisCalibrator::onTick);

    auto *outer = new QVBoxLayout(this);

    instruction_ = new QLabel;
    instruction_->setWordWrap(true);
    QFont f = instruction_->font(); f.setPointSize(f.pointSize() + 1);
    instruction_->setFont(f);
    outer->addWidget(instruction_);

    auto *liveBox = new QHBoxLayout;
    liveBar_ = new QProgressBar;
    liveBar_->setRange(0, 4095);
    liveLabel_ = new QLabel("—");
    liveLabel_->setMinimumWidth(60);
    liveBox->addWidget(new QLabel(tr("Live:")));
    liveBox->addWidget(liveBar_, 1);
    liveBox->addWidget(liveLabel_);
    outer->addLayout(liveBox);

    auto *form = new QFormLayout;
    minLabel_    = new QLabel(QString::number(initial.raw_min));
    centerLabel_ = new QLabel(QString::number(initial.raw_center));
    maxLabel_    = new QLabel(QString::number(initial.raw_max));
    deadzoneSpin_ = new QSpinBox;
    deadzoneSpin_->setRange(0, 2048);
    deadzoneSpin_->setValue(initial.deadzone);
    form->addRow(tr("raw_min"),    minLabel_);
    form->addRow(tr("raw_center"), centerLabel_);
    form->addRow(tr("raw_max"),    maxLabel_);
    form->addRow(tr("deadzone"),   deadzoneSpin_);
    outer->addLayout(form);

    auto *actions = new QHBoxLayout;
    btnExtremes_ = new QPushButton(tr("Capture extremes"));
    btnCenter_   = new QPushButton(tr("Capture center"));
    actions->addWidget(btnExtremes_);
    actions->addWidget(btnCenter_);
    outer->addLayout(actions);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Cancel);
    btnApply_ = bb->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
    btnApply_->setEnabled(false);
    outer->addWidget(bb);

    connect(btnExtremes_, &QPushButton::clicked, this, &AxisCalibrator::onCaptureExtremesClicked);
    connect(btnCenter_,   &QPushButton::clicked, this, &AxisCalibrator::onCaptureCenterClicked);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, &AxisCalibrator::onAccept);

    setPhase(Phase::Idle);
    timer_->start();
    resize(420, 0);
}

void AxisCalibrator::setPhase(Phase p) {
    phase_ = p;
    btnCenter_->setEnabled(p == Phase::Extremes || p == Phase::Center || p == Phase::Done);
    btnApply_->setEnabled(p == Phase::Done || p == Phase::Center);
    updateInstruction();
}

void AxisCalibrator::updateInstruction() {
    switch (phase_) {
        case Phase::Idle:
            instruction_->setText(tr("Hit \"Capture extremes\" and move the axis through its full range "
                                     "in both directions. Click again to stop."));
            btnExtremes_->setText(tr("Capture extremes"));
            break;
        case Phase::Extremes:
            instruction_->setText(tr("Recording min/max — sweep the axis to both ends, then click again."));
            btnExtremes_->setText(tr("Stop recording"));
            break;
        case Phase::Center:
            instruction_->setText(tr("Release the axis to its rest position, then click \"Capture center\"."));
            btnExtremes_->setText(tr("Recapture extremes"));
            break;
        case Phase::Done:
            instruction_->setText(tr("Done. Adjust deadzone if needed, then Apply."));
            btnExtremes_->setText(tr("Recapture extremes"));
            break;
    }
}

void AxisCalibrator::onTick() {
    auto raw = link_->readRawInput();
    if (!raw) return;
    uint16_t v = raw->axis_raw[axisIndex_];
    liveBar_->setValue(v);
    liveLabel_->setText(QString::number(v));

    if (phase_ == Phase::Extremes) {
        if (v < seenMin_) seenMin_ = v;
        if (v > seenMax_) seenMax_ = v;
        minLabel_->setText(QString::number(seenMin_));
        maxLabel_->setText(QString::number(seenMax_));
    } else if (phase_ == Phase::Center) {
        centerSum_ += v;
        centerCnt_ += 1;
        if (centerCnt_ > 0) {
            centerLabel_->setText(QString::number((uint16_t)(centerSum_ / centerCnt_)));
        }
    }
}

void AxisCalibrator::onCaptureExtremesClicked() {
    if (phase_ == Phase::Extremes) {
        // Stop recording, advance to center capture.
        if (seenMin_ < seenMax_) {
            result_.raw_min = seenMin_;
            result_.raw_max = seenMax_;
        }
        setPhase(Phase::Center);
        // Reset center accumulators in case user re-enters.
        centerSum_ = 0;
        centerCnt_ = 0;
    } else {
        seenMin_ = 4095;
        seenMax_ = 0;
        minLabel_->setText("—");
        maxLabel_->setText("—");
        setPhase(Phase::Extremes);
    }
}

void AxisCalibrator::onCaptureCenterClicked() {
    if (centerCnt_ < 5) {
        // Need at least a few samples; stay in Center phase.
        return;
    }
    result_.raw_center = (uint16_t)(centerSum_ / centerCnt_);
    centerLabel_->setText(QString::number(result_.raw_center));
    setPhase(Phase::Done);
}

void AxisCalibrator::onAccept() {
    result_.deadzone = (uint16_t)deadzoneSpin_->value();
    accept();
}
