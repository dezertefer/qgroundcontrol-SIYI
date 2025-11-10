#pragma once

#include "FactGroup.h"

class VehicleActuatorOutputsFactGroup : public FactGroup
{
    Q_OBJECT
public:
    explicit VehicleActuatorOutputsFactGroup(QObject* parent = nullptr);

    /// ch is 1..16 (silently ignored if out of range)
    Q_INVOKABLE void setPwm(int ch, int value);

    /// Optional: get a channel Fact (1..16). Returns nullptr if out of range.
    Q_INVOKABLE Fact* pwmFact(int ch) const {
        return (ch >= 1 && ch <= kMaxChannels) ? _pwmFacts[ch - 1] : nullptr;
    }

    static constexpr int kMaxChannels = 16;

private:
    void _registerFacts();

    // one Fact per PWM channel
    Fact* _pwmFacts[kMaxChannels] = { nullptr };

    // exported names
    static const char* _pwmNames[kMaxChannels];
};
