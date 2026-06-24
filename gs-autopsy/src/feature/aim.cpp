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

    static float clampf(float Value, float Min, float Max)
    {
        return std::max(Min, std::min(Value, Max));
    }

    static bool knocked(const sdk::player& player);
    static sdk::vector3 prediction(const sdk::instance& PartInst, const sdk::vector3& Position, float Distance);
    static bool visible(const sdk::camera& Cam, const sdk::vector3& TargetPos, const sdk::vector2& ScreenPos);

    namespace {
        sdk::vector2 SmoothedScreen{};
        sdk::vector3 SmoothedWorld{};
        bool HasSmoothedAim = false;

        int LockedHitboxIdx = 0;
        uintptr_t LockedCharacter = 0;
        int StickyMissFrames = 0;
        int TriggerOnTargetFrames = 0;

        constexpr int StickyMissGrace = 4;
        constexpr int TriggerConfirmFrames = 3;
        constexpr float TargetSwitchBias = 0.82f;

        static float smoothstep(float value)
        {
            return clampf(value, 0.04f, 1.f);
        }

        static float mouseblend()
        {
            const float smooth = std::max(0.f, global::aim::mouse::Smoothing_X);
            if (smooth <= 0.f)
                return 0.65f;
            return smoothstep(1.f / (1.f + smooth * 0.85f));
        }

        static float camerablend()
        {
            const float smooth = std::max(0.f, global::aim::camera::Smoothing_X);
            if (smooth <= 0.f)
                return 0.55f;
            return smoothstep(1.f / (1.f + smooth * 0.9f));
        }

        static void resetaimstate()
        {
            HasSmoothedAim = false;
            LockedHitboxIdx = 0;
            LockedCharacter = 0;
            StickyMissFrames = 0;
            TriggerOnTargetFrames = 0;
            TargetFound = false;
            CurrentLockedName = "";
            IsPersisting = false;
            PersistenceName = "";
            global::aim::AimTarget = sdk::instance(0);
        }

        static sdk::instance resolvehitboxpart(const sdk::player& player, int hitPart)
        {
            if (hitPart == 0)
                return player.Head;

            if (hitPart == 1)
            {
                if (player.UpperTorso.Address)
                    return player.UpperTorso;
                return player.Torso;
            }

            if (hitPart == 2)
            {
                if (player.LowerTorso.Address)
                    return player.LowerTorso;
                if (player.HumanoidRootPart.Address)
                    return player.HumanoidRootPart;
                return player.Torso;
            }

            if (player.Head.Address)
                return player.Head;
            if (player.UpperTorso.Address)
                return player.UpperTorso;
            if (player.Torso.Address)
                return player.Torso;
            if (player.LowerTorso.Address)
                return player.LowerTorso;
            return player.HumanoidRootPart;
        }

        static bool readbone(
            const sdk::player& player,
            int hitPart,
            const sdk::vector3& cameraOrigin,
            sdk::vector3& outPos,
            sdk::instance& outPart)
        {
            outPart = resolvehitboxpart(player, hitPart);
            if (!outPart.Address)
                return false;

            sdk::part part(outPart.Address);
            sdk::part primitive = part.primitive();
            if (!primitive.Address)
                return false;

            outPos = primitive.position();
            if (std::isnan(outPos.x) || std::isnan(outPos.y) || std::isnan(outPos.z))
                return false;

            const float dist3d = (cameraOrigin - outPos).magnitude();
            outPos = prediction(outPart, outPos, dist3d);
            return !std::isnan(outPos.x);
        }

        static bool refreshlockedaim(const sdk::vector3& cameraOrigin)
        {
            if (!LockedCharacter || !global::render.Address)
                return false;

            for (const auto& player : cache::snapshot())
            {
                if (player.character.Address != LockedCharacter)
                    continue;

                sdk::vector3 bonePos{};
                sdk::instance bonePart{};
                if (!readbone(player, LockedHitboxIdx, cameraOrigin, bonePos, bonePart))
                    return false;

                AimPositionW = bonePos;
                AimPositionS = global::render.screen(bonePos);
                return AimPositionS.x > -0.5f && AimPositionS.y > -0.5f;
            }

            return false;
        }

        static bool validtarget(
            const sdk::player& Plr,
            const sdk::camera& Cam,
            const sdk::vector3& CameraOrigin,
            const POINT& CursorPos,
            HWND RobloxWindow,
            int hitboxIdx,
            sdk::vector3& outBonePos,
            sdk::vector2& outScreenPos,
            float& outScore)
        {
            if (Plr.Local_Player ||
                (global::LocalPlayer.character.Address != 0 && Plr.character.Address == global::LocalPlayer.character.Address) ||
                !Plr.character.Address || !Plr.Head.Address)
                return false;

            if (global::aim::KnockedCheck && knocked(Plr))
                return false;

            sdk::instance bonePart{};
            sdk::vector3 bonePos{};
            if (!readbone(Plr, hitboxIdx, CameraOrigin, bonePos, bonePart))
                return false;

            const sdk::vector2 ScreenPos = global::render.screen(bonePos);
            if (ScreenPos.x <= -0.5f || ScreenPos.y <= -0.5f)
                return false;

            const float Dist3D = (CameraOrigin - bonePos).magnitude();
            if (Dist3D > 700.f)
                return false;

            RECT ClientRect{};
            GetClientRect(RobloxWindow, &ClientRect);
            if (ScreenPos.x > ClientRect.right || ScreenPos.y > ClientRect.bottom)
                return false;

            if (global::aim::VisibleCheck && !visible(Cam, bonePos, ScreenPos))
                return false;

            const float Dist2D = sqrtf(
                (ScreenPos.x - (float)CursorPos.x) * (ScreenPos.x - (float)CursorPos.x) +
                (ScreenPos.y - (float)CursorPos.y) * (ScreenPos.y - (float)CursorPos.y));

            if (global::aim::useFov && Dist2D > global::aim::FovSize)
                return false;

            outBonePos = bonePos;
            outScreenPos = ScreenPos;
            outScore = global::aim::TargetPriority == 1 ? Dist3D : Dist2D;
            return true;
        }

        static void applytarget(
            const sdk::vector3& bonePos,
            const sdk::vector2& screenPos,
            const std::string& name,
            uintptr_t characterAddr,
            int hitboxIdx)
        {
            if (LockedCharacter != characterAddr)
                HasSmoothedAim = false;

            LockedHitboxIdx = hitboxIdx;
            LockedCharacter = characterAddr;
            AimPositionW = bonePos;
            AimPositionS = screenPos;
            CurrentLockedName = name;
            global::aim::AimTarget = sdk::instance(characterAddr);
            TargetFound = true;
            StickyMissFrames = 0;
        }
    }

    static bool knocked(const sdk::player& player)
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
        sdk::vector3 Forward = (TargetPos - CamPos);
        if (Forward.magnitude() < 0.0001f)
            return {};

        Forward = Forward.normalize();
        sdk::vector3 Right = cross({ 0.f, 1.f, 0.f }, Forward);
        if (Right.magnitude() < 0.0001f)
            return {};
        Right = Right.normalize();
        sdk::vector3 Up = cross(Forward, Right);

        sdk::matrix3 M;
        M.data[0] = -Right.x;  M.data[1] = Up.x;     M.data[2] = -Forward.x;
        M.data[3] = Right.y;   M.data[4] = Up.y;     M.data[5] = -Forward.y;
        M.data[6] = -Right.z;  M.data[7] = Up.z;     M.data[8] = -Forward.z;
        return M;
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
        if (!global::render.Address)
            return;

        HWND RobloxWindow = FindWindowA(0, "Roblox");
        if (!RobloxWindow)
            return;

        POINT CursorPos{};
        if (!GetCursorPos(&CursorPos) || !ScreenToClient(RobloxWindow, &CursorPos))
            return;

        sdk::instance CameraInst = global::camera.Address
            ? sdk::instance(global::camera.Address)
            : sdk::model(global::model.Address).childclass("Workspace").childclass("Camera");
        if (!CameraInst.Address)
            return;

        sdk::camera Cam(CameraInst.Address);
        const sdk::vector3 CameraOrigin = Cam.position();
        const auto PlayersSnapshot = cache::snapshot();

        auto tryplayer = [&](const sdk::player& Plr, int hitboxIdx) -> bool {
            sdk::vector3 bonePos{};
            sdk::vector2 screenPos{};
            float score = 0.f;
            if (!validtarget(Plr, Cam, CameraOrigin, CursorPos, RobloxWindow, hitboxIdx, bonePos, screenPos, score))
                return false;

            if (LockedCharacter != 0 && Plr.character.Address != LockedCharacter)
            {
                float currentScore = 999999.f;
                for (auto& current : PlayersSnapshot)
                {
                    if (current.character.Address != LockedCharacter)
                        continue;

                    sdk::vector3 currentBone{};
                    sdk::vector2 currentScreen{};
                    if (!validtarget(current, Cam, CameraOrigin, CursorPos, RobloxWindow, LockedHitboxIdx, currentBone, currentScreen, currentScore))
                        return false;
                    break;
                }

                if (score > currentScore * TargetSwitchBias)
                    return false;
            }

            applytarget(bonePos, screenPos, Plr.name, Plr.character.Address, hitboxIdx);
            if (global::aim::AimbotSticky && !IsPersisting)
            {
                PersistenceName = Plr.name;
                IsPersisting = true;
            }
            return true;
        };

        if (global::aim::AimbotSticky && IsPersisting && !PersistenceName.empty())
        {
            for (auto& Plr : PlayersSnapshot)
            {
                if (Plr.name != PersistenceName)
                    continue;

                if (tryplayer(Plr, LockedHitboxIdx))
                    return;

                if (++StickyMissFrames >= StickyMissGrace)
                    resetaimstate();
                return;
            }

            if (++StickyMissFrames >= StickyMissGrace)
                resetaimstate();
            return;
        }

        if (LockedCharacter != 0)
        {
            for (auto& Plr : PlayersSnapshot)
            {
                if (Plr.character.Address != LockedCharacter)
                    continue;

                if (tryplayer(Plr, LockedHitboxIdx))
                    return;

                if (++StickyMissFrames >= StickyMissGrace)
                    LockedCharacter = 0;
                return;
            }

            LockedCharacter = 0;
        }

        float closestDistance = 999999.f;
        std::string bestName;
        uintptr_t bestCharacterAddr = 0;
        sdk::vector3 bestBonePos{};
        sdk::vector2 bestScreenPos{};
        int bestHitbox = 0;
        bool foundCandidate = false;

        for (auto& Plr : PlayersSnapshot)
        {
            sdk::vector3 bonePos{};
            sdk::vector2 screenPos{};
            float score = 0.f;

            const int resolvedHitbox = std::clamp(global::aim::HitPart, 0, 2);
            if (!validtarget(Plr, Cam, CameraOrigin, CursorPos, RobloxWindow, resolvedHitbox, bonePos, screenPos, score))
                continue;

            if (score < closestDistance)
            {
                closestDistance = score;
                bestName = Plr.name;
                bestCharacterAddr = Plr.character.Address;
                bestBonePos = bonePos;
                bestScreenPos = screenPos;
                bestHitbox = resolvedHitbox;
                foundCandidate = true;
            }
        }

        if (!foundCandidate)
            return;

        applytarget(bestBonePos, bestScreenPos, bestName, bestCharacterAddr, bestHitbox);
        if (global::aim::AimbotSticky && !IsPersisting)
        {
            PersistenceName = bestName;
            IsPersisting = true;
        }
    }

    void triggerbot()
    {
        if (!global::aim::TriggerBot || !global::render.Address)
            return;

        static ULONGLONG LastShot = 0;
        POINT CursorPos{};
        HWND Window = FindWindowA(0, "Roblox");
        if (!Window || GetForegroundWindow() != Window || !GetCursorPos(&CursorPos) || !ScreenToClient(Window, &CursorPos))
        {
            TriggerOnTargetFrames = 0;
            return;
        }

        sdk::model Dm(global::model.Address);
        sdk::instance WorkspaceInst = Dm.childclass("Workspace");
        sdk::instance CameraInst = WorkspaceInst.child("Camera");
        if (!CameraInst.Address)
        {
            TriggerOnTargetFrames = 0;
            return;
        }

        const sdk::vector2 Cursor{ (float)CursorPos.x, (float)CursorPos.y };
        const float Padding = std::max(0.f, global::aim::TriggerRadius);
        sdk::camera Cam(CameraInst.Address);
        bool onTarget = false;

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
            if (global::aim::VisibleCheck && !visible(Cam, Pos, ScreenPos))
                continue;

            onTarget = true;
            break;
        }

        if (!onTarget)
        {
            TriggerOnTargetFrames = 0;
            return;
        }

        if (++TriggerOnTargetFrames < TriggerConfirmFrames)
            return;

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
        TriggerOnTargetFrames = 0;
    }

    void update() {
        if (!TargetFound)
            return;
        if (!hitchance())
            return;

        sdk::instance CameraInst = global::camera.Address
            ? sdk::instance(global::camera.Address)
            : sdk::model(global::model.Address).childclass("Workspace").childclass("Camera");
        if (!CameraInst.Address)
            return;

        sdk::camera Cam(CameraInst.Address);
        const sdk::vector3 CamPos = Cam.position();
        if (!refreshlockedaim(CamPos))
            return;

        sdk::vector3 targetWorld = AimPositionW;

        if (global::aim::Aimbot_type == 1) {

            if (global::aim::Shake) {
                targetWorld.x += ((float)rand() / RAND_MAX * 2 - 1) * global::aim::ShakeX;
                targetWorld.y += ((float)rand() / RAND_MAX * 2 - 1) * global::aim::ShakeY;
                targetWorld.z += ((float)rand() / RAND_MAX * 2 - 1) * global::aim::ShakeZ;
            }

            if (!HasSmoothedAim) {
                SmoothedWorld = targetWorld;
                HasSmoothedAim = true;
            }

            const float blendX = smoothstep(1.f / (1.f + std::max(0.f, global::aim::camera::Smoothing_X) * 0.9f));
            const float blendY = smoothstep(1.f / (1.f + std::max(0.f, global::aim::camera::Smoothing_Y) * 0.9f));
            const float blendZ = (blendX + blendY) * 0.5f;

            SmoothedWorld.x += (targetWorld.x - SmoothedWorld.x) * blendX;
            SmoothedWorld.y += (targetWorld.y - SmoothedWorld.y) * blendY;
            SmoothedWorld.z += (targetWorld.z - SmoothedWorld.z) * blendZ;

            sdk::vector3 Delta = SmoothedWorld - CamPos;
            if (Delta.magnitude() < 0.01f)
                return;

            sdk::matrix3 TargetMatrix = lookat(CamPos, SmoothedWorld);
            sdk::matrix3 CurrentMatrix = Cam.rotation();
            const float rotBlend = camerablend();
            sdk::matrix3 FinalMatrix = lerpmatrix3(CurrentMatrix, TargetMatrix, rotBlend);
            Cam.rotation(FinalMatrix);
        }
        else {
            POINT CursorPos{};
            HWND RobloxWindow = FindWindowA(0, "Roblox");
            if (!RobloxWindow || !GetCursorPos(&CursorPos) || !ScreenToClient(RobloxWindow, &CursorPos))
                return;

            sdk::vector2 targetScreen = AimPositionS;

            if (!HasSmoothedAim) {
                SmoothedScreen = { (float)CursorPos.x, (float)CursorPos.y };
                HasSmoothedAim = true;
            }

            float blendX = smoothstep(1.f / (1.f + std::max(0.f, global::aim::mouse::Smoothing_X) * 0.85f));
            float blendY = smoothstep(1.f / (1.f + std::max(0.f, global::aim::mouse::Smoothing_Y) * 0.85f));
            float sens = global::aim::mouse::Mouse_Sensitivty;
            if (sens <= 0.f)
                sens = 1.f;
            blendX = clampf(blendX * sens, 0.04f, 1.f);
            blendY = clampf(blendY * sens, 0.04f, 1.f);

            SmoothedScreen.x += (targetScreen.x - SmoothedScreen.x) * blendX;
            SmoothedScreen.y += (targetScreen.y - SmoothedScreen.y) * blendY;

            if (global::aim::Shake) {
                SmoothedScreen.x += ((float)rand() / RAND_MAX * 2 - 1) * global::aim::ShakeX;
                SmoothedScreen.y += ((float)rand() / RAND_MAX * 2 - 1) * global::aim::ShakeY;
            }

            POINT targetPt{
                (LONG)std::lround(SmoothedScreen.x),
                (LONG)std::lround(SmoothedScreen.y)
            };
            ClientToScreen(RobloxWindow, &targetPt);
            SetCursorPos(targetPt.x, targetPt.y);
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
                            resetaimstate();
                        }
                    }
                    else {

                        if (Pressed) {
                            acquire();
                            update();
                            WorkedThisTick = true;
                        }
                        else {
                            resetaimstate();
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
