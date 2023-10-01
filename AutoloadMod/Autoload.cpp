#include <Autoload.hpp>

#include <chrono>
#include <iostream>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <SigScanner/SinglePassSigScanner.hpp>
#include <UE4SSProgram.hpp>
#include <Unreal/Signatures.hpp>
#include <Unreal/UnrealVersion.hpp>

#include <windows.h>

namespace
{
    using namespace RC::Unreal;
    using namespace RC;

    std::filesystem::path g_pak_dir;

    void StartDirWatcher();
    DWORD WINAPI ThreadProc(LPVOID);
    void CommitStagedPaks(std::filesystem::path);

    void Error2(std::wstring a, std::wstring b)
    {
        RC::Output::send<RC::LogLevel::Error>(STR("[Autoload]: {} {}\n"), a, b);
    }
    void Error(std::wstring msg)
    {
        RC::Output::send<RC::LogLevel::Error>(STR("[Autoload]: {}\n"), msg);
    }
    void Warn2(std::wstring a, int b)
    {
        RC::Output::send<RC::LogLevel::Warning>(STR("[Autoload]: {} ({})\n"), a, b);
    }
    void Warn2(std::wstring a, std::wstring b)
    {
        RC::Output::send<RC::LogLevel::Warning>(STR("[Autoload]: {} {}\n"), a, b);
    }
    void Warn(std::wstring msg)
    {
        RC::Output::send<RC::LogLevel::Warning>(STR("[Autoload]: {}\n"), msg);
    }
    void Debug(std::wstring msg)
    {
        RC::Output::send<RC::LogLevel::Verbose>(STR("[Autoload]: {}\n"), msg);
    }
    void Debug2(std::wstring a, std::wstring b)
    {
        RC::Output::send<RC::LogLevel::Verbose>(STR("[Autoload]: {} {}\n"), a, b);
    }
    void Debug2(std::wstring msg, void* ptr)
    {
        RC::Output::send<RC::LogLevel::Verbose>(STR("[Autoload]: {} {}\n"), msg, ptr);
    }
    void Debug3(std::wstring msg, void* ptr, int num)
    {
        RC::Output::send<RC::LogLevel::Verbose>(STR("[Autoload]: {} {} ({})\n"), msg, ptr, num);
    }

    struct FPakFile
    {
        void* idk1;
        void* idk2;
        void* idk3;
        FString PakFilename;
    };

    struct FPakListEntry
    {
        uint32_t ReadOrder;
        FPakFile* PakFile;
    };

    struct FPakPlatformFile
    {
        void* vftable;
        void* LowerLevel;
        TArray<FPakListEntry> PakFiles;
    };

    struct DelegateMount
    {
        void* idk1;
        void* idk2;
        void* idk3;
        FPakPlatformFile* pak;
        bool (*fn)(FPakPlatformFile*, FString&, uint32_t);
    };

    struct DelegateUnmount
    {
        void* idk1;
        void* idk2;
        void* idk3;
        FPakPlatformFile* pak;
        bool (*fn)(FPakPlatformFile*, FString&);
    };

    // TODO right now these are not getting assigned to correctly on startup
    DelegateMount* g_mount_delegate_ptr = nullptr;
    DelegateUnmount* g_unmount_delegate_ptr = nullptr;

    std::function<bool(FString&, uint32_t)> g_mount;
    std::function<bool(FString&)> g_unmount;

    bool OnMatchFound(SignatureContainer& container)
    {
        Debug2(L"Found signature match for FPakPlatformFile dtor:", (void*)container.get_match_address());

        int num = 0;

        uint8_t* data = static_cast<uint8_t*>(container.get_match_address());
        void* last = nullptr;
        for (uint8_t* i : std::views::iota(data) | std::views::take(3000))
        {
            // look for 'mov rcx,[rel address]'
            if (i[0] == 0x48 && i[1] == 0x8b && i[2] == 0x0d)
            {
                // mov found, resolve RIP
                int32_t rip;
                memcpy(&rip, i + 3, sizeof(rip));

                void* ptr = i + 3 + 4 + rip;

                if (ptr != last)
                {
                    last = ptr;

                    Debug3(L"Found delegate:", ptr, num);

                    switch (num)
                    {
                        case 2: g_mount_delegate_ptr = *(DelegateMount**)ptr; break;
                        case 3:
                            g_unmount_delegate_ptr = *(DelegateUnmount**)ptr;
                            container.get_did_succeed() = g_mount_delegate_ptr && g_unmount_delegate_ptr;
                            return true;
                    }

                    num += 1;
                }
            }
        }

        return false;
    }

    void OnScanFinished(const SignatureContainer& container)
    {
        Debug2(L"Mount delegate address:", (void*)g_mount_delegate_ptr);
        Debug2(L"Unmount delegate address:", (void*)g_unmount_delegate_ptr);
        if (!container.get_did_succeed())
        {
            Warn(L"Failed to find or resolve delegates");
            return;
        }

        g_mount = std::bind(g_mount_delegate_ptr->fn, g_mount_delegate_ptr->pak, std::placeholders::_1, std::placeholders::_2);
        g_unmount = std::bind(g_unmount_delegate_ptr->fn, g_unmount_delegate_ptr->pak, std::placeholders::_1);

        if (!g_mount || !g_unmount)
        {
            Warn(L"Failed to bind mount and unmount functions");
            return;
        }

        CommitStagedPaks(g_pak_dir);
        StartDirWatcher();
    }

    HANDLE gh_dir_watcher_thread;
    HANDLE gh_thread_exit_event;

    void StartDirWatcher()
    {
        if (gh_dir_watcher_thread != NULL)
        {
            Error(L"Tried to create new directory watcher thread but thread already "
                  L"exists!");
            return;
        }
        gh_dir_watcher_thread = CreateThread(NULL, // default security attributes
                                             0, // stack size
                                             ThreadProc, // thread function name
                                             NULL, // argument to thread function
                                             0, // flags
                                             NULL // thread id
        );
        if (gh_dir_watcher_thread == NULL) { Error(L"Failed to create directory watcher thread"); }
    }

    DWORD WINAPI ThreadProc(LPVOID lpParam)
    {
        HANDLE h_dir_change = FindFirstChangeNotificationA(g_pak_dir.string().c_str(), false, FILE_NOTIFY_CHANGE_FILE_NAME);
        if (h_dir_change == INVALID_HANDLE_VALUE)
        {
            Error(L"FindFirstChangeNotificationA returned invalid handle");
            return 1;
        }
        Debug2(L"Now watching for changes in", g_pak_dir.wstring());
        HANDLE handles[2] = {h_dir_change, gh_thread_exit_event};
        while (true)
        {
            DWORD wait_status = WaitForMultipleObjects(2, // max num wait objects
                                                       handles, // pointer to handles
                                                       FALSE, // return on any handle signaled
                                                       INFINITE // timeout
            );
            if (wait_status == WAIT_OBJECT_0 + 1)
            {
                Debug(L"Directory watcher thread is shutting down...");
                break;
            }
            if (wait_status != WAIT_OBJECT_0)
            {
                Error(L"WaitForMultipleObjects failed");
                break;
            }

            Debug2(L"Change detected in:", g_pak_dir.wstring());
            // handle change
            CommitStagedPaks(g_pak_dir);

            if (!FindNextChangeNotification(h_dir_change))
            {
                Error(L"FindNextChangeNotification failed");
                break;
            }
        }
        if (!FindCloseChangeNotification(h_dir_change))
        {
            Error(L"FindCloseChangeNotification failed");
            return 1;
        }
        return 0;
    }

    bool IsMounted(std::filesystem::path my_pak)
    {
        for (int i = 0; i < g_mount_delegate_ptr->pak->PakFiles.Num(); i++)
        {
            FPakListEntry entry = g_mount_delegate_ptr->pak->PakFiles[i];
            std::filesystem::path mounted_pak(entry.PakFile->PakFilename.GetCharArray());
            if (std::filesystem::equivalent(my_pak, mounted_pak)) { return true; }
        }
        return false;
    }

    enum class CommitAction
    {
        HotSwap,
        RenameAndMount,
        MountOnly,
    };

    void CommitStagedPaks(std::filesystem::path pak_dir)
    {
        if (!g_mount || !g_unmount)
        {
            Error(L"Pak routines are not callable. Aborting commit.");
            return;
        }
        std::error_code err;
        int num_tries = 0;
        std::vector<std::tuple<std::filesystem::path, CommitAction>> pak_paths;
        for (const auto& dir_entry : std::filesystem::directory_iterator(pak_dir))
        {
            if (!dir_entry.is_regular_file()) { continue; }
            std::filesystem::path src = dir_entry.path();
            if (dir_entry.path().extension() == ".pak")
            {
                if (!IsMounted(src))
                {
                    Debug2(L"Found pak that is not already mounted:", src.wstring());
                    Debug(L"Will mount.");
                    pak_paths.push_back(std::make_tuple(src, CommitAction::MountOnly));
                }
                else
                {
                    //Debug2(L"Found pak that is already mounted:", src.wstring());
                }
            }
            else if (dir_entry.path().extension() == ".staged")
            {
                std::filesystem::path dst(src);
                dst.replace_filename(src.stem());
                std::filesystem::directory_entry dst_entry(dst);
                if (std::filesystem::exists(dst))
                {
                    Debug2(L"Found staged changes for:", dst.filename().wstring());
                    Debug(L"Will hot swap.");
                    pak_paths.push_back(std::make_tuple(src, CommitAction::HotSwap));
                }
                else
                {
                    Debug2(L"Found staged file without counterpart:", src.filename().wstring());
                    Debug(L"Will rename and mount.");
                    pak_paths.push_back(std::make_tuple(src, CommitAction::RenameAndMount));
                }
            }
        }
        for (const auto& t : pak_paths)
        {
            const auto& src = std::get<std::filesystem::path>(t);
            const auto& action = std::get<CommitAction>(t);
            std::filesystem::path dst(src);
            dst.replace_filename(src.stem());
            FString fstr_dst(dst.wstring().c_str());
            const int max_retries = 5;
            int num_retries = 0;
            Debug2(L"Working on:", dst.filename().wstring());
            switch (action)
            {
            case CommitAction::HotSwap:
                num_retries = 0;
                while (num_retries < max_retries)
                {
                    if (g_unmount(fstr_dst))
                    {
                        Debug(L"  Unmounted pak.");
                        break;
                    }
                    else
                    {
                        Warn(L"  Failed to unmount pak.");
                        num_retries++;
                        if (num_retries < max_retries)
                        {
                            Warn(L"  Sleeping for 200ms and trying again.");
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        }
                    }
                }

                num_retries = 0;
                err.clear();
                Debug(L"  Deleting pak...");
                while (num_retries < max_retries)
                {
                    std::filesystem::remove(dst, err);
                    if (!err) {
                      break;
                    }
                    Warn(L"  Delete failed. Sleeping for 200ms and trying again...");
                    num_retries++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                if (num_retries == max_retries) {
                    Error(L"  Delete failed. Aborting...");
                    continue;
                }

                // Renaming is more likely to succeed with a bit of buffer time.
                std::this_thread::sleep_for(std::chrono::milliseconds(200));

                // fallthrough
            case CommitAction::RenameAndMount:
                err.clear();
                Debug(L"  Renaming staged pak...");
                std::filesystem::rename(src, dst, err);
                while (err)
                {
                    Warn(L"  Rename failed. Sleeping for 200ms and trying again...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    std::filesystem::rename(src, dst, err);
                }

                // fallthrough
            case CommitAction::MountOnly:
                if (g_mount(fstr_dst, 100))
                {
                    Debug(L"  Mounted pak.");
                }
                else
                {
                    Error(L"  Failed to mount pak. Try again later.");
                }
                // fallthrough
            }
        }
    }
} // namespace

namespace Autoload
{
    void Update()
    {}
    void Reset()
    {
        if (gh_dir_watcher_thread != NULL)
        {
            Debug(L"Asking directory watcher thread to shut itself down...");
            SetEvent(gh_thread_exit_event);
            DWORD wait_status = WaitForSingleObject(gh_dir_watcher_thread, 10000);
            if (wait_status == WAIT_TIMEOUT)
            {
                Error(L"Directory watcher thread did not shut down after 10 seconds. "
                      L"Terminating.");
                if (!TerminateThread(gh_thread_exit_event, 1))
                {
                    Error(L"Something went wrong while forcibly terminating the thread.");
                    throw;
                }
            }
            else if (wait_status != WAIT_OBJECT_0)
            {
                Error(L"Something went wrong while waiting for thread to shut down.");
                throw;
            }
            Debug(L"Directory watcher thread has shut down.");
        }

        if (!CloseHandle(gh_thread_exit_event))
        {
            Error(L"Something went wrong while closing the thread exit event.");
            throw;
        }
        Debug(L"Closed thread exit event.");
    }

    void Init()
    {
        if (gh_thread_exit_event != NULL) { Warn(L"Recreating ThreadExitEvent even though it already exists."); }
        gh_thread_exit_event = CreateEvent(NULL, // default security attributes
                                           TRUE, // manual-reset event
                                           FALSE, // initial state is nonsignaled
                                           TEXT("ThreadExitEvent") // object name
        );
        if (gh_thread_exit_event == NULL) { Error(L"Failed to create ThreadExitEvent"); }

        std::filesystem::path working_dir = UE4SSProgram::get_program().get_working_directory();
        g_pak_dir = working_dir.parent_path().parent_path() / "Content" / "Paks" / "Autoload";
        if (!std::filesystem::exists(g_pak_dir))
        {
            Error2(L"Autoload directory does not exist:", g_pak_dir.wstring());
            Error(L"Please create Autoload directory and restart the mod.");
            return;
        }
    }

    void ScanForPakRoutines()
    {
        if (g_mount && g_unmount) { Warn(L"Mount and Unmount routines already found; rescanning anyway"); }
        // Signature for FPakPlatformFile dtor in UE 5.1
        SignatureData sig{"40 53 56 57 48 83 ec ?? 48 8d 05 ?? ?? ?? ?? 4c 89 74 24 ?? 48 89 01 48 "
                          "8b f9 ?? ?? ?? ?? ?? e8 ?? ?? ?? ?? 48 8b 87"};
        std::vector<RC::SignatureData> sigs = {sig};
        RC::SignatureContainer container(sigs, OnMatchFound, OnScanFinished);
        RC::SinglePassScanner::SignatureContainerMap container_map{{ScanTarget::Core, {container}}};
        Debug(L"Scanning for Mount and Unmount routines...");
        RC::SinglePassScanner::start_scan(container_map);
    }
} // namespace Autoload
