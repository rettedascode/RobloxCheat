#include "engine/engine.h"
#include "global.h"

namespace sdk {

    void light::ambient(uintptr_t LightingAddress, sdk::vector3 tocolor)
    {
        drive->write<sdk::vector3>(LightingAddress + offsets::LightingAmbient, tocolor);
        drive->write<sdk::vector3>(LightingAddress + offsets::OutdoorAmbient, tocolor);
    }

    void light::fog(uintptr_t LightingAddress, float end, sdk::vector3 tocolor)
    {
        drive->write<float>(LightingAddress + offsets::FogEnd, end);
        drive->write<sdk::vector3>(LightingAddress + offsets::FogColor, tocolor);
    }

    void light::brightness(uintptr_t LightingAddress, float Value)
    {
        drive->write<float>(LightingAddress + offsets::LightingBrightness, Value);
    }

    void light::exposure(uintptr_t LightingAddress, float Value)
    {
        drive->write<float>(LightingAddress + offsets::ExposureCompensation, Value);
    }

    void light::fov(uintptr_t CameraAddress, float Degrees)
    {
        drive->write<float>(CameraAddress + offsets::FOV, Degrees * 0.0174533f);
    }
}
