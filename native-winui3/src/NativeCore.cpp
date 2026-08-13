#include "pch.h"
#include "NativeCore.h"

#include <wincrypt.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <fstream>
#include <algorithm>
#include <array>
#include <cwctype>
#include <cstdint>
#include <cstdio>

#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace fs = std::filesystem;

namespace
{
    constexpr wchar_t kRegistryPath[] = L"Software\\miHoYo\\原神";
    constexpr wchar_t kAdlName[] = L"MIHOYOSDK_ADL_PROD_CN_h3123967166";
    constexpr wchar_t kLastUidName[] = L"__LastUid___h2153286551";
    constexpr wchar_t kGeneralDataName[] = L"GENERAL_DATA_h2389025596";
    constexpr wchar_t kHypRegistryPath[] = L"Software\\miHoYo\\HYP\\1_1\\hk4e_cn";
    constexpr wchar_t kGameInstallPathName[] = L"GameInstallPath";
    constexpr char kCredentialMagic[] = "GASCRED1";
    constexpr char kRecoveryMagic[] = "GASREC01";
    constexpr char kEntropy[] = "GenshinAccountSwitcher.v1";
    constexpr std::size_t kMaxBlobBytes = 16u * 1024u * 1024u;

    struct RegKey
    {
        HKEY key{};
        ~RegKey() { if (key) RegCloseKey(key); }
        RegKey(RegKey const&) = delete;
        RegKey& operator=(RegKey const&) = delete;
        RegKey() = default;
    };

    std::runtime_error Win32Error(char const* where, LONG code = static_cast<LONG>(GetLastError()))
    {
        return std::runtime_error(std::string(where) + ":" + std::to_string(code));
    }

    gas::RegistryValueBlob ReadBinaryValue(HKEY key, wchar_t const* name)
    {
        DWORD type{};
        DWORD size{};
        LONG result = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
        if (result == ERROR_FILE_NOT_FOUND)
        {
            return gas::RegistryValueBlob::Missing();
        }
        if (result != ERROR_SUCCESS)
        {
            throw Win32Error("RegQueryValueExW", result);
        }
        if (type != REG_BINARY)
        {
            throw std::runtime_error("registry-value-not-binary");
        }

        std::vector<unsigned char> data(size);
        if (size != 0)
        {
            DWORD actual = size;
            result = RegQueryValueExW(key, name, nullptr, &type, data.data(), &actual);
            if (result != ERROR_SUCCESS)
            {
                throw Win32Error("RegQueryValueExW-data", result);
            }
            data.resize(actual);
        }
        return gas::RegistryValueBlob::Present(std::move(data));
    }

    std::wstring ReadStringValue(HKEY root, wchar_t const* subKey, wchar_t const* name)
    {
        RegKey key;
        LONG result = RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &key.key);
        if (result != ERROR_SUCCESS)
        {
            return {};
        }

        DWORD type{};
        DWORD bytes{};
        result = RegQueryValueExW(key.key, name, nullptr, &type, nullptr, &bytes);
        if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t))
        {
            return {};
        }

        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        result = RegQueryValueExW(key.key, name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
        if (result != ERROR_SUCCESS)
        {
            return {};
        }
        while (!value.empty() && value.back() == L'\0') value.pop_back();
        return value;
    }

    void AppendInt32(std::vector<unsigned char>& out, std::int32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            out.push_back(static_cast<unsigned char>((static_cast<std::uint32_t>(value) >> (i * 8)) & 0xffu));
        }
    }

    std::int32_t ReadInt32(std::vector<unsigned char> const& in, std::size_t& offset)
    {
        if (offset + 4 > in.size()) throw std::runtime_error("payload-truncated-int32");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
        {
            value |= static_cast<std::uint32_t>(in[offset++]) << (i * 8);
        }
        return static_cast<std::int32_t>(value);
    }

    void AppendBlob(std::vector<unsigned char>& out, gas::RegistryValueBlob const& blob)
    {
        out.push_back(blob.exists ? 1u : 0u);
        if (!blob.exists) return;
        if (blob.data.size() > kMaxBlobBytes) throw std::runtime_error("payload-too-large");
        AppendInt32(out, static_cast<std::int32_t>(blob.data.size()));
        out.insert(out.end(), blob.data.begin(), blob.data.end());
    }

    gas::RegistryValueBlob ReadBlob(std::vector<unsigned char> const& in, std::size_t& offset)
    {
        if (offset >= in.size()) throw std::runtime_error("payload-truncated-bool");
        bool exists = in[offset++] != 0;
        if (!exists) return gas::RegistryValueBlob::Missing();

        auto length = ReadInt32(in, offset);
        if (length < 0 || static_cast<std::size_t>(length) > kMaxBlobBytes || offset + static_cast<std::size_t>(length) > in.size())
        {
            throw std::runtime_error("payload-invalid-length");
        }
        std::vector<unsigned char> data(in.begin() + static_cast<std::ptrdiff_t>(offset), in.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += static_cast<std::size_t>(length);
        return gas::RegistryValueBlob::Present(std::move(data));
    }

    bool EqualNoCase(std::wstring const& a, std::wstring const& b)
    {
        return _wcsicmp(a.c_str(), b.c_str()) == 0;
    }
}

namespace gas
{
    RegistryValueBlob RegistryValueBlob::Missing()
    {
        return {};
    }

    RegistryValueBlob RegistryValueBlob::Present(std::vector<unsigned char> value)
    {
        RegistryValueBlob result;
        result.exists = true;
        result.data = std::move(value);
        return result;
    }

    bool RegistryValueBlob::ContentEquals(RegistryValueBlob const& other) const noexcept
    {
        return exists == other.exists && (!exists || data == other.data);
    }

    OperationResult OperationResult::Ok(std::wstring message)
    {
        return { true, std::move(message) };
    }

    OperationResult OperationResult::Fail(std::wstring message)
    {
        return { false, std::move(message) };
    }

    AppCore::AppCore()
    {
        std::wstring buffer(32768, L'\0');
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) throw Win32Error("GetModuleFileNameW");
        buffer.resize(length);
        m_baseDirectory = fs::path(buffer).parent_path();
        m_dataDirectory = m_baseDirectory / L"data";
        m_accountsDirectory = m_dataDirectory / L"accounts";
        m_recoveryDirectory = m_dataDirectory / L"recovery";
        m_logsDirectory = m_dataDirectory / L"logs";
        m_accountsFile = m_dataDirectory / L"accounts.json";
        m_settingsFile = m_dataDirectory / L"settings.json";
        m_recoveryFile = m_recoveryDirectory / L"LastRegistrySnapshot.dat";
        m_logFile = m_logsDirectory / L"app.log";
    }

    void AppCore::Initialize()
    {
        EnsureWritableDirectories();
        LoadAccounts();
        Log(L"Initialize", L"Success");
    }

    void AppCore::EnsureWritableDirectories()
    {
        std::error_code ec;
        fs::create_directories(m_accountsDirectory, ec);
        if (ec) throw std::runtime_error("create-accounts-directory");
        fs::create_directories(m_recoveryDirectory, ec);
        if (ec) throw std::runtime_error("create-recovery-directory");
        fs::create_directories(m_logsDirectory, ec);
        if (ec) throw std::runtime_error("create-logs-directory");

        auto testPath = m_dataDirectory / (L".write-test-" + std::to_wstring(GetCurrentProcessId()));
        HANDLE file = CreateFileW(testPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw Win32Error("data-directory-not-writable");
        CloseHandle(file);
        DeleteFileW(testPath.c_str());
    }

    void AppCore::ReloadAccounts()
    {
        LoadAccounts();
    }

    bool AppCore::RecoveryExists() const
    {
        std::error_code ec;
        return fs::exists(m_recoveryFile, ec) && !ec;
    }

    RegistrySnapshot AppCore::ReadRegistrySnapshot() const
    {
        RegKey key;
        LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_QUERY_VALUE, &key.key);
        if (result == ERROR_FILE_NOT_FOUND)
        {
            return {};
        }
        if (result != ERROR_SUCCESS) throw Win32Error("RegOpenKeyExW", result);

        RegistrySnapshot snapshot;
        snapshot.adl = ReadBinaryValue(key.key, kAdlName);
        snapshot.lastUid = ReadBinaryValue(key.key, kLastUidName);
        snapshot.generalData = ReadBinaryValue(key.key, kGeneralDataName);
        return snapshot;
    }

    void AppCore::WriteRegistryValue(wchar_t const* name, RegistryValueBlob const& value) const
    {
        RegKey key;
        DWORD disposition{};
        LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key.key, &disposition);
        if (result != ERROR_SUCCESS) throw Win32Error("RegCreateKeyExW", result);

        if (!value.exists)
        {
            result = RegDeleteValueW(key.key, name);
            if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) throw Win32Error("RegDeleteValueW", result);
        }
        else
        {
            DWORD size = static_cast<DWORD>(value.data.size());
            BYTE const* ptr = size ? value.data.data() : nullptr;
            result = RegSetValueExW(key.key, name, 0, REG_BINARY, ptr, size);
            if (result != ERROR_SUCCESS) throw Win32Error("RegSetValueExW", result);
        }
        RegFlushKey(key.key);
    }

    std::wstring AppCore::ParseUid(RegistryValueBlob const& blob)
    {
        if (!blob.exists || blob.data.empty()) return {};
        std::string text(blob.data.begin(), blob.data.end());
        while (!text.empty())
        {
            unsigned char c = static_cast<unsigned char>(text.back());
            if (c == 0 || c == ' ' || c == '\r' || c == '\n' || c == '\t') text.pop_back();
            else break;
        }
        if (text.empty()) return {};
        if (!std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) return {};
        return std::wstring(text.begin(), text.end());
    }

    bool AppCore::IsNumericUid(std::wstring const& uid)
    {
        return uid.size() >= 6 && uid.size() <= 12 && std::all_of(uid.begin(), uid.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
    }

    CurrentProbe AppCore::ProbeCurrent()
    {
        CurrentProbe probe;
        probe.snapshot = ReadRegistrySnapshot();
        probe.uid = ParseUid(probe.snapshot.lastUid);

        if (probe.snapshot.adl.exists)
        {
            for (std::size_t i = 0; i < m_accounts.size(); ++i)
            {
                try
                {
                    auto credential = LoadCredential(m_accounts[i].id);
                    if (credential.adl.ContentEquals(probe.snapshot.adl))
                    {
                        probe.exactAccountIndex = static_cast<int>(i);
                        break;
                    }
                }
                catch (...)
                {
                    Log(L"ReadCredentialForCurrentDetection", L"Failed", &m_accounts[i].id);
                }
            }
        }

        if (!probe.uid.empty())
        {
            for (std::size_t i = 0; i < m_accounts.size(); ++i)
            {
                if (m_accounts[i].uid == probe.uid) probe.uidMatches.push_back(static_cast<int>(i));
            }
        }
        return probe;
    }

    CurrentState AppCore::DetectCurrentState()
    {
        CurrentState state;
        auto probe = ProbeCurrent();
        state.uid = probe.uid;
        state.hasAdl = probe.snapshot.adl.exists && !probe.snapshot.adl.data.empty();
        state.gameRunning = IsGameRunning();
        state.gamePath = ResolveGameExecutable();

        if (probe.exactAccountIndex >= 0)
        {
            state.accountIndex = probe.exactAccountIndex;
            state.matchKind = CurrentMatchKind::ExactCredential;
        }
        else if (probe.uidMatches.size() == 1)
        {
            state.accountIndex = probe.uidMatches.front();
            state.matchKind = CurrentMatchKind::UniqueUidCredentialChanged;
        }
        else if (probe.uidMatches.size() > 1)
        {
            state.matchKind = CurrentMatchKind::AmbiguousUid;
        }
        return state;
    }

    int AppCore::SaveNewAccount(std::wstring const& name, std::wstring const& uid, RegistrySnapshot const& snapshot)
    {
        if (IsGameRunning()) throw std::runtime_error("game-running");
        ValidateCredential(snapshot);
        if (name.empty()) throw std::runtime_error("empty-name");
        if (!IsNumericUid(uid)) throw std::runtime_error("invalid-uid");

        AccountProfile profile;
        if (FAILED(CoCreateGuid(&profile.id))) throw std::runtime_error("guid-failed");
        profile.name = name;
        profile.uid = uid;
        profile.createdAt = UtcNowText();
        profile.updatedAt = profile.createdAt;

        SaveCredential(profile.id, snapshot);
        try
        {
            m_accounts.push_back(profile);
            SaveAccounts();
        }
        catch (...)
        {
            m_accounts.pop_back();
            DeleteCredential(profile.id);
            throw;
        }
        Log(L"AddAccount", L"Success", &profile.id);
        return static_cast<int>(m_accounts.size() - 1);
    }

    OperationResult AppCore::UpdateAccount(int index, RegistrySnapshot const& snapshot)
    {
        try
        {
            ValidateAccountIndex(index);
            if (IsGameRunning()) return OperationResult::Fail(L"原神正在运行，请完全退出游戏后再更新登录态。");
            ValidateCredential(snapshot);
            auto uid = ParseUid(snapshot.lastUid);
            if (uid.empty()) return OperationResult::Fail(L"当前注册表 UID 无法读取，已停止更新。");
            if (uid != m_accounts[index].uid) return OperationResult::Fail(L"当前注册表 UID 与所选账号不同，已停止更新。");

            auto oldCredential = LoadCredential(m_accounts[index].id);
            auto oldUpdatedAt = m_accounts[index].updatedAt;
            SaveCredential(m_accounts[index].id, snapshot);
            try
            {
                m_accounts[index].updatedAt = UtcNowText();
                SaveAccounts();
            }
            catch (...)
            {
                m_accounts[index].updatedAt = oldUpdatedAt;
                SaveCredential(m_accounts[index].id, oldCredential);
                throw;
            }
            Log(L"UpdateAccount", L"Success", &m_accounts[index].id);
            return OperationResult::Ok(L"登录态已更新。");
        }
        catch (...)
        {
            if (index >= 0 && index < static_cast<int>(m_accounts.size())) Log(L"UpdateAccount", L"Failed", &m_accounts[index].id);
            return OperationResult::Fail(L"更新失败，本地账号数据或 Windows 系统调用出现异常。");
        }
    }

    OperationResult AppCore::RenameAccount(int index, std::wstring const& newName)
    {
        try
        {
            ValidateAccountIndex(index);
            if (newName.empty()) return OperationResult::Fail(L"账号昵称不能为空。");
            auto old = m_accounts[index].name;
            m_accounts[index].name = newName;
            try { SaveAccounts(); }
            catch (...) { m_accounts[index].name = old; throw; }
            Log(L"RenameAccount", L"Success", &m_accounts[index].id);
            return OperationResult::Ok(L"账号昵称已更新。");
        }
        catch (...)
        {
            return OperationResult::Fail(L"重命名失败。");
        }
    }

    OperationResult AppCore::DeleteAccount(int index)
    {
        try
        {
            ValidateAccountIndex(index);
            auto id = m_accounts[index].id;
            auto credential = LoadCredential(id);
            auto old = m_accounts;
            m_accounts.erase(m_accounts.begin() + index);
            try
            {
                SaveAccounts();
                DeleteCredential(id);
            }
            catch (...)
            {
                m_accounts = std::move(old);
                try { SaveCredential(id, credential); } catch (...) {}
                throw;
            }
            Log(L"DeleteAccount", L"Success", &id);
            return OperationResult::Ok(L"已删除本工具保存的账号记录。");
        }
        catch (...)
        {
            return OperationResult::Fail(L"删除失败。");
        }
    }

    OperationResult AppCore::SwitchAccount(int index)
    {
        try
        {
            ValidateAccountIndex(index);
            if (IsGameRunning()) return OperationResult::Fail(L"原神正在运行，请完全退出游戏后再切换账号。");

            auto target = LoadCredential(m_accounts[index].id);
            ValidateCredential(target);
            auto before = ReadRegistrySnapshot();
            SaveRecovery(before);

            WriteRegistryValue(kAdlName, target.adl);
            WriteRegistryValue(kLastUidName, target.lastUid);

            auto after = ReadRegistrySnapshot();
            if (!after.adl.ContentEquals(target.adl) || !after.lastUid.ContentEquals(target.lastUid))
            {
                Log(L"SwitchAccount", L"VerificationFailed", &m_accounts[index].id);
                return OperationResult::Fail(L"切换后的注册表逐字节校验失败。已停止后续操作，可使用“恢复上一次状态”。");
            }
            Log(L"SwitchAccount", L"Success", &m_accounts[index].id);
            return OperationResult::Ok(L"账号切换成功。");
        }
        catch (...)
        {
            if (index >= 0 && index < static_cast<int>(m_accounts.size())) Log(L"SwitchAccount", L"Failed", &m_accounts[index].id);
            return OperationResult::Fail(L"账号切换失败，本地凭据文件或 Windows 系统调用出现异常。");
        }
    }

    OperationResult AppCore::RestoreLastSnapshot()
    {
        try
        {
            if (IsGameRunning()) return OperationResult::Fail(L"原神正在运行，请完全退出游戏后再恢复。");
            auto snapshot = LoadRecovery();
            WriteRegistryValue(kAdlName, snapshot.adl);
            WriteRegistryValue(kLastUidName, snapshot.lastUid);
            WriteRegistryValue(kGeneralDataName, snapshot.generalData);
            auto after = ReadRegistrySnapshot();
            if (!after.adl.ContentEquals(snapshot.adl) || !after.lastUid.ContentEquals(snapshot.lastUid) || !after.generalData.ContentEquals(snapshot.generalData))
            {
                Log(L"Restore", L"VerificationFailed");
                return OperationResult::Fail(L"恢复后的注册表逐字节校验失败。");
            }
            Log(L"Restore", L"Success");
            return OperationResult::Ok(L"已恢复上一次切换前的注册表状态。");
        }
        catch (...)
        {
            Log(L"Restore", L"Failed");
            return OperationResult::Fail(L"恢复失败，当前没有可用恢复点或恢复文件无法读取。");
        }
    }

    bool AppCore::IsGameRunning() const
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (_wcsicmp(entry.szExeFile, L"YuanShen.exe") == 0)
                {
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }

    std::wstring AppCore::ResolveGameExecutable()
    {
        auto settings = LoadSettings();
        if (!settings.manualGamePath.empty() && IsValidGameExecutable(settings.manualGamePath)) return fs::absolute(settings.manualGamePath).wstring();

        auto install = ReadStringValue(HKEY_CURRENT_USER, kHypRegistryPath, kGameInstallPathName);
        if (!install.empty())
        {
            fs::path candidate = fs::path(install) / L"YuanShen.exe";
            if (IsValidGameExecutable(candidate)) return fs::absolute(candidate).wstring();
        }
        return {};
    }

    OperationResult AppCore::SetManualGameExecutable(std::wstring const& path)
    {
        try
        {
            if (!IsValidGameExecutable(path)) return OperationResult::Fail(L"请选择有效的 YuanShen.exe。");
            auto settings = LoadSettings();
            settings.manualGamePath = fs::absolute(path).wstring();
            SaveSettings(settings);
            return OperationResult::Ok(L"游戏路径已保存。");
        }
        catch (...)
        {
            return OperationResult::Fail(L"保存游戏路径失败。");
        }
    }

    OperationResult AppCore::LaunchGame()
    {
        try
        {
            auto path = ResolveGameExecutable();
            if (path.empty()) return OperationResult::Fail(L"没有找到 YuanShen.exe，请先手动指定游戏路径。");
            fs::path exe(path);
            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = SEE_MASK_NOCLOSEPROCESS;
            info.lpVerb = L"open";
            info.lpFile = exe.c_str();
            auto working = exe.parent_path().wstring();
            info.lpDirectory = working.c_str();
            info.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&info)) throw Win32Error("ShellExecuteExW");
            if (info.hProcess) CloseHandle(info.hProcess);
            Log(L"LaunchGame", L"Success");
            return OperationResult::Ok(L"原神已启动。");
        }
        catch (...)
        {
            Log(L"LaunchGame", L"Failed");
            return OperationResult::Fail(L"启动原神失败。");
        }
    }

    void AppCore::ValidateAccountIndex(int index) const
    {
        if (index < 0 || index >= static_cast<int>(m_accounts.size())) throw std::runtime_error("invalid-account-index");
    }

    void AppCore::ValidateCredential(RegistrySnapshot const& snapshot)
    {
        if (!snapshot.adl.exists || snapshot.adl.data.empty()) throw std::runtime_error("invalid-adl");
    }

    std::filesystem::path AppCore::CredentialFile(GUID const& id) const
    {
        return m_accountsDirectory / (GuidToString(id) + L".dat");
    }

    void AppCore::SaveCredential(GUID const& id, RegistrySnapshot const& snapshot) const
    {
        ValidateCredential(snapshot);
        auto plain = SerializeSnapshot(snapshot, false);
        AtomicWriteBytes(CredentialFile(id), Protect(plain));
    }

    RegistrySnapshot AppCore::LoadCredential(GUID const& id) const
    {
        auto path = CredentialFile(id);
        if (!fs::exists(path)) throw std::runtime_error("credential-missing");
        auto result = DeserializeSnapshot(Unprotect(ReadAllBytes(path)), false);
        ValidateCredential(result);
        return result;
    }

    void AppCore::DeleteCredential(GUID const& id) const
    {
        std::error_code ec;
        fs::remove(CredentialFile(id), ec);
        if (ec) throw std::runtime_error("credential-delete-failed");
    }

    void AppCore::SaveRecovery(RegistrySnapshot const& snapshot) const
    {
        AtomicWriteBytes(m_recoveryFile, Protect(SerializeSnapshot(snapshot, true)));
        (void)LoadRecovery();
    }

    RegistrySnapshot AppCore::LoadRecovery() const
    {
        if (!fs::exists(m_recoveryFile)) throw std::runtime_error("recovery-missing");
        return DeserializeSnapshot(Unprotect(ReadAllBytes(m_recoveryFile)), true);
    }

    std::vector<unsigned char> AppCore::SerializeSnapshot(RegistrySnapshot const& snapshot, bool recovery)
    {
        std::vector<unsigned char> out;
        auto magic = recovery ? kRecoveryMagic : kCredentialMagic;
        out.insert(out.end(), magic, magic + 8);
        AppendInt32(out, 1);
        AppendBlob(out, snapshot.adl);
        AppendBlob(out, snapshot.lastUid);
        AppendBlob(out, snapshot.generalData);
        return out;
    }

    RegistrySnapshot AppCore::DeserializeSnapshot(std::vector<unsigned char> const& bytes, bool recovery)
    {
        auto expected = recovery ? kRecoveryMagic : kCredentialMagic;
        if (bytes.size() < 12 || !std::equal(bytes.begin(), bytes.begin() + 8, expected)) throw std::runtime_error("payload-magic");
        std::size_t offset = 8;
        if (ReadInt32(bytes, offset) != 1) throw std::runtime_error("payload-version");
        RegistrySnapshot result;
        result.adl = ReadBlob(bytes, offset);
        result.lastUid = ReadBlob(bytes, offset);
        result.generalData = ReadBlob(bytes, offset);
        if (offset != bytes.size()) throw std::runtime_error("payload-trailing-data");
        return result;
    }

    std::vector<unsigned char> AppCore::Protect(std::vector<unsigned char> const& plaintext)
    {
        DATA_BLOB input{ static_cast<DWORD>(plaintext.size()), const_cast<BYTE*>(reinterpret_cast<BYTE const*>(plaintext.data())) };
        DATA_BLOB entropy{ static_cast<DWORD>(sizeof(kEntropy) - 1), const_cast<BYTE*>(reinterpret_cast<BYTE const*>(kEntropy)) };
        DATA_BLOB output{};
        if (!CryptProtectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) throw Win32Error("CryptProtectData");
        std::vector<unsigned char> result(output.pbData, output.pbData + output.cbData);
        if (output.pbData) LocalFree(output.pbData);
        return result;
    }

    std::vector<unsigned char> AppCore::Unprotect(std::vector<unsigned char> const& ciphertext)
    {
        DATA_BLOB input{ static_cast<DWORD>(ciphertext.size()), const_cast<BYTE*>(reinterpret_cast<BYTE const*>(ciphertext.data())) };
        DATA_BLOB entropy{ static_cast<DWORD>(sizeof(kEntropy) - 1), const_cast<BYTE*>(reinterpret_cast<BYTE const*>(kEntropy)) };
        DATA_BLOB output{};
        LPWSTR description{};
        if (!CryptUnprotectData(&input, &description, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) throw Win32Error("CryptUnprotectData");
        if (description) LocalFree(description);
        std::vector<unsigned char> result(output.pbData, output.pbData + output.cbData);
        if (output.pbData) LocalFree(output.pbData);
        return result;
    }

    void AppCore::LoadAccounts()
    {
        m_accounts.clear();
        if (!fs::exists(m_accountsFile)) return;
        auto text = ReadTextUtf8(m_accountsFile);
        if (text.empty()) return;

        auto root = winrt::Windows::Data::Json::JsonObject::Parse(winrt::hstring(text));
        if (!root.HasKey(L"accounts")) return;
        auto array = root.GetNamedArray(L"accounts");
        for (auto const& value : array)
        {
            auto obj = value.GetObject();
            AccountProfile profile;
            profile.id = GuidFromString(obj.GetNamedString(L"id"));
            profile.name = obj.GetNamedString(L"name").c_str();
            profile.uid = obj.GetNamedString(L"uid").c_str();
            profile.createdAt = obj.GetNamedString(L"createdAt", L"").c_str();
            profile.updatedAt = obj.GetNamedString(L"updatedAt", L"").c_str();
            if (profile.name.empty() || !IsNumericUid(profile.uid)) throw std::runtime_error("invalid-account-metadata");
            m_accounts.push_back(std::move(profile));
        }
        std::sort(m_accounts.begin(), m_accounts.end(), [](auto const& a, auto const& b) { return a.createdAt < b.createdAt; });
    }

    void AppCore::SaveAccounts() const
    {
        using namespace winrt::Windows::Data::Json;
        JsonObject root;
        JsonArray array;
        for (auto const& account : m_accounts)
        {
            JsonObject obj;
            obj.Insert(L"id", JsonValue::CreateStringValue(winrt::hstring(GuidToString(account.id))));
            obj.Insert(L"name", JsonValue::CreateStringValue(winrt::hstring(account.name)));
            obj.Insert(L"uid", JsonValue::CreateStringValue(winrt::hstring(account.uid)));
            obj.Insert(L"createdAt", JsonValue::CreateStringValue(winrt::hstring(account.createdAt)));
            obj.Insert(L"updatedAt", JsonValue::CreateStringValue(winrt::hstring(account.updatedAt)));
            array.Append(obj);
        }
        root.Insert(L"accounts", array);
        AtomicWriteTextUtf8(m_accountsFile, root.Stringify().c_str());
    }

    AppSettings AppCore::LoadSettings() const
    {
        AppSettings settings;
        if (!fs::exists(m_settingsFile)) return settings;
        try
        {
            auto text = ReadTextUtf8(m_settingsFile);
            if (text.empty()) return settings;
            auto obj = winrt::Windows::Data::Json::JsonObject::Parse(winrt::hstring(text));
            settings.manualGamePath = obj.GetNamedString(L"manualGamePath", L"").c_str();
        }
        catch (...) {}
        return settings;
    }

    void AppCore::SaveSettings(AppSettings const& settings) const
    {
        using namespace winrt::Windows::Data::Json;
        JsonObject obj;
        obj.Insert(L"manualGamePath", JsonValue::CreateStringValue(winrt::hstring(settings.manualGamePath)));
        AtomicWriteTextUtf8(m_settingsFile, obj.Stringify().c_str());
    }

    std::wstring AppCore::GuidToString(GUID const& id)
    {
        wchar_t buffer[64]{};
        int length = StringFromGUID2(id, buffer, static_cast<int>(std::size(buffer)));
        if (length <= 0) throw std::runtime_error("guid-format");
        std::wstring text(buffer);
        if (text.size() >= 2 && text.front() == L'{' && text.back() == L'}') text = text.substr(1, text.size() - 2);
        return text;
    }

    GUID AppCore::GuidFromString(std::wstring const& text)
    {
        std::wstring decorated = text;
        if (decorated.empty()) throw std::runtime_error("guid-empty");
        if (decorated.front() != L'{') decorated = L"{" + decorated + L"}";
        GUID id{};
        if (FAILED(CLSIDFromString(decorated.c_str(), &id))) throw std::runtime_error("guid-parse");
        return id;
    }

    std::wstring AppCore::UtcNowText()
    {
        SYSTEMTIME st{};
        GetSystemTime(&st);
        wchar_t buffer[32]{};
        swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buffer;
    }

    std::vector<unsigned char> AppCore::ReadAllBytes(fs::path const& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("file-open-read");
        stream.seekg(0, std::ios::end);
        auto length = stream.tellg();
        if (length < 0) throw std::runtime_error("file-length");
        stream.seekg(0, std::ios::beg);
        std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
        if (!bytes.empty()) stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream && !bytes.empty()) throw std::runtime_error("file-read");
        return bytes;
    }

    std::wstring AppCore::ReadTextUtf8(fs::path const& path)
    {
        auto bytes = ReadAllBytes(path);
        std::string text(bytes.begin(), bytes.end());
        return winrt::to_hstring(text).c_str();
    }

    void AppCore::AtomicWriteBytes(fs::path const& path, std::vector<unsigned char> const& bytes)
    {
        fs::create_directories(path.parent_path());
        fs::path temp = path;
        temp += L".tmp-" + std::to_wstring(GetCurrentProcessId());
        HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw Win32Error("CreateFileW-write");
        bool ok = true;
        DWORD written{};
        if (!bytes.empty())
        {
            ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
        }
        if (ok) ok = FlushFileBuffers(file) != FALSE;
        CloseHandle(file);
        if (!ok)
        {
            DeleteFileW(temp.c_str());
            throw Win32Error("WriteFile");
        }
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temp.c_str());
            throw Win32Error("MoveFileExW");
        }
    }

    void AppCore::AtomicWriteTextUtf8(fs::path const& path, std::wstring const& text)
    {
        auto utf8 = winrt::to_string(winrt::hstring(text));
        AtomicWriteBytes(path, std::vector<unsigned char>(utf8.begin(), utf8.end()));
    }

    bool AppCore::IsValidGameExecutable(fs::path const& path)
    {
        if (path.empty()) return false;
        std::error_code ec;
        if (!fs::exists(path, ec) || ec || !fs::is_regular_file(path, ec) || ec) return false;
        return EqualNoCase(path.filename().wstring(), L"YuanShen.exe");
    }

    void AppCore::Log(std::wstring const& operation, std::wstring const& result, GUID const* accountId) const noexcept
    {
        try
        {
            std::wstring line = UtcNowText() + L" | " + operation + L" | " + result;
            if (accountId) line += L" | " + GuidToString(*accountId);
            line += L"\r\n";
            auto utf8 = winrt::to_string(winrt::hstring(line));
            HANDLE file = CreateFileW(m_logFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return;
            DWORD written{};
            WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            CloseHandle(file);
        }
        catch (...) {}
    }
}
