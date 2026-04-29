#pragma once
#include <QMainWindow>
#include <QPointer>
#include <array>

class QComboBox;
class QCheckBox;
class QSpinBox;
class QLabel;
class QProgressBar;
class QTabWidget;
class QPushButton;
class QTimer;

class DeviceLink;
class ConfigModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onConnectClicked();
    void onReadClicked();
    void onApplyClicked();           // write RAM only
    void onApplyAndSaveClicked();    // write + commit to flash
    void onFactoryResetClicked();
    void onRebootBootselClicked();
    void onPollLive();
    void onConnectedChanged(bool connected);
    void onCalibrateAxis(int axisIndex);

private:
    void buildUi();
    void buildAxesTab();
    void buildButtonsTab();
    void buildHatTab();
    void buildMuxTab();
    void buildSrTab();
    void buildGearboxTab();

    // ConfigModel ↔ widgets.
    void modelToUi();
    void uiToModel();

    void setStatus(const QString &msg);
    void readStatusInto(QString &line);

    DeviceLink  *link_  = nullptr;
    ConfigModel *model_ = nullptr;
    QTimer      *pollTimer_ = nullptr;

    QTabWidget *tabs_ = nullptr;
    QPushButton *btnConnect_ = nullptr;
    QPushButton *btnRead_ = nullptr;
    QPushButton *btnApply_ = nullptr;
    QPushButton *btnSave_ = nullptr;
    QPushButton *btnReset_ = nullptr;
    QPushButton *btnBootsel_ = nullptr;
    QLabel *statusLine_ = nullptr;

    // Per-axis widgets.
    struct AxisRow {
        QComboBox    *source = nullptr;
        QCheckBox    *enable = nullptr;
        QCheckBox    *invert = nullptr;
        QSpinBox     *rawMin = nullptr;
        QSpinBox     *rawCenter = nullptr;
        QSpinBox     *rawMax = nullptr;
        QSpinBox     *deadzone = nullptr;
        QProgressBar *liveRaw = nullptr;
        QLabel       *liveScaled = nullptr;
        QPushButton  *calibrate = nullptr;
    };
    std::array<AxisRow, 6> axisRows_{};

    // Per-button widgets.
    struct ButtonRow {
        QComboBox *gpio = nullptr;
        QCheckBox *enable = nullptr;
        QCheckBox *activeLow = nullptr;
        QLabel    *live = nullptr;
    };
    std::array<ButtonRow, 16> buttonRows_{};

    // Mux widgets (single 4051-class mux).
    struct MuxBlock {
        QComboBox *adc = nullptr;
        QComboBox *s0 = nullptr;
        QComboBox *s1 = nullptr;
        QComboBox *s2 = nullptr;
        QCheckBox *enable = nullptr;
    } mux_;

    // Shift register widgets (chained 74HC165).
    struct SrBlock {
        QComboBox *data  = nullptr;
        QComboBox *clock = nullptr;
        QComboBox *latch = nullptr;
        QSpinBox  *bits  = nullptr;
        QCheckBox *enable = nullptr;
    } sr_;

    // H-pattern / sequential shifter widgets.
    struct GearboxBlock {
        QCheckBox *enable    = nullptr;
        QComboBox *mode      = nullptr;   // Hold / Pulse / Sequential
        QSpinBox  *first     = nullptr;
        QSpinBox  *count     = nullptr;
        QSpinBox  *pulseMs   = nullptr;
        QComboBox *upButton  = nullptr;
        QComboBox *downButton= nullptr;
    } gear_;

    // HAT widgets.
    struct HatBlock {
        QComboBox *up = nullptr;
        QComboBox *right = nullptr;
        QComboBox *down = nullptr;
        QComboBox *left = nullptr;
        QCheckBox *enable = nullptr;
        QLabel    *live = nullptr;
    } hat_;
};
