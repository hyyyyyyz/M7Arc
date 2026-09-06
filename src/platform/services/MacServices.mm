#include "MacServices.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QEventLoop>
#include <QSet>
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface M7ArcServicesProvider : NSObject {
@public
    m7arc::platform::ArchiveServiceHandler callback;
}
- (void)performArchiveService:(NSPasteboard *)pasteboard userData:(NSString *)action error:(NSString **)error;
@end

@implementation M7ArcServicesProvider
- (void)performArchiveService:(NSPasteboard *)pasteboard userData:(NSString *)action error:(NSString **)error {
    if (![@[@"add", @"extract", @"test"] containsObject:action]) {
        if (error) *error = @"Unknown M7Arc service.";
        return;
    }
    NSArray<NSURL *> *urls = [pasteboard readObjectsForClasses:@[[NSURL class]]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    QStringList paths;
    for (NSURL *url in urls) {
        if (url.fileURL) paths.append(QString::fromNSString(url.path));
    }
    paths.removeDuplicates();
    if (paths.isEmpty()) {
        if (error) *error = @"Select files or folders in Finder before using this service.";
        return;
    }
    const auto handler = callback;
    const QString command = QString::fromNSString(action);
    QMetaObject::invokeMethod(QCoreApplication::instance(), [handler, command, paths] {
        handler(command, paths);
    }, Qt::QueuedConnection);
}
@end

namespace m7arc::platform {

void installArchiveServices(ArchiveServiceHandler handler) {
    static M7ArcServicesProvider *provider = [[M7ArcServicesProvider alloc] init];
    provider->callback = std::move(handler);
    [NSApplication sharedApplication].servicesProvider = provider;
    NSUpdateDynamicServices();
}

QString defaultAppForExtension(const QString &extension) {
    UTType *type = [UTType typeWithFilenameExtension:extension.toNSString()];
    if (!type) return {};
    NSURL *application = [[NSWorkspace sharedWorkspace] URLForApplicationToOpenContentType:type];
    return application ? QString::fromNSString([NSBundle bundleWithURL:application].bundleIdentifier) : QString();
}

QString setDefaultForExtensions(const QStringList &extensions) {
    QStringList errors;
    QSet<QString> handled;
    for (QString extension : extensions) {
        if (extension.startsWith('.')) extension.remove(0, 1);
        UTType *type = [UTType typeWithFilenameExtension:extension.toNSString()];
        if (!type) {
            errors.append(extension + QStringLiteral(": macOS cannot resolve this file type."));
            continue;
        }
        const QString identifier = QString::fromNSString(type.identifier);
        if (handled.contains(identifier)) continue;
        handled.insert(identifier);
        QEventLoop loop;
        QEventLoop *waiter = &loop;
        QString message;
        QString *result = &message;
        [[NSWorkspace sharedWorkspace] setDefaultApplicationAtURL:[NSBundle mainBundle].bundleURL
            toOpenContentType:type completionHandler:^(NSError *error) {
                const QString detail = error ? QString::fromNSString(error.localizedDescription) : QString();
                QMetaObject::invokeMethod(waiter, [waiter, result, detail] {
                    *result = detail;
                    waiter->quit();
                }, Qt::QueuedConnection);
            }];
        loop.exec();
        if (!message.isEmpty()) errors.append(extension + QStringLiteral(": ") + message);
    }
    return errors.join('\n');
}

QString installQuickActions() {
    NSFileManager *manager = [NSFileManager defaultManager];
    NSURL *source = [[NSBundle mainBundle].resourceURL URLByAppendingPathComponent:@"Quick Actions" isDirectory:YES];
    NSURL *destination = [[NSURL fileURLWithPath:NSHomeDirectory() isDirectory:YES]
        URLByAppendingPathComponent:@"Library/Services" isDirectory:YES];
    NSError *error = nil;
    if (![manager createDirectoryAtURL:destination withIntermediateDirectories:YES attributes:nil error:&error])
        return QString::fromNSString(error.localizedDescription);
    for (NSString *name in @[@"M7Arc Compress.workflow", @"M7Arc Extract.workflow", @"M7Arc Test.workflow"]) {
        NSURL *input = [source URLByAppendingPathComponent:name isDirectory:YES];
        NSURL *output = [destination URLByAppendingPathComponent:name isDirectory:YES];
        if (![manager fileExistsAtPath:input.path]) return QString::fromNSString([@"Missing bundled Quick Action: " stringByAppendingString:name]);
        if ([manager fileExistsAtPath:output.path] && ![manager removeItemAtURL:output error:&error])
            return QString::fromNSString(error.localizedDescription);
        if (![manager copyItemAtURL:input toURL:output error:&error])
            return QString::fromNSString(error.localizedDescription);
    }
    NSUpdateDynamicServices();
    return {};
}

}
