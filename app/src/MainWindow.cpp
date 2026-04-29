#include "MainWindow.h"
#include "DeviceLink.h"
#include "ConfigModel.h"
#include "AxisCalibrator.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QStringList axisSourceOptions() {
    return {"None",
            "ADC0 (GPIO26)", "ADC1 (GPIO27)", "ADC2 (GPIO28)", "ADC3 (GPIO29)",
            "Mux ch0", "Mux ch1", "Mux ch2", "Mux ch3",
            "Mux ch4", "Mux ch5", "Mux ch6", "Mux ch7"};
}

QComboBox *makeAdcCombo() {
    auto *cb = new QComboBox;
    cb->addItem("None",  int(AXIS_SOURCE_NONE));
    cb->addItem("ADC0 (GPIO26)", int(AXIS_SOURCE_ADC0));
    cb->addItem("ADC1 (GPIO27)", int(AXIS_SOURCE_ADC1));
    cb->addItem("ADC2 (GPIO28)", int(AXIS_SOURCE_ADC2));
    cb->addItem("ADC3 (GPIO29)", int(AXIS_SOURCE_ADC3));
    return cb;
}

QComboBox *makeGpioCombo() {
    auto *cb = new QComboBox;
    cb->addItem("None", int(BUTTON_GPIO_NONE));
    // RP2040 user-available GPIO range. Skipping 29 to avoid VSYS conflicts on
    // Pico boards; user can still choose ADC3 manually for axes if desired.
    for (int i = 0; i <= 28; i++) {
        cb->addItem(QStringLiteral("GPIO%1").arg(i), i);
    }
    return cb;
}

// Encoded data values for the per-button source combobox.
// 0..28        → direct GPIO N
// 255          → unused
// 1000..1031   → SR bit (N - 1000)
constexpr int kSrBase = 1000;

QComboBox *makeButtonSourceCombo() {
    auto *cb = new QComboBox;
    cb->addItem("None", int(BUTTON_GPIO_NONE));
    for (int i = 0; i <= 28; i++) {
        cb->addItem(QStringLiteral("GPIO%1").arg(i), i);
    }
    for (int i = 0; i < SR_MAX_BITS; i++) {
        cb->addItem(QStringLiteral("SR bit %1").arg(i), kSrBase + i);
    }
    return cb;
}

// Decode a button_cfg_t into its combobox userData.
int buttonComboData(uint8_t gpio, uint8_t flags) {
    if (gpio == BUTTON_GPIO_NONE && !(flags & BUTTON_FLAG_SHIFT_REG)) return BUTTON_GPIO_NONE;
    if (flags & BUTTON_FLAG_SHIFT_REG) return kSrBase + gpio;
    return gpio;
}

// Encode the combobox userData back into (gpio, sr_flag_bit).
void decodeButtonCombo(int data, uint8_t *gpio_out, bool *use_sr_out) {
    if (data >= kSrBase) {
        *gpio_out = (uint8_t)(data - kSrBase);
        *use_sr_out = true;
    } else if (data == BUTTON_GPIO_NONE) {
        *gpio_out = BUTTON_GPIO_NONE;
        *use_sr_out = false;
    } else {
        *gpio_out = (uint8_t)data;
        *use_sr_out = false;
    }
}

void selectComboByData(QComboBox *cb, int data) {
    int idx = cb->findData(data);
    cb->setCurrentIndex(idx >= 0 ? idx : 0);
}

// Mirror of firmware's scale_axis() — must stay in lockstep so the live
// preview reflects what the device actually emits on the gamepad report.
int16_t scaleAxis(const axis_cfg_t &a, uint16_t raw) {
    int32_t center = a.raw_center;
    int32_t r      = raw;
    int32_t dz     = a.deadzone;
    int32_t out;

    if (r > center + dz) {
        int32_t span = (int32_t)a.raw_max - (center + dz);
        if (span <= 0) return 32767;
        int32_t v = r - (center + dz);
        if (v > span) v = span;
        out = (v * 32767) / span;
    } else if (r < center - dz) {
        int32_t span = (center - dz) - (int32_t)a.raw_min;
        if (span <= 0) return -32768;
        int32_t v = (center - dz) - r;
        if (v > span) v = span;
        out = -((v * 32768) / span);
    } else {
        out = 0;
    }
    if (a.flags & AXIS_FLAG_INVERT) out = -out;
    if (out > 32767)  out = 32767;
    if (out < -32768) out = -32768;
    return (int16_t)out;
}

QString hatName(uint8_t v) {
    switch (v) {
        case JOY_HAT_UP:          return "↑";
        case JOY_HAT_UP_RIGHT:    return "↗";
        case JOY_HAT_RIGHT:       return "→";
        case JOY_HAT_DOWN_RIGHT:  return "↘";
        case JOY_HAT_DOWN:        return "↓";
        case JOY_HAT_DOWN_LEFT:   return "↙";
        case JOY_HAT_LEFT:        return "←";
        case JOY_HAT_UP_LEFT:     return "↖";
        default:                  return "·";
    }
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    link_  = new DeviceLink(this);
    model_ = new ConfigModel(this);
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(50);   // 20 Hz live preview

    connect(link_, &DeviceLink::connectedChanged, this, &MainWindow::onConnectedChanged);
    connect(pollTimer_, &QTimer::timeout, this, &MainWindow::onPollLive);

    buildUi();
    modelToUi();
    setStatus("Ready. Click Connect.");
}

void MainWindow::buildUi() {
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *outer = new QVBoxLayout(central);

    auto *toolbar = new QHBoxLayout;
    btnConnect_ = new QPushButton("Connect");
    btnRead_    = new QPushButton("Read");
    btnApply_   = new QPushButton("Apply");
    btnSave_    = new QPushButton("Apply + Save");
    btnReset_   = new QPushButton("Factory Reset");
    btnBootsel_ = new QPushButton("Reboot → BOOTSEL");
    for (auto *b : {btnConnect_, btnRead_, btnApply_, btnSave_, btnReset_, btnBootsel_}) {
        toolbar->addWidget(b);
    }
    toolbar->addStretch();
    outer->addLayout(toolbar);

    connect(btnConnect_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(btnRead_,    &QPushButton::clicked, this, &MainWindow::onReadClicked);
    connect(btnApply_,   &QPushButton::clicked, this, &MainWindow::onApplyClicked);
    connect(btnSave_,    &QPushButton::clicked, this, &MainWindow::onApplyAndSaveClicked);
    connect(btnReset_,   &QPushButton::clicked, this, &MainWindow::onFactoryResetClicked);
    connect(btnBootsel_, &QPushButton::clicked, this, &MainWindow::onRebootBootselClicked);

    tabs_ = new QTabWidget;
    outer->addWidget(tabs_, 1);

    buildAxesTab();
    buildButtonsTab();
    buildHatTab();
    buildMuxTab();
    buildSrTab();
    buildGearboxTab();

    statusLine_ = new QLabel("—");
    statusBar()->addWidget(statusLine_, 1);

    resize(1000, 640);
    setWindowTitle("rp2040joy configurator");

    // Disable action buttons until connected.
    for (auto *b : {btnRead_, btnApply_, btnSave_, btnReset_, btnBootsel_}) b->setEnabled(false);
}

void MainWindow::buildAxesTab() {
    auto *page = new QWidget;
    auto *grid = new QGridLayout(page);

    int col = 0;
    for (auto *t : {"#", "Source", "On", "Inv", "raw_min", "raw_center", "raw_max",
                     "deadzone", "live raw", "scaled", ""}) {
        auto *l = new QLabel(QStringLiteral("<b>%1</b>").arg(t));
        grid->addWidget(l, 0, col++);
    }

    for (int i = 0; i < (int)axisRows_.size(); i++) {
        AxisRow &r = axisRows_[i];
        int row = i + 1;
        col = 0;
        grid->addWidget(new QLabel(QString::number(i)), row, col++);

        r.source = new QComboBox;
        r.source->addItems(axisSourceOptions());
        grid->addWidget(r.source, row, col++);

        r.enable = new QCheckBox;
        grid->addWidget(r.enable, row, col++);

        r.invert = new QCheckBox;
        grid->addWidget(r.invert, row, col++);

        for (QSpinBox **sb : {&r.rawMin, &r.rawCenter, &r.rawMax, &r.deadzone}) {
            *sb = new QSpinBox;
            (*sb)->setRange(0, 4095);
            grid->addWidget(*sb, row, col++);
        }

        r.liveRaw = new QProgressBar;
        r.liveRaw->setRange(0, 4095);
        r.liveRaw->setTextVisible(true);
        grid->addWidget(r.liveRaw, row, col++);

        r.liveScaled = new QLabel("0");
        r.liveScaled->setMinimumWidth(60);
        grid->addWidget(r.liveScaled, row, col++);

        r.calibrate = new QPushButton("Cal…");
        connect(r.calibrate, &QPushButton::clicked, this, [this, i]{ onCalibrateAxis(i); });
        grid->addWidget(r.calibrate, row, col++);
    }
    grid->setRowStretch(axisRows_.size() + 1, 1);
    tabs_->addTab(page, "Axes");
}

void MainWindow::onCalibrateAxis(int axisIndex) {
    if (!link_->isOpen()) return;
    // Snapshot current axis settings into a working copy. Source/flags carry
    // through; only min/center/max/deadzone are mutated by the wizard.
    uiToModel();
    axis_cfg_t snapshot = model_->raw()->axes[axisIndex];

    AxisCalibrator dlg(link_, axisIndex, snapshot, this);
    if (dlg.exec() != QDialog::Accepted) return;

    axis_cfg_t r = dlg.resultAxis();
    AxisRow &row = axisRows_[axisIndex];
    row.rawMin->setValue(r.raw_min);
    row.rawCenter->setValue(r.raw_center);
    row.rawMax->setValue(r.raw_max);
    row.deadzone->setValue(r.deadzone);
    setStatus(QStringLiteral("Calibrated axis %1: min=%2 center=%3 max=%4 dz=%5 (Apply to push)")
              .arg(axisIndex).arg(r.raw_min).arg(r.raw_center).arg(r.raw_max).arg(r.deadzone));
}

void MainWindow::buildButtonsTab() {
    auto *page = new QWidget;
    auto *grid = new QGridLayout(page);

    int col = 0;
    for (auto *t : {"#", "GPIO", "On", "Active low", "live"}) {
        grid->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(t)), 0, col++);
    }

    for (int i = 0; i < (int)buttonRows_.size(); i++) {
        ButtonRow &r = buttonRows_[i];
        int row = i + 1;
        col = 0;
        grid->addWidget(new QLabel(QString::number(i)), row, col++);
        r.gpio = makeButtonSourceCombo();
        grid->addWidget(r.gpio, row, col++);
        r.enable = new QCheckBox;
        grid->addWidget(r.enable, row, col++);
        r.activeLow = new QCheckBox;
        grid->addWidget(r.activeLow, row, col++);
        r.live = new QLabel("·");
        r.live->setMinimumWidth(20);
        grid->addWidget(r.live, row, col++);
    }
    grid->setRowStretch(buttonRows_.size() + 1, 1);
    tabs_->addTab(page, "Buttons");
}

void MainWindow::buildHatTab() {
    auto *page = new QWidget;
    auto *grid = new QGridLayout(page);

    grid->addWidget(new QLabel("<b>Direction</b>"), 0, 0);
    grid->addWidget(new QLabel("<b>GPIO</b>"), 0, 1);

    int row = 1;
    auto addRow = [&](const QString &name, QComboBox **target) {
        grid->addWidget(new QLabel(name), row, 0);
        *target = makeGpioCombo();
        grid->addWidget(*target, row, 1);
        row++;
    };
    addRow("Up",    &hat_.up);
    addRow("Right", &hat_.right);
    addRow("Down",  &hat_.down);
    addRow("Left",  &hat_.left);

    grid->addWidget(new QLabel("Enabled"), row, 0);
    hat_.enable = new QCheckBox;
    grid->addWidget(hat_.enable, row, 1);
    row++;

    grid->addWidget(new QLabel("Live"), row, 0);
    hat_.live = new QLabel("·");
    QFont big = hat_.live->font();
    big.setPointSize(big.pointSize() + 8);
    hat_.live->setFont(big);
    grid->addWidget(hat_.live, row, 1);

    grid->setRowStretch(row + 1, 1);
    tabs_->addTab(page, "HAT");
}

void MainWindow::buildMuxTab() {
    auto *page = new QWidget;
    auto *grid = new QGridLayout(page);

    auto *info = new QLabel(
        "Single 8-channel analog multiplexer (4051-class). The mux output is "
        "wired to one ADC pin; three GPIOs select the channel. Axes whose "
        "source is set to <i>Mux chN</i> read through this mux.");
    info->setWordWrap(true);
    grid->addWidget(info, 0, 0, 1, 2);

    int row = 1;
    grid->addWidget(new QLabel("Enabled"), row, 0);
    mux_.enable = new QCheckBox;
    grid->addWidget(mux_.enable, row++, 1);

    grid->addWidget(new QLabel("ADC pin (mux output)"), row, 0);
    mux_.adc = makeAdcCombo();
    grid->addWidget(mux_.adc, row++, 1);

    grid->addWidget(new QLabel("Select S0 (LSB)"), row, 0);
    mux_.s0 = makeGpioCombo();
    grid->addWidget(mux_.s0, row++, 1);

    grid->addWidget(new QLabel("Select S1"), row, 0);
    mux_.s1 = makeGpioCombo();
    grid->addWidget(mux_.s1, row++, 1);

    grid->addWidget(new QLabel("Select S2 (MSB)"), row, 0);
    mux_.s2 = makeGpioCombo();
    grid->addWidget(mux_.s2, row++, 1);

    grid->setRowStretch(row, 1);
    tabs_->addTab(page, "Mux");
}

void MainWindow::buildSrTab() {
    auto *page = new QWidget;
    auto *grid = new QGridLayout(page);

    auto *info = new QLabel(
        "Chained 74HC165 parallel-in/serial-out shift registers — 8 inputs per "
        "chip, up to 4 chips (32 bits) on three GPIOs total. Buttons whose "
        "source is set to <i>SR bit N</i> read from this chain.<br>"
        "Wiring: PL/SH → latch GPIO, CP → clock GPIO, QH (last chip) → data GPIO. "
        "Tie CE to GND, DS of first chip to GND, button switches between Vcc "
        "and the parallel inputs (or use pull-ups + active-low).");
    info->setWordWrap(true);
    grid->addWidget(info, 0, 0, 1, 2);

    int row = 1;
    grid->addWidget(new QLabel("Enabled"), row, 0);
    sr_.enable = new QCheckBox;
    grid->addWidget(sr_.enable, row++, 1);

    grid->addWidget(new QLabel("Data (QH → MCU)"), row, 0);
    sr_.data = makeGpioCombo();
    grid->addWidget(sr_.data, row++, 1);

    grid->addWidget(new QLabel("Clock (CP)"), row, 0);
    sr_.clock = makeGpioCombo();
    grid->addWidget(sr_.clock, row++, 1);

    grid->addWidget(new QLabel("Latch (PL/SH)"), row, 0);
    sr_.latch = makeGpioCombo();
    grid->addWidget(sr_.latch, row++, 1);

    grid->addWidget(new QLabel("Bit count"), row, 0);
    sr_.bits = new QSpinBox;
    sr_.bits->setRange(0, SR_MAX_BITS);
    sr_.bits->setSingleStep(8);
    sr_.bits->setSuffix(" bits");
    grid->addWidget(sr_.bits, row++, 1);

    grid->setRowStretch(row, 1);
    tabs_->addTab(page, "Shift reg");
}

void MainWindow::buildGearboxTab() {
    auto *page = new QWidget;
    auto *grid = new QGridLayout(page);

    auto *info = new QLabel(
        "H-pattern shifter post-processor. Treats a contiguous range of "
        "buttons as gear switches.<br>"
        "<b>Hold</b> — natural H-pattern (only the engaged gear is held; "
        "glitches suppressed).<br>"
        "<b>Pulse</b> — each engagement emits a brief pulse on the gear "
        "button instead of holding it.<br>"
        "<b>Sequential</b> — gear switches collapse into two virtual buttons "
        "\"shift up\" / \"shift down\" — pulsed on each gear change.");
    info->setWordWrap(true);
    grid->addWidget(info, 0, 0, 1, 2);

    int row = 1;
    grid->addWidget(new QLabel("Enabled"), row, 0);
    gear_.enable = new QCheckBox;
    grid->addWidget(gear_.enable, row++, 1);

    grid->addWidget(new QLabel("Mode"), row, 0);
    gear_.mode = new QComboBox;
    gear_.mode->addItem("Hold (mutex)",  0);
    gear_.mode->addItem("Pulse on engage", int(GEARBOX_FLAG_PULSE));
    gear_.mode->addItem("Sequential (up/down)", int(GEARBOX_FLAG_SEQUENTIAL));
    grid->addWidget(gear_.mode, row++, 1);

    grid->addWidget(new QLabel("First gear button index"), row, 0);
    gear_.first = new QSpinBox;
    gear_.first->setRange(0, JOY_BUTTON_COUNT - 1);
    grid->addWidget(gear_.first, row++, 1);

    grid->addWidget(new QLabel("Gear count"), row, 0);
    gear_.count = new QSpinBox;
    gear_.count->setRange(0, GEARBOX_MAX_GEARS);
    gear_.count->setSpecialValueText("disabled");
    grid->addWidget(gear_.count, row++, 1);

    grid->addWidget(new QLabel("Pulse length (ms)"), row, 0);
    gear_.pulseMs = new QSpinBox;
    gear_.pulseMs->setRange(10, 250);
    gear_.pulseMs->setSuffix(" ms");
    grid->addWidget(gear_.pulseMs, row++, 1);

    auto buttonCombo = []{
        auto *cb = new QComboBox;
        cb->addItem("Disabled", int(GEARBOX_BUTTON_NONE));
        for (int i = 0; i < JOY_BUTTON_COUNT; i++) cb->addItem(QStringLiteral("Button %1").arg(i), i);
        return cb;
    };
    grid->addWidget(new QLabel("Sequential — Shift Up button"), row, 0);
    gear_.upButton = buttonCombo();
    grid->addWidget(gear_.upButton, row++, 1);

    grid->addWidget(new QLabel("Sequential — Shift Down button"), row, 0);
    gear_.downButton = buttonCombo();
    grid->addWidget(gear_.downButton, row++, 1);

    grid->setRowStretch(row, 1);
    tabs_->addTab(page, "Gearbox");
}

// --- Model ↔ UI -----------------------------------------------------------

void MainWindow::modelToUi() {
    const joy_config_t *c = model_->raw();
    for (int i = 0; i < (int)axisRows_.size(); i++) {
        const axis_cfg_t &a = c->axes[i];
        AxisRow &r = axisRows_[i];
        r.source->setCurrentIndex(a.source);
        r.enable->setChecked(a.flags & AXIS_FLAG_ENABLE);
        r.invert->setChecked(a.flags & AXIS_FLAG_INVERT);
        r.rawMin->setValue(a.raw_min);
        r.rawCenter->setValue(a.raw_center);
        r.rawMax->setValue(a.raw_max);
        r.deadzone->setValue(a.deadzone);
    }
    for (int i = 0; i < (int)buttonRows_.size(); i++) {
        const button_cfg_t &b = c->buttons[i];
        ButtonRow &r = buttonRows_[i];
        selectComboByData(r.gpio, buttonComboData(b.gpio, b.flags));
        r.enable->setChecked(b.flags & BUTTON_FLAG_ENABLE);
        r.activeLow->setChecked(b.flags & BUTTON_FLAG_ACTIVE_LOW);
    }
    selectComboByData(hat_.up,    c->hat.gpio_up);
    selectComboByData(hat_.right, c->hat.gpio_right);
    selectComboByData(hat_.down,  c->hat.gpio_down);
    selectComboByData(hat_.left,  c->hat.gpio_left);
    hat_.enable->setChecked(c->hat.flags & HAT_FLAG_ENABLE);

    selectComboByData(mux_.adc, c->mux.adc_source);
    selectComboByData(mux_.s0,  c->mux.select_gpio[0]);
    selectComboByData(mux_.s1,  c->mux.select_gpio[1]);
    selectComboByData(mux_.s2,  c->mux.select_gpio[2]);
    mux_.enable->setChecked(c->mux.flags & MUX_FLAG_ENABLE);

    selectComboByData(sr_.data,  c->sr.pin_data);
    selectComboByData(sr_.clock, c->sr.pin_clock);
    selectComboByData(sr_.latch, c->sr.pin_latch);
    sr_.bits->setValue(c->sr.bit_count);
    sr_.enable->setChecked(c->sr.flags & SR_FLAG_ENABLE);

    gear_.enable->setChecked(c->gearbox.flags & GEARBOX_FLAG_ENABLE);
    int modeData = 0;
    if (c->gearbox.flags & GEARBOX_FLAG_SEQUENTIAL) modeData = GEARBOX_FLAG_SEQUENTIAL;
    else if (c->gearbox.flags & GEARBOX_FLAG_PULSE) modeData = GEARBOX_FLAG_PULSE;
    selectComboByData(gear_.mode, modeData);
    gear_.first->setValue(c->gearbox.first);
    gear_.count->setValue(c->gearbox.count);
    gear_.pulseMs->setValue(c->gearbox.pulse_ms ? c->gearbox.pulse_ms : 50);
    selectComboByData(gear_.upButton,   c->gearbox.up_button);
    selectComboByData(gear_.downButton, c->gearbox.down_button);
}

void MainWindow::uiToModel() {
    joy_config_t *c = model_->raw();
    for (int i = 0; i < (int)axisRows_.size(); i++) {
        AxisRow &r = axisRows_[i];
        axis_cfg_t &a = c->axes[i];
        a.source     = (uint8_t)r.source->currentIndex();
        a.flags      = (r.enable->isChecked() ? AXIS_FLAG_ENABLE : 0)
                     | (r.invert->isChecked() ? AXIS_FLAG_INVERT : 0);
        a.raw_min    = (uint16_t)r.rawMin->value();
        a.raw_center = (uint16_t)r.rawCenter->value();
        a.raw_max    = (uint16_t)r.rawMax->value();
        a.deadzone   = (uint16_t)r.deadzone->value();
    }
    for (int i = 0; i < (int)buttonRows_.size(); i++) {
        ButtonRow &r = buttonRows_[i];
        button_cfg_t &b = c->buttons[i];
        uint8_t gpio; bool useSr;
        decodeButtonCombo(r.gpio->currentData().toInt(), &gpio, &useSr);
        b.gpio  = gpio;
        b.flags = (r.enable->isChecked()    ? BUTTON_FLAG_ENABLE     : 0)
                | (r.activeLow->isChecked() ? BUTTON_FLAG_ACTIVE_LOW : 0)
                | (useSr                    ? BUTTON_FLAG_SHIFT_REG  : 0);
    }
    c->hat.gpio_up    = (uint8_t)hat_.up->currentData().toInt();
    c->hat.gpio_right = (uint8_t)hat_.right->currentData().toInt();
    c->hat.gpio_down  = (uint8_t)hat_.down->currentData().toInt();
    c->hat.gpio_left  = (uint8_t)hat_.left->currentData().toInt();
    c->hat.flags      = hat_.enable->isChecked() ? HAT_FLAG_ENABLE : 0;

    c->mux.adc_source       = (uint8_t)mux_.adc->currentData().toInt();
    c->mux.select_gpio[0]   = (uint8_t)mux_.s0->currentData().toInt();
    c->mux.select_gpio[1]   = (uint8_t)mux_.s1->currentData().toInt();
    c->mux.select_gpio[2]   = (uint8_t)mux_.s2->currentData().toInt();
    c->mux.flags            = mux_.enable->isChecked() ? MUX_FLAG_ENABLE : 0;

    c->sr.pin_data  = (uint8_t)sr_.data->currentData().toInt();
    c->sr.pin_clock = (uint8_t)sr_.clock->currentData().toInt();
    c->sr.pin_latch = (uint8_t)sr_.latch->currentData().toInt();
    c->sr.bit_count = (uint8_t)sr_.bits->value();
    c->sr.flags     = sr_.enable->isChecked() ? SR_FLAG_ENABLE : 0;

    c->gearbox.first       = (uint8_t)gear_.first->value();
    c->gearbox.count       = (uint8_t)gear_.count->value();
    c->gearbox.pulse_ms    = (uint8_t)gear_.pulseMs->value();
    c->gearbox.flags       = (gear_.enable->isChecked() ? GEARBOX_FLAG_ENABLE : 0)
                           | (uint8_t)gear_.mode->currentData().toInt();
    c->gearbox.up_button   = (uint8_t)gear_.upButton->currentData().toInt();
    c->gearbox.down_button = (uint8_t)gear_.downButton->currentData().toInt();
}

// --- Slots ----------------------------------------------------------------

void MainWindow::setStatus(const QString &msg) {
    statusLine_->setText(msg);
}

void MainWindow::readStatusInto(QString &line) {
    auto st = link_->readStatus();
    if (!st) {
        line = "Connected, but STATUS read failed.";
        return;
    }
    line = QStringLiteral("Connected. proto=%1 cfg_v=%2 fw=0x%3 build=%4")
        .arg(st->protocol_version)
        .arg(st->config_version)
        .arg(st->fw_version, 4, 16, QLatin1Char('0'))
        .arg(QString::fromLatin1(reinterpret_cast<const char *>(st->build)));
}

void MainWindow::onConnectClicked() {
    if (link_->isOpen()) {
        link_->close();
        return;
    }
    if (!link_->open()) {
        QMessageBox::warning(this, "Connect", link_->lastError());
    }
}

void MainWindow::onConnectedChanged(bool connected) {
    btnConnect_->setText(connected ? "Disconnect" : "Connect");
    for (auto *b : {btnRead_, btnApply_, btnSave_, btnReset_, btnBootsel_}) b->setEnabled(connected);
    if (connected) {
        QString line; readStatusInto(line); setStatus(line);
        onReadClicked();
        pollTimer_->start();
    } else {
        pollTimer_->stop();
        setStatus("Disconnected.");
    }
}

void MainWindow::onReadClicked() {
    auto bytes = link_->readConfig();
    if (!bytes) {
        QMessageBox::warning(this, "Read", link_->lastError());
        return;
    }
    if (!model_->setFromBytes(*bytes)) {
        QMessageBox::warning(this, "Read", "Invalid config size returned by device.");
        return;
    }
    modelToUi();
    setStatus("Config loaded from device.");
}

void MainWindow::onApplyClicked() {
    uiToModel();
    if (!link_->writeConfig(model_->toBytes())) {
        QMessageBox::warning(this, "Apply", link_->lastError());
        return;
    }
    link_->sendCommand(CMD_APPLY);
    setStatus("Applied to RAM (not yet saved to flash).");
}

void MainWindow::onApplyAndSaveClicked() {
    uiToModel();
    if (!link_->writeConfig(model_->toBytes())) {
        QMessageBox::warning(this, "Save", link_->lastError());
        return;
    }
    if (!link_->sendCommand(CMD_SAVE_TO_FLASH)) {
        QMessageBox::warning(this, "Save", link_->lastError());
        return;
    }
    setStatus("Applied + saved to flash.");
}

void MainWindow::onFactoryResetClicked() {
    auto reply = QMessageBox::question(this, "Factory Reset",
        "Restore default config (in RAM) and save it to flash?");
    if (reply != QMessageBox::Yes) return;
    if (!link_->sendCommand(CMD_FACTORY_RESET)) {
        QMessageBox::warning(this, "Factory Reset", link_->lastError());
        return;
    }
    if (!link_->sendCommand(CMD_SAVE_TO_FLASH)) {
        QMessageBox::warning(this, "Factory Reset", link_->lastError());
        return;
    }
    QTimer::singleShot(200, this, &MainWindow::onReadClicked);
    setStatus("Factory reset done.");
}

void MainWindow::onRebootBootselClicked() {
    auto reply = QMessageBox::question(this, "Reboot to BOOTSEL",
        "Device will disconnect and appear as a USB mass-storage drive (RPI-RP2). Continue?");
    if (reply != QMessageBox::Yes) return;
    link_->sendCommand(CMD_REBOOT_BOOTSEL);
    // Device is gone; close locally too.
    link_->close();
    setStatus("Sent BOOTSEL command. Device disconnected.");
}

void MainWindow::onPollLive() {
    auto raw = link_->readRawInput();
    if (!raw) return;
    // Pull the live config from the UI so toggling Invert/etc. is reflected
    // immediately, without round-tripping through the device.
    uiToModel();
    const joy_config_t *c = model_->raw();
    for (int i = 0; i < (int)axisRows_.size(); i++) {
        axisRows_[i].liveRaw->setValue(raw->axis_raw[i]);
        int16_t scaled = scaleAxis(c->axes[i], raw->axis_raw[i]);
        axisRows_[i].liveScaled->setText(QString::number(scaled));
    }
    for (int i = 0; i < (int)buttonRows_.size(); i++) {
        bool on = (raw->buttons >> i) & 1u;
        buttonRows_[i].live->setText(on ? "●" : "·");
    }
    hat_.live->setText(hatName(raw->hat));
}
