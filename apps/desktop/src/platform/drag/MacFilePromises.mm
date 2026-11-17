#include "MacFilePromises.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QWidget>
#include <atomic>
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace {

struct PromiseState {
    QPointer<QWidget> context;
    m7arc::platform::PromiseFulfill fulfill;
    std::atomic_bool cancelled{false};
    __strong NSOperationQueue *queue = [[NSOperationQueue alloc] init];
    PromiseState() { queue.maxConcurrentOperationCount = 1; queue.name = @"M7Arc File Promises"; }
};

NSString *const MemberMime = @"com.trolltech.anymime.application--x-m7arc-members";
NSMutableSet *DragSources() {
    static NSMutableSet *sources;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ sources = [NSMutableSet set]; });
    return sources;
}

}

@interface M7ArcFilePromise : NSFilePromiseProvider
@property(nonatomic, strong) NSData *memberPayload;
@end

@implementation M7ArcFilePromise
- (NSArray<NSPasteboardType> *)writableTypesForPasteboard:(NSPasteboard *)pasteboard {
    NSArray *types = [super writableTypesForPasteboard:pasteboard];
    return self.memberPayload ? [types arrayByAddingObject:MemberMime] : types;
}
- (id)pasteboardPropertyListForType:(NSPasteboardType)type {
    if ([type isEqualToString:MemberMime]) return self.memberPayload;
    return [super pasteboardPropertyListForType:type];
}
- (NSPasteboardWritingOptions)writingOptionsForType:(NSPasteboardType)type pasteboard:(NSPasteboard *)pasteboard {
    if ([type isEqualToString:MemberMime]) return 0;
    return [super writingOptionsForType:type pasteboard:pasteboard];
}
@end

@interface M7ArcPromiseDelegate : NSObject <NSFilePromiseProviderDelegate> {
@public
    std::shared_ptr<PromiseState> state;
    m7arc::platform::ArchivePromiseItem item;
}
@end

@implementation M7ArcPromiseDelegate
- (NSString *)filePromiseProvider:(NSFilePromiseProvider *)provider fileNameForType:(NSString *)type {
    (void)provider; (void)type;
    return item.name.toNSString();
}
- (NSOperationQueue *)operationQueueForFilePromiseProvider:(NSFilePromiseProvider *)provider {
    (void)provider;
    return state->queue;
}
- (void)filePromiseProvider:(NSFilePromiseProvider *)provider writePromiseToURL:(NSURL *)url
    completionHandler:(void (^)(NSError *))completion {
    (void)provider;
    const auto request = state;
    const auto member = item;
    const QString destination = QString::fromNSString(url.path);
    auto error = std::make_shared<QString>();
    dispatch_semaphore_t ready = dispatch_semaphore_create(0);
    QMetaObject::invokeMethod(QCoreApplication::instance(), [request, member, destination, error, ready] {
        if (request->cancelled || !request->context) {
            *error = QStringLiteral("The archive drag was cancelled.");
            dispatch_semaphore_signal(ready);
            return;
        }
        request->fulfill(member, destination, [request, error, ready](const QString &message) {
            *error = message;
            if (!message.isEmpty()) request->cancelled = true;
            dispatch_semaphore_signal(ready);
        });
    }, Qt::QueuedConnection);
    dispatch_semaphore_wait(ready, DISPATCH_TIME_FOREVER);
    NSError *failure = error->isEmpty() ? nil : [NSError errorWithDomain:@"org.m7arc.filepromise" code:1
        userInfo:@{NSLocalizedDescriptionKey: error->toNSString()}];
    completion(failure);
}
@end

@interface M7ArcDragSource : NSObject <NSDraggingSource> {
@public
    std::shared_ptr<PromiseState> state;
}
@end

@implementation M7ArcDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession *)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    (void)session;
    return context == NSDraggingContextWithinApplication ? NSDragOperationCopy | NSDragOperationMove : NSDragOperationCopy;
}
- (void)draggingSession:(NSDraggingSession *)session endedAtPoint:(NSPoint)point operation:(NSDragOperation)operation {
    (void)session; (void)point;
    if (operation == NSDragOperationNone) state->cancelled = true;
    [DragSources() removeObject:self];
}
@end

namespace m7arc::platform {

bool beginArchivePromiseDrag(QWidget *source, const QList<ArchivePromiseItem> &items,
                             const QByteArray &internalPayload, PromiseFulfill fulfill) {
    NSEvent *event = [NSApplication sharedApplication].currentEvent;
    if (items.isEmpty() || !source || !event.window ||
        (event.type != NSEventTypeLeftMouseDragged && event.type != NSEventTypeLeftMouseDown)) return false;
    NSView *view = event.window.contentView;
    const auto state = std::make_shared<PromiseState>();
    state->context = source;
    state->fulfill = std::move(fulfill);
    M7ArcDragSource *sender = [[M7ArcDragSource alloc] init];
    sender->state = state;
    NSMutableArray<NSDraggingItem *> *draggingItems = [NSMutableArray array];
    NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
    for (qsizetype index = 0; index < items.size(); ++index) {
        const auto &item = items[index];
        M7ArcPromiseDelegate *delegate = [[M7ArcPromiseDelegate alloc] init];
        delegate->state = state;
        delegate->item = item;
        UTType *type = item.directory ? UTTypeDirectory : [UTType typeWithFilenameExtension:QFileInfo(item.name).suffix().toNSString()];
        if (!type) type = UTTypeData;
        M7ArcFilePromise *provider = [[M7ArcFilePromise alloc] initWithFileType:type.identifier delegate:delegate];
        provider.userInfo = delegate;
        if (index == 0 && !internalPayload.isEmpty())
            provider.memberPayload = [NSData dataWithBytes:internalPayload.constData() length:internalPayload.size()];
        NSDraggingItem *drag = [[NSDraggingItem alloc] initWithPasteboardWriter:provider];
        NSImage *icon = [[NSWorkspace sharedWorkspace] iconForContentType:type];
        [drag setDraggingFrame:NSMakeRect(point.x + (index % 5) * 4, point.y - 32, 32, 32) contents:icon];
        [draggingItems addObject:drag];
    }
    [DragSources() addObject:sender];
    NSDraggingSession *session = [view beginDraggingSessionWithItems:draggingItems event:event source:sender];
    session.animatesToStartingPositionsOnCancelOrFail = YES;
    session.draggingFormation = NSDraggingFormationPile;
    return true;
}

OperationResult writeArchivePromise(ArchiveEngine &engine, const ArchivePromiseItem &item,
                                   const QString &destination, const Callbacks &callbacks) {
    OperationResult result;
    if (item.indices.isEmpty()) {
        result.errors.emplace_back("The drag does not contain archive member indices.");
        return result;
    }
    QString member = item.memberPath;
    member.replace('\\', '/');
    member = QDir::cleanPath(member);
    if (QDir::isAbsolutePath(member) || member == ".." || member.startsWith("../")) {
        result.errors.emplace_back("The archive member has no relative extraction path.");
        return result;
    }
    if (QFileInfo::exists(destination)) {
        result.errors.emplace_back("The drag destination already exists.");
        return result;
    }
    QTemporaryDir temporary(QFileInfo(destination).dir().filePath(QStringLiteral(".m7arc-drag-XXXXXX")));
    if (!temporary.isValid()) {
        result.errors.push_back(temporary.errorString().toStdString());
        return result;
    }
    std::vector<std::uint32_t> indices(item.indices.begin(), item.indices.end());
    ExtractOptions options;
    options.overwrite = Overwrite::Skip;
    result = engine.extract(item.archivePath.toStdString(), temporary.path().toStdString(), indices, options, callbacks);
    if (!result.ok()) return result;
    if (callbacks.cancelled && callbacks.cancelled()) {
        result.cancelled = true;
        return result;
    }
    const QString extracted = temporary.filePath(member);
    if (!QDir().rename(extracted, destination)) {
        result.errors.emplace_back("Cannot move the extracted member to Finder's destination.");
        return result;
    }
    result.outputPaths = {destination.toStdString()};
    return result;
}

}
