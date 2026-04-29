#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <optional>

extern "C" {
#include "hid_protocol.h"
}

struct hid_device_;

class DeviceLink : public QObject {
    Q_OBJECT
public:
    explicit DeviceLink(QObject *parent = nullptr);
    ~DeviceLink() override;

    // Open the first matching VID/PID device. Returns true on success and
    // emits connectedChanged.
    bool open();
    void close();
    bool isOpen() const { return dev_ != nullptr; }

    // Blocking control transfers. All return std::nullopt on transport error.
    std::optional<hid_status_t>      readStatus();
    std::optional<hid_raw_input_t>   readRawInput();
    std::optional<QByteArray>        readConfigBlock(uint8_t index);
    bool                             writeConfigBlock(uint8_t index, const QByteArray &payload);
    bool                             sendCommand(uint8_t command, uint32_t arg = 0);

    // Whole-config helpers built on the chunked protocol.
    std::optional<QByteArray>        readConfig();             // returns sizeof(joy_config_t) bytes
    bool                             writeConfig(const QByteArray &cfg);

    QString lastError() const { return lastError_; }

signals:
    void connectedChanged(bool connected);

private:
    hid_device_ *dev_ = nullptr;
    QString lastError_;

    void setError(const QString &msg);
};
