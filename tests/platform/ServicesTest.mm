#include "platform/services/MacServices.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#import <AppKit/AppKit.h>

@interface NSObject (M7ArcServicesTest)
- (void)performArchiveService:(NSPasteboard *)pasteboard userData:(NSString *)action error:(NSString **)error;
@end

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    @autoreleasepool {
        QTemporaryDir directory;
        if (!directory.isValid()) return 1;
        const QStringList expected{directory.filePath(QStringLiteral("中文 file.txt")), directory.filePath(QStringLiteral("second.txt"))};
        NSMutableArray<NSURL *> *urls = [NSMutableArray array];
        for (const QString &path : expected) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) return 1;
            [urls addObject:[NSURL fileURLWithPath:path.toNSString()]];
        }
        int received = 0;
        m7arc::platform::installArchiveServices([&](const QString &action, const QStringList &paths) {
            if (action == QStringLiteral("add") && paths == expected) ++received;
        });
        NSPasteboard *pasteboard = [NSPasteboard pasteboardWithUniqueName];
        [pasteboard writeObjects:urls];
        NSString *error = nil;
        [[NSApplication sharedApplication].servicesProvider performArchiveService:pasteboard userData:@"add" error:&error];
        QCoreApplication::processEvents();
        [pasteboard releaseGlobally];
        if (error || received != 1) return 1;
        std::cout << "Native Services pasteboard: both selected paths reached the Qt callback.\n";
        return 0;
    }
}
