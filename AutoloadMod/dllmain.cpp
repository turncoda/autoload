#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <Autoload.hpp>

using namespace RC;

class AutoloadMod : public CppUserModBase
{
public:
    AutoloadMod() : CppUserModBase()
    {
        ModName = STR("AutoloadMod");
        ModVersion = STR("0.2");
        ModDescription = STR("Automatically reloads paks on change.");
        ModAuthors = STR("Turncoda");

        Autoload::Init();
    }

    ~AutoloadMod()
    {
        Autoload::Reset();
    }

    auto on_update() -> void override
    {
    }
    auto on_unreal_init() -> void override
    {
        Autoload::ScanForPakRoutines();
    }
};

extern "C"
{
    __declspec(dllexport) CppUserModBase* start_mod()
    {
        return new AutoloadMod();
    }

    __declspec(dllexport) void uninstall_mod(CppUserModBase* mod)
    {
        delete mod;
    }
}
