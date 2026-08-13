#pragma once

#include <windows.h>
#include <winrt/base.h>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace gas
{
    struct RegistryValueBlob
    {
        bool exists{ false };
        std::vector<unsigned char> data;

        static RegistryValueBlob Missing();
        static RegistryValueBlob Present(std::vector<unsigned char> value);
        bool ContentEquals(RegistryValueBlob const& other) const noexcept;
    };

    struct RegistrySnapshot
    {
        RegistryValueBlob adl;
        RegistryValueBlob lastUid;
        RegistryValueBlob generalData;
    };

    struct AccountProfile
    {
        GUID id{};
        std::wstring name;
        std::wstring uid;
        std::wstring createdAt;
        std::wstring updatedAt;
    };

    struct AppSettings
    {
        std::wstring manualGamePath;
    };

    enum class CurrentMatchKind
    {
        None,
        ExactCredential,
        UniqueUidCredentialChanged,
        AmbiguousUid
    };

    struct CurrentState
    {
        std::wstring uid;
        int accountIndex{ -1 };
        CurrentMatchKind matchKind{ CurrentMatchKind::None };
        bool hasAdl{ false };
        bool gameRunning{ false };
        std::wstring gamePath;
    };

    struct CurrentProbe
    {
        RegistrySnapshot snapshot;
        std::wstring uid;
        int exactAccountIndex{ -1 };
        std::vector<int> uidMatches;
    };

    struct OperationResult
    {
        bool success{ false };
        std::wstring message;

        static OperationResult Ok(std::wstring message);
        static OperationResult Fail(std::wstring message);
    };

    class AppCore
    {
    public:
        AppCore();

        void Initialize();
        void ReloadAccounts();

        std::vector<AccountProfile> const& Accounts() const noexcept { return m_accounts; }
        std::filesystem::path const& BaseDirectory() const noexcept { return m_baseDirectory; }
        std::filesystem::path const& DataDirectory() const noexcept { return m_dataDirectory; }
        bool RecoveryExists() const;

        CurrentState DetectCurrentState();
        CurrentProbe ProbeCurrent();

        int SaveNewAccount(std::wstring const& name, std::wstring const& uid, RegistrySnapshot const& snapshot);
        OperationResult UpdateAccount(int index, RegistrySnapshot const& snapshot);
        OperationResult RenameAccount(int index, std::wstring const& newName);
        OperationResult DeleteAccount(int index);
        OperationResult SwitchAccount(int index);
        OperationResult RestoreLastSnapshot();

        std::wstring ResolveGameExecutable();
        OperationResult SetManualGameExecutable(std::wstring const& path);
        OperationResult LaunchGame();

        bool IsGameRunning() const;
        static std::wstring ParseUid(RegistryValueBlob const& blob);
        static bool IsNumericUid(std::wstring const& uid);

    private:
        std::filesystem::path m_baseDirectory;
        std::filesystem::path m_dataDirectory;
        std::filesystem::path m_accountsDirectory;
        std::filesystem::path m_recoveryDirectory;
        std::filesystem::path m_logsDirectory;
        std::filesystem::path m_accountsFile;
        std::filesystem::path m_settingsFile;
        std::filesystem::path m_recoveryFile;
        std::filesystem::path m_logFile;
        std::vector<AccountProfile> m_accounts;

        RegistrySnapshot ReadRegistrySnapshot() const;
        void WriteRegistryValue(wchar_t const* name, RegistryValueBlob const& value) const;

        std::filesystem::path CredentialFile(GUID const& id) const;
        void SaveCredential(GUID const& id, RegistrySnapshot const& snapshot) const;
        RegistrySnapshot LoadCredential(GUID const& id) const;
        void DeleteCredential(GUID const& id) const;

        void SaveRecovery(RegistrySnapshot const& snapshot) const;
        RegistrySnapshot LoadRecovery() const;

        void SaveAccounts() const;
        void LoadAccounts();
        AppSettings LoadSettings() const;
        void SaveSettings(AppSettings const& settings) const;

        static std::vector<unsigned char> SerializeSnapshot(RegistrySnapshot const& snapshot, bool recovery);
        static RegistrySnapshot DeserializeSnapshot(std::vector<unsigned char> const& bytes, bool recovery);
        static std::vector<unsigned char> Protect(std::vector<unsigned char> const& plaintext);
        static std::vector<unsigned char> Unprotect(std::vector<unsigned char> const& ciphertext);

        static std::wstring GuidToString(GUID const& id);
        static GUID GuidFromString(std::wstring const& text);
        static GUID GuidFromString(winrt::hstring const& text) { return GuidFromString(std::wstring(text.c_str())); }
        static std::wstring UtcNowText();
        static std::wstring ReadTextUtf8(std::filesystem::path const& path);
        static std::vector<unsigned char> ReadAllBytes(std::filesystem::path const& path);
        static void AtomicWriteBytes(std::filesystem::path const& path, std::vector<unsigned char> const& bytes);
        static void AtomicWriteTextUtf8(std::filesystem::path const& path, std::wstring const& text);
        static bool IsValidGameExecutable(std::filesystem::path const& path);
        void Log(std::wstring const& operation, std::wstring const& result, GUID const* accountId = nullptr) const noexcept;
        void EnsureWritableDirectories();
        void ValidateAccountIndex(int index) const;
        static void ValidateCredential(RegistrySnapshot const& snapshot);
    };
}
