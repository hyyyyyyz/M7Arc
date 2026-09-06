#pragma once

#include <QString>
#include <functional>

namespace m7arc::tools {

// Returns an empty string on success. Run from an operation worker for large archives.
QString createConsoleSfx(const QString &stub, const QString &archive, const QString &output,
                         const std::function<bool()> &cancelled = {});

struct SfxBundleOptions {
    QString displayName;
    bool embedPayload = true;
    std::function<bool()> cancelled;
};

// sourceApp is a deployed M7Arc.app containing M7ArcExtractor and its frameworks.
QString createGraphicalSfx(const QString &sourceApp, const QString &archive, const QString &outputApp,
                           const SfxBundleOptions &options = {});
// externalPayload is empty for an embedded bundle, or a 7z file copied as Payload.7z.
QString createSfxDmg(const QString &extractorApp, const QString &externalPayload, const QString &outputDmg,
                    const std::function<bool()> &cancelled = {});
QString createWindowsSfx(const QString &stub, const QString &archive, const QString &outputExe,
                        const std::function<bool()> &cancelled = {});

enum class WindowsInstallerLaunch { RunProgram, ExecuteFile };
struct WindowsInstallerOptions {
    QString title;
    QString beginPrompt;
    bool showProgress = true;
    WindowsInstallerLaunch launch = WindowsInstallerLaunch::RunProgram;
    QString runProgram = QStringLiteral("setup.exe");
    QString directoryPrefix = QStringLiteral(".\\");
    QString executeFile;
    QString executeParameters;
};
// Official 7zSD.sfx: unencrypted LZMA/LZMA2/Copy payloads. Configuration runs only on Windows.
QString createWindowsInstallerSfx(const QString &stub, const QString &archive, const QString &outputExe,
                                 const WindowsInstallerOptions &options,
                                 const std::function<bool()> &cancelled = {});

}
