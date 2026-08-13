#include "LighthouseMenu.h"
#include "LighthouseModals.h"
#include "UIWidgets.hpp"
#include "port/build.h"
#include "port/Updater/Updater.h"

#include <cfloat>
#include <spdlog/fmt/fmt.h>

namespace LighthouseGui {

extern std::shared_ptr<LighthouseModalWindow> mModalWindow;
using namespace UIWidgets;

#ifdef ENABLE_UPDATER

namespace {

std::string FormatBytes(uint64_t bytes) {
    if (bytes >= 1024ull * 1024) {
        return fmt::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return fmt::format("{} KB", bytes / 1024);
}

void DrawStatus(WidgetInfo& info) {
    const Updater::Status status = Updater::GetStatus();

    ImGui::Text("Currently installed Docklight %s", gDocklightVersion);
    if (!status.latestTag.empty()) {
        ImGui::Text("Latest on GitHub: %s", status.latestTag.c_str());
    }

    ImVec4 color;
    switch (status.state) {
        case Updater::State::Failed:
            color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
            break;
        case Updater::State::UpdateAvailable:
        case Updater::State::Installed:
            color = ImVec4(0.45f, 1.0f, 0.55f, 1.0f);
            break;
        default:
            color = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
            break;
    }
    if (!status.message.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(color, "%s", status.message.c_str());
        ImGui::PopTextWrapPos();
    }

    if (status.state == Updater::State::Installed && !status.installedFiles.empty()) {
        std::string replaced;
        for (const auto& file : status.installedFiles) {
            replaced += (replaced.empty() ? "" : ", ") + file;
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s will be replaced on restart", replaced.c_str());
        ImGui::PopTextWrapPos();
    }

    if (status.state == Updater::State::Downloading && status.bytesTotal > 0) {
        const float fraction = static_cast<float>(status.bytesDone) / static_cast<float>(status.bytesTotal);
        const std::string overlay = FormatBytes(status.bytesDone) + " / " + FormatBytes(status.bytesTotal);
        ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), overlay.c_str());
    } else if (status.state == Updater::State::Downloading) {
        ImGui::Text("Downloaded %s...", FormatBytes(status.bytesDone).c_str());
    }
}

void DrawReleaseNotes(WidgetInfo& info) {
    const Updater::Status status = Updater::GetStatus();
    if (status.releaseNotes.empty()) {
        return;
    }
    ImGui::SeparatorText(status.releaseName.empty() ? "Release Notes" : status.releaseName.c_str());
    ImGui::BeginChild("UpdaterReleaseNotes", ImVec2(0.0f, 180.0f), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(status.releaseNotes.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void DrawActions(WidgetInfo& info) {
    const Updater::Status status = Updater::GetStatus();
    const bool busy = Updater::IsBusy();

    ButtonOptions checkOptions = {};
    checkOptions.color = Colors::LightBlue;
    checkOptions.disabled = busy;
    checkOptions.disabledTooltip = "An update job is already running.";
    checkOptions.tooltip = "Check whether a newer Docklight release has been published.";
    if (UIWidgets::Button("Check for Updates", checkOptions)) {
        Updater::CheckForUpdates(false);
    }

    const bool canInstall = status.state == Updater::State::UpdateAvailable && status.hasDownload;
    if (canInstall) {
        ButtonOptions installOptions = {};
        installOptions.color = Colors::Green;
        installOptions.disabled = busy;
        installOptions.disabledTooltip = "An update job is already running.";
        installOptions.tooltip =
            "Download and verify the release, then restart into it. Your saves, settings and bk.o2r are left alone. "
            "You will need to rebuild your bk.o2r file if either major or minor version changed.";
        if (UIWidgets::Button("Download and Install", installOptions)) {
            mModalWindow->RegisterPopup(
                "Install Update",
                "Docklight " + status.latestTag +
                    " will be downloaded and checked now, then swapped in the next time Docklight "
                    "starts.\n\nYour saves and settings will be preserved.",
                "Install", "Cancel", []() { Updater::DownloadAndInstall(); }, nullptr);
        }
    }

    if (busy) {
        ButtonOptions cancelOptions = {};
        cancelOptions.color = Colors::Gray;
        cancelOptions.tooltip = "Quit the transfer.";
        if (UIWidgets::Button("Cancel", cancelOptions)) {
            Updater::Cancel();
        }
    }

    if (status.state == Updater::State::Installed) {
        ButtonOptions restartOptions = {};
        restartOptions.color = Colors::Green;
        restartOptions.tooltip = "Quit and relaunch to finish installing the downloaded version.";
        if (UIWidgets::Button("Restart Now", restartOptions)) {
            mModalWindow->RegisterPopup(
                "Restart Docklight",
                "Docklight will close, swap the new files in as it starts, and reopen on the new version. Unsaved "
                "progress is lost.",
                "Restart", "Cancel",
                []() {
                    if (!Updater::RestartIntoNewBuild()) {
                        mModalWindow->RegisterPopup("Restart Failed",
                                                    "This launcher can't relaunch homebrew automatically. Quit "
                                                    "Docklight and start it again from the homebrew menu.",
                                                    "OK", "", nullptr, nullptr);
                    }
                },
                nullptr);
        }
    }
}

} // namespace

#endif // ENABLE_UPDATER

void LighthouseMenu::AddMenuUpdates() {
#ifdef ENABLE_UPDATER
    AddSidebarEntry("Settings", "Updates", 1);
    WidgetPath path = { "Settings", "Updates", SECTION_COLUMN_1 };

    AddWidget(path, "Docklight Updates", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "UpdaterStatus", WIDGET_CUSTOM).RaceDisable(false).CustomFunction(DrawStatus);
    AddWidget(path, "UpdaterActions", WIDGET_CUSTOM).RaceDisable(false).CustomFunction(DrawActions);
    AddWidget(path, "UpdaterNotes", WIDGET_CUSTOM).RaceDisable(false).CustomFunction(DrawReleaseNotes);

    AddWidget(path, "Options", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Check at Startup", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_SETTING("Updater.CheckOnBoot"))
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(false).Tooltip(
            "Check for the newest release when Docklight starts and show a notification if "
            "one is newer than this build."));
    AddWidget(path, "UpdaterRepo", WIDGET_CUSTOM).RaceDisable(false).CustomFunction([](WidgetInfo& info) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Updates come from github.com/%s", gDocklightUpdateRepo);
    });
#endif // ENABLE_UPDATER
}

} // namespace LighthouseGui
