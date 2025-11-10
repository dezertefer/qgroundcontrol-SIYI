#include "VehicleActuatorOutputsFactGroup.h"
#include "FactMetaData.h"

const char* VehicleActuatorOutputsFactGroup::_pwmNames[VehicleActuatorOutputsFactGroup::kMaxChannels] = {
    "pwm1","pwm2","pwm3","pwm4","pwm5","pwm6","pwm7","pwm8",
    "pwm9","pwm10","pwm11","pwm12","pwm13","pwm14","pwm15","pwm16"
};

VehicleActuatorOutputsFactGroup::VehicleActuatorOutputsFactGroup(QObject* parent)
    // 250 ms UI update rate; no JSON metadata file required, but you can point to one if you have it
    : FactGroup(250, /*jsonMetadataFile*/ QString() , parent)
{
    _registerFacts();
}

void VehicleActuatorOutputsFactGroup::_registerFacts()
{
    for (int i = 0; i < kMaxChannels; ++i) {
        // Correct Fact ctor: (id, name, type, parent)
        _pwmFacts[i] = new Fact(0, _pwmNames[i], FactMetaData::valueTypeUint16, this);
        _pwmFacts[i]->setRawValue(0);
        _addFact(_pwmFacts[i], _pwmNames[i]);
    }
}

void VehicleActuatorOutputsFactGroup::setPwm(int ch, int value)
{
    if (ch < 1 || ch > kMaxChannels) {
        return;
    }
    _pwmFacts[ch - 1]->setRawValue(value);
}
