#define NOMINMAX
#include <Windows.h>
#include <thread>
#include <cmath>
#include <cfloat>
#include <string>
#include <algorithm>
#include <utility>
#include <vector>

#include "feature/aim.h"
#include "feature/cache.h"
#include "engine/engine.h"
#include "global.h"
#include "feature/wallcheck.h"
#include "imgui/addons/imgui_addons.h"

namespace aim {
    std::string CurrentLockedName = "";
    sdk::vector3 AimPositionW = { 0, 0, 0 };
    sdk::vector2 AimPositionS = { 0, 0 };
    bool TargetFound = false;

    std::string PersistenceName = "";
    bool IsPersisting = false;

    static bool knocked(sdk::player& player)
    {
        if (player.character.Address == 0)
            return false;

        sdk::instance BodyEffects = player.character.child("BodyEffects");
        if (BodyEffects.Address == 0)
            return false;

        sdk::instance Ko = BodyEffects.child("K.O");
        if (Ko.Address == 0)
            return false;

        return drive->read<bool>(Ko.Address + offsets::Value);
    }

    static sdk::matrix3 lerpmatrix3(const sdk::matrix3& A, const sdk::matrix3& B, float T) {
        sdk::matrix3 Result;
        for (int I = 0; I < 9; ++I) Result.data[I] = A.data[I] + (B.data[I] - A.data[I]) * T;
        return Result;
    }

    static sdk::vector3 cross(const sdk::vector3& A, const sdk::vector3& B) {
        return { A.y * B.z - A.z * B.y, A.z * B.x - A.x * B.z, A.x * B.y - A.y * B.x };
    }

    static sdk::matrix3 lookat(const sdk::vector3& CamPos, const sdk::vector3& TargetPos) {
        sdk::vector3 Forward = (TargetPos - CamPos).normalize();
        sdk::vector3 Right = cross(Forward, { 0.f, 1.f, 0.f }).normalize();
        sdk::vector3 Up = cross(Right, Forward);

        sdk::matrix3 M;
        M.data[0] = Right.x;     M.data[1] = Up.x;    M.data[2] = -Forward.x;
        M.data[3] = Right.y;     M.data[4] = Up.y;    M.data[5] = -Forward.y;
        M.data[6] = Right.z;     M.data[7] = Up.z;    M.data[8] = -Forward.z;
        return M;
    }

    static float clampf(float Value, float Min, float Max)
    {
        return std::max(Min, std::min(Value, Max));
    }

    static bool hitchance()
    {
        const int Chance = std::max(0, std::min(global::aim::HitChance, 100));
        if (Chance >= 100)
            return true;
        if (Chance <= 0)
            return false;

        return (rand() % 100) < Chance;
    }

    static sdk::vector3 linearvelocity(const sdk::instance& PartInst)
    {
        if (!PartInst.Address)
            return {};

        sdk::part part(PartInst.Address);
        sdk::part primitive = part.primitive();
        if (!primitive.Address)
            return {};

        return drive->read<sdk::vector3>(primitive.Address + offsets::PrimitiveAssemblyLinearVelocity);
    }

    static sdk::vector3 prediction(const sdk::instance& PartInst, const sdk::vector3& Position, float Distance)
    {
        if (!global::aim::Prediction)
            return Position;

        const sdk::vector3 Velocity = linearvelocity(PartInst);
        if (std::isnan(Velocity.x) || std::isnan(Velocity.y) || std::isnan(Velocity.z))
            return Position;

        if (global::aim::AutoPrediction)
        {
            const float SpeedTerm = clampf(Velocity.magnitude() / 2200.f, 0.f, .045f);
            const float DistanceTerm = clampf(Distance / 9500.f, .018f, .185f);
            const float Factor = DistanceTerm + SpeedTerm;
            return Position + Velocity * Factor;
        }

        return {
            Position.x + Velocity.x * global::aim::PredictionX,
            Position.y + Velocity.y * global::aim::PredictionY,
            Position.z + Velocity.z * global::aim::PredictionZ
        };
    }

    static float dot(const sdk::vector3& A, const sdk::vector3& B)
    {
        return A.x * B.x + A.y * B.y + A.z * B.z;
    }

    static bool visible(const sdk::camera& Cam, const sdk::vector3& TargetPos, const sdk::vector2& ScreenPos)
    {
        (void)Cam;
        (void)TargetPos;

        HWND Window = FindWindowA(0, "Roblox");
        RECT ClientRect{};
        if (!Window || !GetClientRect(Window, &ClientRect))
            return false;

        if (!(ScreenPos.x >= 0.f && ScreenPos.y >= 0.f &&
            ScreenPos.x <= (float)ClientRect.right &&
            ScreenPos.y <= (float)ClientRect.bottom))
            return false;

        return wallcheck::can_see(Cam.position(), TargetPos);
    }

    static bool bounds(sdk::player& player, ImVec2& Min, ImVec2& Max)
    {
        if (!global::render.Address)
            return false;

        std::vector<sdk::instance> Parts;
        if (player.Head.Address) Parts.push_back(player.Head);
        if (player.Torso.Address) Parts.push_back(player.Torso);
        if (player.UpperTorso.Address) Parts.push_back(player.UpperTorso);
        if (player.LowerTorso.Address) Parts.push_back(player.LowerTorso);
        if (player.HumanoidRootPart.Address) Parts.push_back(player.HumanoidRootPart);
        if (player.LeftArm.Address) Parts.push_back(player.LeftArm);
        if (player.RightArm.Address) Parts.push_back(player.RightArm);
        if (player.LeftLeg.Address) Parts.push_back(player.LeftLeg);
        if (player.RightLeg.Address) Parts.push_back(player.RightLeg);

        if (Parts.empty())
            return false;

        float left = FLT_MAX;
        float top = FLT_MAX;
        float right = -FLT_MAX;
        float bottom = -FLT_MAX;
        int valid = 0;

        HWND Window = FindWindowA(0, "Roblox");
        RECT ClientRect{};
        if (!Window || !GetClientRect(Window, &ClientRect))
            return false;

        for (const auto& inst : Parts)
        {
            sdk::part part(inst.Address);
            sdk::vector3 pos = part.partposition();
            if (std::isnan(pos.x) || std::isnan(pos.y) || std::isnan(pos.z))
                continue;

            sdk::vector2 screen = global::render.screen(pos);
            if (screen.x <= -0.5f || screen.y <= -0.5f ||
                screen.x >= (float)ClientRect.right || screen.y >= (float)ClientRect.bottom)
                continue;

            left = std::min(left, screen.x);
            top = std::min(top, screen.y);
            right = std::max(right, screen.x);
            bottom = std::max(bottom, screen.y);
            ++valid;
        }

        if (valid < 2 || left >= right || top >= bottom)
            return false;

        const float width = std::max(18.f, right - left);
        const float height = std::max(34.f, bottom - top);
        const float cx = (left + right) * .5f;
        const float cy = (top + bottom) * .5f;

        Min = { cx - width * .55f, cy - height * .58f };
        Max = { cx + width * .55f, cy + height * .58f };
        return true;
    }

    void acquire() {
        TargetFound = false;
        if (!global::render.Address) return;

        HWND RobloxWindow = FindWindowA(0, "Roblox");
        if (!RobloxWindow)
            return;

        POINT CursorPos;
        if (!GetCursorPos(&CursorPos) || !ScreenToClient(RobloxWindow, &CursorPos))
            return;

        float ClosestDistance = 999999.f;
        std::string BestName = "";
        uintptr_t BestCharacterAddr = 0;

        sdk::model Dm(global::model.Address);
        sdk::instance WorkspaceInst = Dm.childclass("Workspace");
        sdk::instance CameraInst = WorkspaceInst.child("Camera");
        if (!CameraInst.Address) return;

        sdk::camera Cam(CameraInst.Address);
        auto CameraOrigin = Cam.position();
        auto PlayersSnapshot = cache::snapshot();

        if (global::aim::AimbotSticky && IsPersisting && !PersistenceName.empty()) {
            for (auto& Plr : PlayersSnapshot) {
                if (Plr.name != PersistenceName) continue;

                if (Plr.Local_Player ||
                    (global::LocalPlayer.character.Address != 0 && Plr.character.Address == global::LocalPlayer.character.Address) ||
                    !Plr.character.Address || !Plr.Head.Address)
                {
                    IsPersisting = false;
                    PersistenceName = "";
                    global::aim::AimTarget = sdk::instance(0);
                    return;
                }

                std::vector<std::pair<sdk::vector3, sdk::instance>> Bones;
                if (Plr.Head.Address) Bones.push_back({ sdk::part(Plr.Head.Address).partposition(), Plr.Head });
                if (Plr.Torso.Address) Bones.push_back({ sdk::part(Plr.Torso.Address).partposition(), Plr.Torso });
                if (Plr.LowerTorso.Address) Bones.push_back({ sdk::part(Plr.LowerTorso.Address).partposition(), Plr.LowerTorso });

                if (Bones.empty()) {
                    IsPersisting = false;
                    PersistenceName = "";
                    global::aim::AimTarget = sdk::instance(0);
                    return;
                }

                int HitboxIdx = global::aim::HitPart;
                if (HitboxIdx >= (int)Bones.size()) HitboxIdx = 0;
                if (HitboxIdx == 4) HitboxIdx = rand() % (int)Bones.size();

                sdk::vector3 BonePos = Bones[HitboxIdx].first;
                if (std::isnan(BonePos.x)) {
                    IsPersisting = false;
                    PersistenceName = "";
                    global::aim::AimTarget = sdk::instance(0);
                    return;
                }

                const float Dist3D = (CameraOrigin - BonePos).magnitude();
                BonePos = prediction(Bones[HitboxIdx].second, BonePos, Dist3D);

                sdk::render Ve(global::render.Address);
                auto ScreenPos = Ve.screen(BonePos);


                if (ScreenPos.x <= -0.5f || ScreenPos.y <= -0.5f ||
                    (global::aim::VisibleCheck && !visible(Cam, BonePos, ScreenPos))) {
                    IsPersisting = false;
                    PersistenceName = "";
                    global::aim::AimTarget = sdk::instance(0);
                    return;
                }

                float Dist2D = sqrtf(
                    (ScreenPos.x - CursorPos.x) * (ScreenPos.x - CursorPos.x) +
                    (ScreenPos.y - CursorPos.y) * (ScreenPos.y - CursorPos.y));

                if (global::aim::useFov && Dist2D > global::aim::FovSize) {
                    IsPersisting = false;
                    PersistenceName = "";
                    global::aim::AimTarget = sdk::instance(0);
                    return;
                }

                AimPositionW = BonePos;
                AimPositionS = { ScreenPos.x, ScreenPos.y };
                CurrentLockedName = Plr.name;
                global::aim::AimTarget = sdk::instance(Plr.character.Address);
                TargetFound = true;
                return;
            }

            IsPersisting = false;
            PersistenceName = "";
            global::aim::AimTarget = sdk::instance(0);
        }

        for (auto& Plr : PlayersSnapshot) {
            if (Plr.Local_Player ||
                (global::LocalPlayer.character.Address != 0 && Plr.character.Address == global::LocalPlayer.character.Address) ||
                !Plr.character.Address || !Plr.Head.Address)
                continue;

            if (global::aim::KnockedCheck && knocked(Plr))
                continue;

            std::vector<std::pair<sdk::vector3, sdk::instance>> Bones;
            if (Plr.Head.Address) Bones.push_back({ sdk::part(Plr.Head.Address).partposition(), Plr.Head });
            if (Plr.Torso.Address) Bones.push_back({ sdk::part(Plr.Torso.Address).partposition(), Plr.Torso });
            if (Plr.LowerTorso.Address) Bones.push_back({ sdk::part(Plr.LowerTorso.Address).partposition(), Plr.LowerTorso });
            else if (Plr.HumanoidRootPart.Address) Bones.push_back({ sdk::part(Plr.HumanoidRootPart.Address).partposition(), Plr.HumanoidRootPart });
            if (Bones.empty()) continue;

            int HitboxIdx = global::aim::HitPart;
            if (HitboxIdx >= (int)Bones.size()) HitboxIdx = 0;
            if (HitboxIdx == 4) HitboxIdx = rand() % (int)Bones.size();

            sdk::vector3 BonePos = Bones[HitboxIdx].first;
            if (std::isnan(BonePos.x)) continue;

            float Dist3D = (CameraOrigin - BonePos).magnitude();
            if (Dist3D > 700.f) continue;

            BonePos = prediction(Bones[HitboxIdx].second, BonePos, Dist3D);

            sdk::render Ve(global::render.Address);
            auto ScreenPos = Ve.screen(BonePos);

            if (ScreenPos.x <= -0.5f || ScreenPos.y <= -0.5f) continue;

            RECT ClientRect;
            GetClientRect(FindWindowA(0, "Roblox"), &ClientRect);
            if (ScreenPos.x > ClientRect.right || ScreenPos.y > ClientRect.bottom) continue;
            if (global::aim::VisibleCheck && !visible(Cam, BonePos, ScreenPos)) continue;

            float Dist2D = sqrtf(
                (ScreenPos.x - CursorPos.x) * (ScreenPos.x - CursorPos.x) +
                (ScreenPos.y - CursorPos.y) * (ScreenPos.y - CursorPos.y));

            if (global::aim::useFov && Dist2D > global::aim::FovSize) continue;

            const float Score = global::aim::TargetPriority == 1 ? Dist3D : Dist2D;
            if (Score < ClosestDistance) {
                ClosestDistance = Score;
                AimPositionW = BonePos;
                AimPositionS = { ScreenPos.x, ScreenPos.y };
                BestName = Plr.name;
                BestCharacterAddr = Plr.character.Address;
                TargetFound = true;
            }
        }

        if (TargetFound) {
            CurrentLockedName = BestName;
            global::aim::AimTarget = sdk::instance(BestCharacterAddr);
            if (global::aim::AimbotSticky && !IsPersisting) {
                PersistenceName = BestName;
                IsPersisting = true;
            }
        }
    }

    void triggerbot()
    {
        if (!global::aim::TriggerBot || !global::render.Address)
            return;

        static ULONGLONG LastShot = 0;
        POINT CursorPos;
        HWND Window = FindWindowA(0, "Roblox");
        if (!Window || GetForegroundWindow() != Window || !GetCursorPos(&CursorPos) || !ScreenToClient(Window, &CursorPos))
            return;

        sdk::model Dm(global::model.Address);
        sdk::instance WorkspaceInst = Dm.childclass("Workspace");
        sdk::instance CameraInst = WorkspaceInst.child("Camera");
        if (!CameraInst.Address)
            return;

        const sdk::vector2 Cursor{ (float)CursorPos.x, (float)CursorPos.y };
        const float Padding = std::max(0.f, global::aim::TriggerRadius);

        auto PlayersSnapshot = cache::snapshot();
        for (auto& Plr : PlayersSnapshot) {
            if (Plr.Local_Player ||
                (global::LocalPlayer.character.Address != 0 && Plr.character.Address == global::LocalPlayer.character.Address) ||
                !Plr.character.Address || !Plr.Head.Address)
                continue;

            if (global::aim::KnockedCheck && knocked(Plr))
                continue;

            ImVec2 Min{}, Max{};
            if (!bounds(Plr, Min, Max))
                continue;

            if (Cursor.x < Min.x - Padding || Cursor.x > Max.x + Padding ||
                Cursor.y < Min.y - Padding || Cursor.y > Max.y + Padding)
                continue;

            sdk::vector3 Pos = sdk::part(Plr.Head.Address).partposition();
            sdk::vector2 ScreenPos = global::render.screen(Pos);
            sdk::camera Cam(CameraInst.Address);
            if (global::aim::VisibleCheck && !visible(Cam, Pos, ScreenPos))
                continue;

            const ULONGLONG Now = GetTickCount64();
            if (Now - LastShot < (ULONGLONG)std::max(0, global::aim::TriggerDelayMs))
                return;

            INPUT Input[2]{};
            Input[0].type = INPUT_MOUSE;
            Input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            Input[1].type = INPUT_MOUSE;
            Input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(2, Input, sizeof(INPUT));
            LastShot = Now;
            return;
        }
    }

    void update() {
        if (!TargetFound) return;
        if (!hitchance()) return;

        if (global::aim::Aimbot_type == 1) {
            sdk::model Dm(global::model.Address);
            sdk::instance WorkspaceInst = Dm.childclass("Workspace");
            sdk::instance CameraInst = WorkspaceInst.child("Camera");
            if (!CameraInst.Address) return;

            sdk::camera Cam(CameraInst.Address);
            auto CamPos = Cam.position();
            auto TargetPos = AimPositionW;

            TargetPos.x += global::aim::ShakeX;
            TargetPos.y += global::aim::ShakeY;
            TargetPos.z += global::aim::ShakeY;

            if (global::aim::Shake) {
                TargetPos.x += ((float)rand() / RAND_MAX * 2 - 1) * global::aim::ShakeX;
                TargetPos.y += ((float)rand() / RAND_MAX * 2 - 1) * global::aim::ShakeY;
                TargetPos.z += ((float)rand() / RAND_MAX * 2 - 1) * global::aim::ShakeZ;
            }

            sdk::vector3 Delta = TargetPos - CamPos;
            if (Delta.magnitude() < 0.01f) return;

            float SmoothFactor = global::aim::camera::Smoothing_X / 100.f;
            SmoothFactor = std::pow(SmoothFactor, 1.2f);
            SmoothFactor = std::max(0.0f, std::min(SmoothFactor, 0.98f));

            sdk::matrix3 TargetMatrix = lookat(CamPos, TargetPos);
            sdk::matrix3 CurrentMatrix = Cam.rotation();
            sdk::matrix3 FinalMatrix = lerpmatrix3(CurrentMatrix, TargetMatrix, 1.f - SmoothFactor);

            Cam.rotation(FinalMatrix);
        }
        else {
            POINT CursorPos;
            HWND RobloxWindow = FindWindowA(0, "Roblox");
            if (!RobloxWindow || !GetCursorPos(&CursorPos) || !ScreenToClient(RobloxWindow, &CursorPos))
                return;

            float Sensitivity = global::aim::mouse::Mouse_Sensitivty;
            if (global::aim::mouse::Smoothing_X > 0) {
                float SmoothVal = global::aim::mouse::Smoothing_X;
                if (SmoothVal < 1.f) SmoothVal = 1.f;
                if (SmoothVal > 100.f) SmoothVal = 100.f;
                Sensitivity /= SmoothVal;
            }

            float MoveX = (AimPositionS.x - CursorPos.x) * Sensitivity;
            float MoveY = (AimPositionS.y - CursorPos.y) * Sensitivity;

            if (global::aim::Shake) {
                MoveX += ((float)rand() / RAND_MAX * 2 - 1) * global::aim::ShakeX;
                MoveY += ((float)rand() / RAND_MAX * 2 - 1) * global::aim::ShakeY;
            }

            if (MoveX < -100.f) MoveX = -100.f;
            if (MoveX > 100.f) MoveX = 100.f;
            if (MoveY < -100.f) MoveY = -100.f;
            if (MoveY > 100.f) MoveY = 100.f;

            if (abs(MoveX) >= 1.f || abs(MoveY) >= 1.f) {
                INPUT Input = {};
                Input.type = INPUT_MOUSE;
                Input.mi.dx = (LONG)MoveX;
                Input.mi.dy = (LONG)MoveY;
                Input.mi.dwFlags = MOUSEEVENTF_MOVE;
                SendInput(1, &Input, sizeof(INPUT));
            }
        }
    }

    void run() {
        std::thread([]() {

            bool Toggled = false;
            bool LastPressed = false;

            while (true) {
                bool WorkedThisTick = false;

                if (global::aim::Enabled) {

                    int Vk = ImGuiKeyToVK(global::aim::Aimbot_Key);
                    if (!Vk) { Sleep(12); continue; }

                    HWND RobloxHwnd = FindWindowA(0, "Roblox");
                    bool RobloxFocused = RobloxHwnd && GetForegroundWindow() == RobloxHwnd;
                    bool Pressed = RobloxFocused && (GetAsyncKeyState(Vk) & 0x8000) != 0;

                    if (global::aim::Aimbot_Mode == ImKeyBindMode_Toggle) {

                        if (Pressed && !LastPressed)
                            Toggled = !Toggled;

                        if (Toggled) {
                            acquire();
                            update();
                            WorkedThisTick = true;
                        }
                        else {
                            CurrentLockedName = "";
                            IsPersisting = false;
                            PersistenceName = "";
                            global::aim::AimTarget = sdk::instance(0);
                        }
                    }
                    else {

                        if (Pressed) {
                            acquire();
                            update();
                            WorkedThisTick = true;
                        }
                        else {
                            CurrentLockedName = "";
                            IsPersisting = false;
                            PersistenceName = "";
                            global::aim::AimTarget = sdk::instance(0);
                        }
                    }

                    LastPressed = Pressed;
                }

                triggerbot();
                Sleep((WorkedThisTick || global::aim::TriggerBot) ? 1 : 10);
            }

            }).detach();
    }

}
