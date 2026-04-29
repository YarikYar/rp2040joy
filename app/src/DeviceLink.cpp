#include "DeviceLink.h"
#include <hidapi/hidapi.h>
#include <cstring>

namespace {
constexpr uint16_t kVid = 0xCafe;
constexpr uint16_t kPid = 0x4004;

constexpr int kStatusBufLen   = 1 + sizeof(hid_status_t);
constexpr int kRawInputBufLen = 1 + sizeof(hid_raw_input_t);
constexpr int kConfigBufLen   = 1 + sizeof(hid_config_block_t);
constexpr int kCommandBufLen  = 1 + sizeof(hid_command_t);
}

DeviceLink::DeviceLink(QObject *parent) : QObject(parent) {
    hid_init();
}

DeviceLink::~DeviceLink() {
    close();
    hid_exit();
}

void DeviceLink::setError(const QString &msg) {
    lastError_ = msg;
}

bool DeviceLink::open() {
    close();
    dev_ = hid_open(kVid, kPid, nullptr);
    if (!dev_) {
        setError(QStringLiteral("hid_open failed (device not found or permission denied)"));
        return false;
    }
    emit connectedChanged(true);
    return true;
}

void DeviceLink::close() {
    if (dev_) {
        hid_close(dev_);
        dev_ = nullptr;
        emit connectedChanged(false);
    }
}

std::optional<hid_status_t> DeviceLink::readStatus() {
    if (!dev_) return std::nullopt;
    unsigned char buf[kStatusBufLen] = {};
    buf[0] = HID_REPORT_ID_STATUS;
    int n = hid_get_feature_report(dev_, buf, sizeof(buf));
    if (n < (int)sizeof(buf)) {
        setError(QStringLiteral("hid_get_feature_report(STATUS) returned %1").arg(n));
        return std::nullopt;
    }
    hid_status_t st;
    std::memcpy(&st, buf + 1, sizeof(st));
    return st;
}

std::optional<hid_raw_input_t> DeviceLink::readRawInput() {
    if (!dev_) return std::nullopt;
    unsigned char buf[kRawInputBufLen] = {};
    buf[0] = HID_REPORT_ID_RAW_INPUT;
    int n = hid_get_feature_report(dev_, buf, sizeof(buf));
    if (n < (int)sizeof(buf)) {
        setError(QStringLiteral("hid_get_feature_report(RAW_INPUT) returned %1").arg(n));
        return std::nullopt;
    }
    hid_raw_input_t r;
    std::memcpy(&r, buf + 1, sizeof(r));
    return r;
}

bool DeviceLink::sendCommand(uint8_t command, uint32_t arg) {
    if (!dev_) return false;
    unsigned char buf[kCommandBufLen] = {};
    buf[0] = HID_REPORT_ID_COMMAND;
    hid_command_t c{};
    c.command = command;
    c.arg = arg;
    std::memcpy(buf + 1, &c, sizeof(c));
    int n = hid_send_feature_report(dev_, buf, sizeof(buf));
    if (n < 0) {
        setError(QStringLiteral("hid_send_feature_report(COMMAND) failed"));
        return false;
    }
    return true;
}

std::optional<QByteArray> DeviceLink::readConfigBlock(uint8_t index) {
    if (!dev_) return std::nullopt;
    if (!sendCommand(CMD_SELECT_BLOCK, index)) return std::nullopt;

    unsigned char buf[kConfigBufLen] = {};
    buf[0] = HID_REPORT_ID_CONFIG_BLOCK;
    int n = hid_get_feature_report(dev_, buf, sizeof(buf));
    if (n < (int)sizeof(buf)) {
        setError(QStringLiteral("hid_get_feature_report(CONFIG_BLOCK) returned %1").arg(n));
        return std::nullopt;
    }
    // buf[1] = block_index, buf[2] = block_total, buf[3..] = payload.
    return QByteArray(reinterpret_cast<const char *>(buf + 3), HID_CONFIG_BLOCK_PAYLOAD);
}

bool DeviceLink::writeConfigBlock(uint8_t index, const QByteArray &payload) {
    if (!dev_) return false;
    if (payload.size() != HID_CONFIG_BLOCK_PAYLOAD) {
        setError(QStringLiteral("writeConfigBlock: payload must be %1 bytes")
                  .arg(HID_CONFIG_BLOCK_PAYLOAD));
        return false;
    }
    unsigned char buf[kConfigBufLen] = {};
    buf[0] = HID_REPORT_ID_CONFIG_BLOCK;
    buf[1] = index;
    buf[2] = HID_CONFIG_BLOCK_TOTAL;
    std::memcpy(buf + 3, payload.constData(), HID_CONFIG_BLOCK_PAYLOAD);
    int n = hid_send_feature_report(dev_, buf, sizeof(buf));
    if (n < 0) {
        setError(QStringLiteral("hid_send_feature_report(CONFIG_BLOCK) failed"));
        return false;
    }
    return true;
}

std::optional<QByteArray> DeviceLink::readConfig() {
    QByteArray full;
    full.reserve(HID_CONFIG_BLOCK_TOTAL * HID_CONFIG_BLOCK_PAYLOAD);
    for (uint8_t i = 0; i < HID_CONFIG_BLOCK_TOTAL; i++) {
        auto block = readConfigBlock(i);
        if (!block) return std::nullopt;
        full.append(*block);
    }
    full.truncate(sizeof(joy_config_t));
    return full;
}

bool DeviceLink::writeConfig(const QByteArray &cfg) {
    if ((size_t)cfg.size() != sizeof(joy_config_t)) {
        setError(QStringLiteral("writeConfig: expected %1 bytes, got %2")
                  .arg(sizeof(joy_config_t)).arg(cfg.size()));
        return false;
    }
    QByteArray padded = cfg;
    padded.resize(HID_CONFIG_BLOCK_TOTAL * HID_CONFIG_BLOCK_PAYLOAD, '\0');
    for (uint8_t i = 0; i < HID_CONFIG_BLOCK_TOTAL; i++) {
        QByteArray chunk = padded.mid(i * HID_CONFIG_BLOCK_PAYLOAD, HID_CONFIG_BLOCK_PAYLOAD);
        if (!writeConfigBlock(i, chunk)) return false;
    }
    return true;
}
