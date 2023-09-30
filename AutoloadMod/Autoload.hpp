#pragma once

#include <unordered_map>
#include <unordered_set>

#include <Unreal/FFrame.hpp>
#include <Unreal/UStruct.hpp>
#include <Unreal/UObject.hpp>

namespace Autoload
{
    void Reset();
    void Init();
    void ScanForPakRoutines();
    void Update();
}
