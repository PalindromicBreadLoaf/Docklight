#include "Updater.h"

#ifdef ENABLE_UPDATER

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include <curl/curl.h>
#include <mbedtls/sha256.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sys/statvfs.h>
#include <zip.h>

#include <libultraship/libultraship.h>

#include "port/build.h"
#include "port/UI/cvar_prefixes.h"
#include "port/UI/Notification.h"

namespace Updater {

bool QueueNextLoad(const std::string& nroPath);

namespace {

constexpr uint64_t kMaxDownloadBytes = 512ull * 1024 * 1024;
constexpr size_t kCopyChunk = 64 * 1024;

// Kindly don't replace these :)
const char* const kProtectedNames[] = { "bk.o2r", "lighthouse.cfg.json" };

std::mutex sMutex;
Status sStatus;
std::thread sWorker;
std::atomic_bool sWorkerRunning{ false };
std::atomic_bool sCancel{ false };
std::atomic_bool sCurlReady{ false };

std::string sProgramPath;
std::string sDownloadUrl;
uint64_t sDownloadSize = 0;

void SetState(State state, const std::string& message = "") {
    std::lock_guard<std::mutex> lock(sMutex);
    sStatus.state = state;
    sStatus.message = message;
}

void Fail(const std::string& message) {
    SPDLOG_ERROR("[Updater] {}", message);
    SetState(State::Failed, message);
}

void FailOrCancel(State cancelledState, const std::string& message) {
    if (sCancel.load()) {
        SetState(cancelledState, "Cancelled. Nothing was changed.");
    } else {
        Fail(message);
    }
}

std::string InstallDir() {
    if (!sProgramPath.empty()) {
        const size_t slash = sProgramPath.find_last_of("/\\");
        if (slash != std::string::npos && slash > 0) {
            return sProgramPath.substr(0, slash);
        }
    }
    return ".";
}

std::string PathIn(const std::string& name) {
    return InstallDir() + "/" + name;
}

bool IsSafePayloadName(const std::string& name) {
    if (name.empty() || name.size() > 128 || name == "." || name == "..") {
        return false;
    }
    if (name.find_first_of("/\\:") != std::string::npos) {
        return false;
    }
    for (const char* protectedName : kProtectedNames) {
        if (name == protectedName) {
            return false;
        }
    }
    return true;
}

std::string Basename(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool ParseVersion(const std::string& text, Version& out) {
    for (size_t i = 0; i < text.size(); i++) {
        if (!isdigit(static_cast<unsigned char>(text[i]))) {
            continue;
        }
        unsigned major = 0, minor = 0, patch = 0;
        int consumed = 0;
        if (sscanf(text.c_str() + i, "%u.%u.%u%n", &major, &minor, &patch, &consumed) == 3 && consumed > 0) {
            out.major = static_cast<uint16_t>(major);
            out.minor = static_cast<uint16_t>(minor);
            out.patch = static_cast<uint16_t>(patch);
            return true;
        }
        while (i < text.size() && isdigit(static_cast<unsigned char>(text[i]))) {
            i++;
        }
    }
    return false;
}

size_t WriteToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    const size_t bytes = size * nmemb;
    if (out->size() + bytes > kMaxDownloadBytes) {
        return 0;
    }
    out->append(ptr, bytes);
    return bytes;
}

size_t WriteToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    return fwrite(ptr, size, nmemb, static_cast<FILE*>(userdata));
}

int ProgressCb(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    if (sCancel.load()) {
        return 1;
    }
    if (clientp != nullptr && *static_cast<const bool*>(clientp)) {
        std::lock_guard<std::mutex> lock(sMutex);
        sStatus.bytesDone = static_cast<uint64_t>(dlnow < 0 ? 0 : dlnow);
        sStatus.bytesTotal = static_cast<uint64_t>(dltotal < 0 ? 0 : dltotal);
    }
    return 0;
}

std::string UserAgent() {
    return std::string("Docklight-Updater/") + gDocklightVersion;
}

bool HttpGet(const std::string& url, std::string* bodyOut, FILE* fileOut, bool isGitHubApi, bool trackProgress,
             std::string& error) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        error = "Could not initialise the network stack.";
        return false;
    }

    curl_slist* headers = nullptr;
    if (isGitHubApi) {
        headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
        headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    const std::string userAgent = UserAgent();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 45L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &trackProgress);

    if (bodyOut != nullptr) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, bodyOut);
    } else {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fileOut);
    }

    const CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res == CURLE_ABORTED_BY_CALLBACK) {
        error = "Cancelled.";
        return false;
    }
    if (res != CURLE_OK) {
        error = std::string("Network error: ") + curl_easy_strerror(res);
        return false;
    }
    if (httpCode == 403 || httpCode == 429) {
        error = "GitHub is rate limiting this device. Try again in an hour.";
        return false;
    }
    if (httpCode == 404) {
        error = "No published release found for " + std::string(gDocklightUpdateRepo) + ".";
        return false;
    }
    if (httpCode < 200 || httpCode >= 300) {
        error = "GitHub returned HTTP " + std::to_string(httpCode) + ".";
        return false;
    }
    return true;
}

std::string ToHex(const unsigned char* digest, size_t len) {
    static const char* kHexDigits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out.push_back(kHexDigits[digest[i] >> 4]);
        out.push_back(kHexDigits[digest[i] & 0xF]);
    }
    return out;
}

bool ExtractEntry(zip_t* archive, zip_uint64_t index, const std::string& destPath, std::string& sha256Out,
                  std::string& error) {
    zip_file_t* entry = zip_fopen_index(archive, index, 0);
    if (entry == nullptr) {
        error = "Could not read the release archive.";
        return false;
    }
    FILE* dest = fopen(destPath.c_str(), "wb");
    if (dest == nullptr) {
        zip_fclose(entry);
        error = "Could not write to " + destPath + ".";
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);

    std::vector<char> buffer(kCopyChunk);
    bool ok = true;
    for (;;) {
        if (sCancel.load()) {
            error = "Cancelled.";
            ok = false;
            break;
        }
        const zip_int64_t read = zip_fread(entry, buffer.data(), buffer.size());
        if (read < 0) {
            error = "The release archive is corrupt.";
            ok = false;
            break;
        }
        if (read == 0) {
            break;
        }
        if (fwrite(buffer.data(), 1, static_cast<size_t>(read), dest) != static_cast<size_t>(read)) {
            error = "Ran out of space writing " + Basename(destPath) + ".";
            ok = false;
            break;
        }
        mbedtls_sha256_update_ret(&sha, reinterpret_cast<unsigned char*>(buffer.data()), static_cast<size_t>(read));
    }

    unsigned char digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (fflush(dest) != 0) {
        ok = false;
        error = "Ran out of space writing " + Basename(destPath) + ".";
    }
    fclose(dest);
    zip_fclose(entry);

    if (!ok) {
        remove(destPath.c_str());
        return false;
    }
    sha256Out = ToHex(digest, sizeof(digest));
    return true;
}

bool ReadEntryToString(zip_t* archive, zip_uint64_t index, std::string& out, std::string& error) {
    zip_stat_t stat;
    if (zip_stat_index(archive, index, 0, &stat) != 0 || (stat.valid & ZIP_STAT_SIZE) == 0) {
        error = "The release archive is corrupt.";
        return false;
    }
    if (stat.size > 1024 * 1024) {
        error = "The release manifest is implausibly large.";
        return false;
    }
    zip_file_t* entry = zip_fopen_index(archive, index, 0);
    if (entry == nullptr) {
        error = "Could not read the release archive.";
        return false;
    }
    out.resize(static_cast<size_t>(stat.size));
    const zip_int64_t read = out.empty() ? 0 : zip_fread(entry, out.data(), out.size());
    zip_fclose(entry);
    if (read < 0 || static_cast<size_t>(read) != out.size()) {
        error = "The release manifest is truncated.";
        return false;
    }
    return true;
}

std::map<std::string, std::string> ParseManifest(const std::string& text) {
    std::map<std::string, std::string> hashes;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        const size_t space = line.find(' ');
        if (space != 64) {
            continue;
        }
        std::string hash = line.substr(0, 64);
        std::string name = line.substr(space);
        name.erase(0, name.find_first_not_of(" *"));
        std::transform(hash.begin(), hash.end(), hash.begin(),
                       [](unsigned char c) { return static_cast<char>(tolower(c)); });
        if (hash.find_first_not_of("0123456789abcdef") != std::string::npos) {
            continue;
        }
        if (!IsSafePayloadName(name)) {
            SPDLOG_WARN("[Updater] Ignoring manifest entry '{}'", name);
            continue;
        }
        hashes[name] = hash;
    }
    return hashes;
}

bool HasFreeSpace(uint64_t needed) {
    struct statvfs vfs;
    if (statvfs(InstallDir().c_str(), &vfs) != 0) {
        return true;
    }
    const uint64_t free = static_cast<uint64_t>(vfs.f_bsize) * static_cast<uint64_t>(vfs.f_bavail);
    return free >= needed;
}

bool CommitPayload(const std::vector<std::string>& names, std::string& error) {
    std::vector<std::string> committed;
    for (const auto& name : names) {
        const std::string live = PathIn(name);
        const std::string backup = live + ".bak";
        const std::string staged = live + ".new";

        remove(backup.c_str());
        const bool hadOriginal = rename(live.c_str(), backup.c_str()) == 0;
        if (rename(staged.c_str(), live.c_str()) != 0) {
            error = "Could not replace " + name + ".";
            if (hadOriginal) {
                rename(backup.c_str(), live.c_str());
            }
            for (const auto& done : committed) {
                const std::string doneLive = PathIn(done);
                remove(doneLive.c_str());
                rename((doneLive + ".bak").c_str(), doneLive.c_str());
            }
            return false;
        }
        committed.push_back(name);
    }
    return true;
}

void RemoveStaged(const std::vector<std::string>& names) {
    for (const auto& name : names) {
        remove((PathIn(name) + ".new").c_str());
    }
}

void CheckWorker(bool silent) {
    SetState(State::Checking, "Contacting GitHub...");

    const std::string url = "https://api.github.com/repos/" + std::string(gDocklightUpdateRepo) + "/releases/latest";
    std::string body;
    std::string error;
    if (!HttpGet(url, &body, nullptr, true, false, error)) {
        if (silent) {
            SPDLOG_WARN("[Updater] Background check failed: {}", error);
            SetState(State::Idle);
        } else {
            FailOrCancel(State::Idle, error);
        }
        return;
    }

    const nlohmann::json release = nlohmann::json::parse(body, nullptr, false);
    if (release.is_discarded() || !release.is_object()) {
        Fail("GitHub sent a response this build could not read.");
        return;
    }

    const std::string tag = release.value("tag_name", "");
    Version latest;
    if (!ParseVersion(tag, latest) && !ParseVersion(release.value("name", ""), latest)) {
        Fail("Could not read a version number out of release tag '" + tag + "'.");
        return;
    }

    std::string assetUrl;
    std::string assetName;
    uint64_t assetSize = 0;
    if (release.contains("assets") && release["assets"].is_array()) {
        for (const auto& asset : release["assets"]) {
            std::string name = asset.value("name", "");
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(tolower(c)); });
            if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".zip") != 0) {
                continue;
            }
            const bool namesSwitch = lower.find("switch") != std::string::npos;
            if (assetUrl.empty() || namesSwitch) {
                assetUrl = asset.value("browser_download_url", "");
                assetName = name;
                assetSize = asset.value("size", 0ull);
            }
            if (namesSwitch) {
                break;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(sMutex);
        sStatus.latestTag = tag;
        sStatus.releaseName = release.value("name", tag);
        sStatus.releaseNotes = release.value("body", "");
        sStatus.hasDownload = !assetUrl.empty();
        sStatus.bytesDone = 0;
        sStatus.bytesTotal = assetSize;
        sStatus.installedFiles.clear();
        sDownloadUrl = assetUrl;
        sDownloadSize = assetSize;
    }

    if (!(latest > CurrentVersion())) {
        SetState(State::UpToDate, "Docklight " + CurrentVersion().ToString() + " is the latest release.");
        if (!silent) {
            Notification::Emit({ .prefix = "Docklight", .message = "is up to date." });
        }
        return;
    }

    if (assetUrl.empty()) {
        SetState(State::UpdateAvailable, "Release " + tag +
                                             " has no downloadable archive. Install it manually from the "
                                             "release page.");
    } else {
        SetState(State::UpdateAvailable, "Docklight " + tag + " is available (" + assetName + ").");
    }
    Notification::Emit({ .prefix = "Docklight", .message = tag, .suffix = "is available!" });
}

void InstallWorker(std::string url, uint64_t expectedSize) {
    SetState(State::Downloading, "Starting download...");
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sStatus.bytesDone = 0;
    }

    if (expectedSize > 0 && !HasFreeSpace(expectedSize * 2)) {
        Fail("Not enough free space on the SD card. About " + std::to_string((expectedSize * 2) / (1024 * 1024)) +
             " MB is needed.");
        return;
    }

    const std::string zipPath = PathIn("docklight-update.zip.part");
    std::vector<std::string> staged;
    std::string error;

    FILE* zipFile = fopen(zipPath.c_str(), "wb");
    if (zipFile == nullptr) {
        Fail("Could not write to " + InstallDir());
        return;
    }
    const bool downloaded = HttpGet(url, nullptr, zipFile, false, true, error);
    const bool flushed = fflush(zipFile) == 0;
    fclose(zipFile);
    if (!downloaded || !flushed) {
        remove(zipPath.c_str());
        FailOrCancel(State::UpdateAvailable, downloaded ? "Ran out of space downloading the update." : error);
        return;
    }

    SetState(State::Installing, "Verifying download...");

    int zipError = 0;
    zip_t* archive = zip_open(zipPath.c_str(), ZIP_RDONLY, &zipError);
    if (archive == nullptr) {
        remove(zipPath.c_str());
        Fail("The downloaded archive could not be opened.");
        return;
    }

    std::map<std::string, zip_uint64_t> entries;
    std::vector<std::string> ambiguous;
    uint64_t payloadBytes = 0;
    const zip_int64_t entryCount = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < entryCount; i++) {
        const char* rawName = zip_get_name(archive, static_cast<zip_uint64_t>(i), 0);
        if (rawName == nullptr) {
            continue;
        }
        const std::string name = Basename(rawName);
        if (name.empty() || rawName[strlen(rawName) - 1] == '/') {
            continue; // directory entry
        }
        if (!entries.emplace(name, static_cast<zip_uint64_t>(i)).second) {
            ambiguous.push_back(name);
        }
        zip_stat_t stat;
        if (zip_stat_index(archive, static_cast<zip_uint64_t>(i), 0, &stat) == 0 && (stat.valid & ZIP_STAT_SIZE)) {
            payloadBytes += stat.size;
        }
    }

    const auto manifestEntry = entries.find("SHA256SUMS.txt");
    if (manifestEntry == entries.end()) {
        zip_close(archive);
        remove(zipPath.c_str());
        Fail("The release archive has no SHA256SUMS.txt.");
        return;
    }

    std::string manifestText;
    if (!ReadEntryToString(archive, manifestEntry->second, manifestText, error)) {
        zip_close(archive);
        remove(zipPath.c_str());
        Fail(error);
        return;
    }

    const std::map<std::string, std::string> expected = ParseManifest(manifestText);
    if (expected.empty()) {
        zip_close(archive);
        remove(zipPath.c_str());
        Fail("SHA256SUMS.txt listed no files this updater is allowed to install.");
        return;
    }

    if (!HasFreeSpace(payloadBytes + kCopyChunk)) {
        zip_close(archive);
        remove(zipPath.c_str());
        Fail("Not enough free space on the SD card to unpack the update.");
        return;
    }

    bool unpacked = true;
    for (const auto& [name, hash] : expected) {
        if (std::find(ambiguous.begin(), ambiguous.end(), name) != ambiguous.end()) {
            error = "The release archive contains more than one " + name + ".";
            unpacked = false;
            break;
        }
        const auto entry = entries.find(name);
        if (entry == entries.end()) {
            error = "SHA256SUMS.txt lists " + name + ", but the archive does not contain it.";
            unpacked = false;
            break;
        }

        SetState(State::Installing, "Unpacking " + name + "...");
        std::string actualHash;
        if (!ExtractEntry(archive, entry->second, PathIn(name) + ".new", actualHash, error)) {
            unpacked = false;
            break;
        }
        staged.push_back(name);
        if (actualHash != hash) {
            error = name + " failed its checksum. Nothing was changed.";
            unpacked = false;
            break;
        }
    }

    zip_close(archive);
    remove(zipPath.c_str());

    if (!unpacked) {
        RemoveStaged(staged);
        FailOrCancel(State::UpdateAvailable, error);
        return;
    }

    SetState(State::Installing, "Swapping files in...");
    if (!CommitPayload(staged, error)) {
        RemoveStaged(staged);
        Fail(error);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(sMutex);
        sStatus.installedFiles = staged;
        sStatus.state = State::Installed;
        sStatus.message = "Installed. Restart Docklight to run the new version.";
    }
    Notification::Emit({ .prefix = "Update installed.", .message = "Restart to play it." });
}

void RunAsync(std::function<void()> work) {
    bool expected = false;
    if (!sWorkerRunning.compare_exchange_strong(expected, true)) {
        return;
    }
    if (sWorker.joinable()) {
        sWorker.join();
    }
    sCancel.store(false);
    sWorker = std::thread([work = std::move(work)]() {
        work();
        sWorkerRunning.store(false);
    });
}

} // namespace

std::string Version::ToString() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

Version CurrentVersion() {
    return { gDocklightVersionMajor, gDocklightVersionMinor, gDocklightVersionPatch };
}

void SetProgramPath(const char* argv0) {
    if (argv0 != nullptr && argv0[0] != '\0') {
        sProgramPath = argv0;
    }
}

void Init() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        SPDLOG_ERROR("[Updater] curl_global_init failed.");
        return;
    }
    sCurlReady.store(true);
    SPDLOG_INFO("[Updater] Docklight {}, install directory {}", gDocklightVersion, InstallDir());

    if (CVarGetInteger(CVAR_SETTING("Updater.CheckOnBoot"), 0)) {
        CheckForUpdates(true);
    }
}

void Shutdown() {
    sCancel.store(true);
    if (sWorker.joinable()) {
        sWorker.join();
    }
    if (sCurlReady.exchange(false)) {
        curl_global_cleanup();
    }
}

bool IsBusy() {
    return sWorkerRunning.load();
}

void Cancel() {
    sCancel.store(true);
}

void CheckForUpdates(bool silent) {
    if (!sCurlReady.load()) {
        Fail("Networking is unavailable in this session.");
        return;
    }
    RunAsync([silent]() { CheckWorker(silent); });
}

void DownloadAndInstall() {
    if (!sCurlReady.load()) {
        Fail("Networking is unavailable in this session.");
        return;
    }
    std::string url;
    uint64_t size = 0;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        url = sDownloadUrl;
        size = sDownloadSize;
    }
    if (url.empty()) {
        Fail("No release archive to install. Check for updates first.");
        return;
    }
    RunAsync([url, size]() { InstallWorker(url, size); });
}

Status GetStatus() {
    std::lock_guard<std::mutex> lock(sMutex);
    return sStatus;
}

bool RestartIntoNewBuild() {
    const std::string nro = sProgramPath.empty() ? PathIn("Lighthouse.nro") : sProgramPath;
    if (!QueueNextLoad(nro)) {
        SPDLOG_ERROR("[Updater] Could not queue {} for relaunch", nro);
        return false;
    }
    Ship::Context::GetRawInstance()->GetWindow()->Close();
    return true;
}

} // namespace Updater

#else // ENABLE_UPDATER

namespace Updater {
// The updater is Switch only.
}

#endif // ENABLE_UPDATER
