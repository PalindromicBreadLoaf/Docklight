#ifndef UPDATER_H
#define UPDATER_H

#ifdef __cplusplus

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

// In-game updater for Docklight releases.
namespace Updater {

enum class State {
    Idle,            // nothing has been attempted yet
    Checking,        // querying the GitHub releases API
    UpToDate,        // running version is current
    UpdateAvailable, // a newer release exists
    Downloading,     // fetching and unpacking the release zip
    Installing,      // verifying hashes and swapping files in
    Installed,       // done
    Failed,          // see Status::message
};

struct Version {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t patch = 0;

    bool operator>(const Version& o) const {
        return std::tie(major, minor, patch) > std::tie(o.major, o.minor, o.patch);
    }
    std::string ToString() const;
};

struct Status {
    State state = State::Idle;
    std::string latestTag;    // tag_name (e.g. "1.0.2")
    std::string releaseName;  // human-readable release title
    std::string releaseNotes; // markdown body
    std::string message;      // error text
    bool hasDownload = false; // false when the release ships no installable archive
    uint64_t bytesDone = 0;   // download progress
    uint64_t bytesTotal = 0;
    std::vector<std::string> installedFiles;
};

void SetProgramPath(const char* argv0);

// Starts curl and, if the "check at startup" setting is on, check- for an update.
void Init();

void Shutdown();

bool IsBusy();

void CheckForUpdates(bool silent);

void DownloadAndInstall();

void Cancel();

Status GetStatus();

bool RestartIntoNewBuild();

Version CurrentVersion();

} // namespace Updater

#endif // __cplusplus

#endif // UPDATER_H
