#include "engine/engine.h"

#include <Windows.h>

namespace sdk {

    static sdk::vector2 viewport_size(uintptr_t visual_engine)
    {
        sdk::vector2 dimensions{};
        if (visual_engine)
            dimensions = drive->read<sdk::vector2>(visual_engine + offsets::Dimensions);

        if (dimensions.x > 1.f && dimensions.y > 1.f)
            return dimensions;

        HWND window = FindWindowA(nullptr, "Roblox");
        RECT rect{};
        if (window && GetClientRect(window, &rect))
        {
            dimensions.x = static_cast<float>(rect.right - rect.left);
            dimensions.y = static_cast<float>(rect.bottom - rect.top);
        }

        return dimensions;
    }

    uintptr_t render::resolve(uintptr_t model_address)
    {
        if (!drive || !drive->modulebase())
            return 0;

        const uintptr_t module = drive->modulebase();
        uintptr_t visual = drive->read<uintptr_t>(module + offsets::VisualEnginePointer);
        if (visual)
            return visual;

        if (!model_address)
            return 0;

        uintptr_t step = drive->read<uintptr_t>(model_address + offsets::DataModelToRenderView1);
        if (!step)
            return 0;

        step = drive->read<uintptr_t>(step + offsets::DataModelToRenderView2);
        if (!step)
            return 0;

        step = drive->read<uintptr_t>(step + offsets::DataModelToRenderView3);
        if (!step)
            return 0;

        return drive->read<uintptr_t>(step + offsets::VisualEngine);
    }

    sdk::vector2 sdk::render::size() const
    {
        return viewport_size(Address);
    }

    sdk::matrix4 sdk::render::matrix() const
    {
        return drive->read<sdk::matrix4>(this->Address + offsets::viewmatrix);
    }

    sdk::vector2 sdk::render::screen(const sdk::vector3& world) const
    {
        if (!Address)
            return { -1.f, -1.f };

        const sdk::matrix4 view_matrix = matrix();
        const sdk::vector2 dimensions = viewport_size(Address);
        if (dimensions.x <= 1.f || dimensions.y <= 1.f)
            return { -1.f, -1.f };

        sdk::vector4 clip{};
        clip.x = (world.x * view_matrix.data[0]) + (world.y * view_matrix.data[1]) + (world.z * view_matrix.data[2]) + view_matrix.data[3];
        clip.y = (world.x * view_matrix.data[4]) + (world.y * view_matrix.data[5]) + (world.z * view_matrix.data[6]) + view_matrix.data[7];
        clip.z = (world.x * view_matrix.data[8]) + (world.y * view_matrix.data[9]) + (world.z * view_matrix.data[10]) + view_matrix.data[11];
        clip.w = (world.x * view_matrix.data[12]) + (world.y * view_matrix.data[13]) + (world.z * view_matrix.data[14]) + view_matrix.data[15];

        if (clip.w < 0.1f)
            return { -1.f, -1.f };

        const float inv_w = 1.f / clip.w;
        const float ndc_x = clip.x * inv_w;
        const float ndc_y = clip.y * inv_w;

        return {
            (dimensions.x * 0.5f * ndc_x) + (dimensions.x * 0.5f),
            -(dimensions.y * 0.5f * ndc_y) + (dimensions.y * 0.5f)
        };
    }
}
