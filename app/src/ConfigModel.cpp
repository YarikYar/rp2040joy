#include "ConfigModel.h"
#include <cstring>

ConfigModel::ConfigModel(QObject *parent) : QObject(parent) {
    config_set_defaults(&cfg_);
}

bool ConfigModel::setFromBytes(const QByteArray &bytes) {
    if ((size_t)bytes.size() != sizeof(joy_config_t)) return false;
    std::memcpy(&cfg_, bytes.constData(), sizeof(cfg_));
    emit changed();
    return true;
}

QByteArray ConfigModel::toBytes() const {
    return QByteArray(reinterpret_cast<const char *>(&cfg_), sizeof(cfg_));
}

void ConfigModel::resetToDefaults() {
    config_set_defaults(&cfg_);
    emit changed();
}
