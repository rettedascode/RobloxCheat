#include "engine/engine.h"

namespace sdk {

    float humanoid::health() const
    {
        if (!this->Address)
            return 0.0f;

        union Conv
        {
            std::uint64_t hex;
            float f;
        } EasyConversion;

        uintptr_t healthPtr = drive->read<uintptr_t>(this->Address + offsets::Health);
        uintptr_t healthRead = drive->read<uintptr_t>(healthPtr);
        EasyConversion.hex = healthPtr ^ healthRead;

        return EasyConversion.f;
    }

    float humanoid::maxhealth() const
    {
        if (!this->Address)
            return 0.0f;

        union Conv
        {
            std::uint64_t hex;
            float f;
        } EasyConversion;

        uintptr_t healthPtr = drive->read<uintptr_t>(this->Address + offsets::MaxHealth);
        uintptr_t healthRead = drive->read<uintptr_t>(healthPtr);
        EasyConversion.hex = healthPtr ^ healthRead;

        return EasyConversion.f;
    }

    void humanoid::kill() const
    {
        if (!this->Address)
            return;

        drive->write<uintptr_t>(this->Address + offsets::Health, 0);
    }

    int humanoid::rig() const
    {
        if (!this->Address)
            return 0;

        return drive->read<int>(this->Address + offsets::RigType);
    }
}
