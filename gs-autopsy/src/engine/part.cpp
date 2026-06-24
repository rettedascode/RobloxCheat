#include "engine/engine.h"

#include <cmath>

namespace sdk {

    static bool valid_vector3(const sdk::vector3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    sdk::vector3 part::position() const {

        return drive->read<sdk::vector3>(Address + offsets::Position);
    }

    sdk::matrix3 part::rotation() const {

        return drive->read<sdk::matrix3>(Address + offsets::CFrame);
    }

    sdk::vector3 part::size() const {

        return drive->read<sdk::vector3>(Address + offsets::PartSize);
    }

    sdk::vector3 part::movedir() const {

        return drive->read<sdk::vector3>(Address + offsets::CameraMode);
    }

    sdk::vector3 part::velocity() const {

        if (!Address) return {};
        uintptr_t primitive = drive->read<uintptr_t>(Address + offsets::Primitive);
        if (!primitive) return {};
        return drive->read<sdk::vector3>(primitive + offsets::PrimitiveAssemblyAngularVelocity);
    }

    bool part::anchored() const {

        return drive->read<bool>(Address + offsets::Anchored);
    }

    sdk::vector3 part::cframe() const {

        return drive->read<sdk::vector3>(Address + offsets::CFrame);
    }

    part part::primitive() const {

        uintptr_t primitiveAddress = drive->read<uintptr_t>(Address + offsets::Primitive);
        return part{ primitiveAddress };
    }

    sdk::vector3 part::partposition() const {

        auto item = this->primitive();
        if (item.Address)
        {
            const sdk::vector3 primitive_position = drive->read<sdk::vector3>(item.Address + offsets::Position);
            if (valid_vector3(primitive_position))
                return primitive_position;
        }

        const sdk::vector3 base_position = drive->read<sdk::vector3>(Address + offsets::Position);
        if (valid_vector3(base_position))
            return base_position;

        return {};
    }

    float part::transparency() const {

        return drive->read<float>(Address + offsets::Transparency);
    }

    void part::velocity(const sdk::vector3& Velocity) const {

        auto item = this->primitive();
        if (!item.Address) return;
        drive->write<sdk::vector3>(item.Address + offsets::PrimitiveAssemblyLinearVelocity, Velocity);
    }

    void part::partposition(const sdk::vector3& Position) const {

        auto item = this->primitive();
        if (!item.Address) return;
        drive->write<sdk::vector3>(item.Address + offsets::Position, Position);
    }

    void part::rotation(const sdk::matrix3& Rotation) const {

        drive->write<sdk::matrix3>(Address + offsets::CFrame, Rotation);
    }

    void part::mesh(const std::string& MeshID) const {

        drive->writestring(Address + offsets::MeshId, MeshID);
    }

    void part::transparency(float Transparency) const {

        drive->write<float>(Address + offsets::Transparency, Transparency);
    }

    void part::movedir(const sdk::vector3& MoveDir) const {

        drive->write<sdk::vector3>(Address + offsets::CameraMode, MoveDir);
    }
}
