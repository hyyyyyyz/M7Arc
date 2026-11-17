#include "app/Translation.h"
#include "engine/ArchiveEngine.h"
#include "platform/services/MacServices.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
#include <QTextStream>
#include <QTimer>

class M7ArcApplication final : public QApplication {
  public:
    using QApplication::QApplication;
    m7arc::ui::MainWindow* window = nullptr;
    QStringList pending;
    bool event(QEvent* event) override {
        if (event->type() == QEvent::FileOpen) {
            auto* open = static_cast<QFileOpenEvent*>(event);
            if (window)
                window->openPath(open->file());
            else
                pending << open->file();
            return true;
        }
        return QApplication::event(event);
    }
};
int main(int argc, char** argv) {
    M7ArcApplication app(argc, argv);
    QCoreApplication::setApplicationName("M7Arc");
    QCoreApplication::setOrganizationName("M7Arc");
    QCoreApplication::setApplicationVersion(QStringLiteral(M7ARC_VERSION));
    QApplication::setQuitOnLastWindowClosed(true);
    if (app.arguments().contains("--smoke-test")) {
        try {
            m7arc::ArchiveEngine engine;
            auto formats = engine.formats();
            if (formats.empty())
                return 1;
            // Build the real main window so that startup failures in its construction show up here.
            m7arc::loadTranslations(app);
            m7arc::ui::MainWindow window;
            app.processEvents();
            QTextStream(stdout) << "M7Arc " << QCoreApplication::applicationVersion() << "; Qt " << qVersion()
                                << "; 7-Zip " << QString::fromStdString(engine.version())
                                << "; formats=" << formats.size() << "; window=" << window.windowTitle() << "\n";
            return 0;
        } catch (const std::exception& e) {
            QTextStream(stderr) << e.what() << "\n";
            return 1;
        }
    }
    m7arc::loadTranslations(app);
    m7arc::ui::MainWindow window;
    app.window = &window;
    window.show();
    m7arc::platform::installArchiveServices(
        [&window](const QString& action, const QStringList& paths) { window.runService(action, paths); });
    QStringList paths = app.pending;
    for (int i = 1; i < app.arguments().size(); ++i)
        if (!app.arguments()[i].startsWith('-') && QFileInfo::exists(app.arguments()[i]))
            paths << app.arguments()[i];
    if (!paths.isEmpty()) {
        int service = app.arguments().indexOf("--service");
        if (service >= 0 && service + 1 < app.arguments().size())
            QTimer::singleShot(0, &window, [&window, paths, action = app.arguments()[service + 1]] {
                window.runService(action, paths);
            });
        else
            QTimer::singleShot(0, &window, [&window, path = paths.first()] { window.openPath(path); });
    }
    return app.exec();
}
