#pragma once
#include <QObject>
#include <QByteArray>

extern "C" {
#include "config.h"
}

// Thin wrapper around joy_config_t. Holds a single struct, exposes accessors
// for the UI, and provides round-trip with raw bytes (as exchanged with
// firmware via CONFIG_BLOCK reports).
class ConfigModel : public QObject {
    Q_OBJECT
public:
    explicit ConfigModel(QObject *parent = nullptr);

    joy_config_t       *raw()       { return &cfg_; }
    const joy_config_t *raw() const { return &cfg_; }

    // Replace contents with `bytes` (must be exactly sizeof(joy_config_t)).
    bool setFromBytes(const QByteArray &bytes);

    // Serialize current contents (sizeof(joy_config_t) bytes).
    QByteArray toBytes() const;

    // Reset to firmware defaults (matches CMD_FACTORY_RESET semantics).
    void resetToDefaults();

signals:
    void changed();

private:
    joy_config_t cfg_{};
};
