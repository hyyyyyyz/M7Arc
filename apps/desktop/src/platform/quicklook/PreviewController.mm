#import <AppKit/AppKit.h>
#import <QuickLookUI/QuickLookUI.h>
#include "engine/ArchiveEngine.h"
#include <algorithm>

@interface M7ArcPreviewController : NSViewController <QLPreviewingController, NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, strong) NSTextField *titleLabel;
@property(nonatomic, strong) NSTextField *summaryLabel;
@property(nonatomic, strong) NSTableView *table;
@property(nonatomic, strong) NSArray<NSDictionary *> *entries;
@end

static NSString *Text(const std::string &value) {
    return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding] ?: @"";
}

static NSString *Localized(NSString *english, NSString *chinese) {
    return [NSLocale.preferredLanguages.firstObject hasPrefix:@"zh"] ? chinese : english;
}

@implementation M7ArcPreviewController
- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 780, 520)];
    self.preferredContentSize = NSMakeSize(780, 520);
    self.titleLabel = [NSTextField labelWithString:@"M7Arc"];
    self.titleLabel.font = [NSFont systemFontOfSize:19 weight:NSFontWeightSemibold];
    self.summaryLabel = [NSTextField labelWithString:Localized(@"Reading archive…", @"正在读取归档…")];
    self.summaryLabel.textColor = NSColor.secondaryLabelColor;
    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    scroll.borderType = NSBezelBorder;
    self.table = [[NSTableView alloc] init];
    self.table.usesAlternatingRowBackgroundColors = YES;
    self.table.rowHeight = 24;
    self.table.dataSource = self;
    self.table.delegate = self;
    for (NSArray *description in @[@[@"path", Localized(@"Name", @"名称"), @430],
                                   @[@"size", Localized(@"Size", @"大小"), @100],
                                   @[@"packed", Localized(@"Packed", @"压缩后"), @100],
                                   @[@"method", Localized(@"Method", @"方法"), @180]]) {
        NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:description[0]];
        column.title = description[1];
        column.width = [description[2] doubleValue];
        [self.table addTableColumn:column];
    }
    scroll.documentView = self.table;
    for (NSView *view in @[self.titleLabel, self.summaryLabel, scroll]) {
        view.translatesAutoresizingMaskIntoConstraints = NO;
        [self.view addSubview:view];
    }
    [NSLayoutConstraint activateConstraints:@[
        [self.titleLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:20],
        [self.titleLabel.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-20],
        [self.titleLabel.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:18],
        [self.summaryLabel.leadingAnchor constraintEqualToAnchor:self.titleLabel.leadingAnchor],
        [self.summaryLabel.trailingAnchor constraintEqualToAnchor:self.titleLabel.trailingAnchor],
        [self.summaryLabel.topAnchor constraintEqualToAnchor:self.titleLabel.bottomAnchor constant:8],
        [scroll.topAnchor constraintEqualToAnchor:self.summaryLabel.bottomAnchor constant:16],
        [scroll.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:16],
        [scroll.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-16],
        [scroll.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-16]
    ]];
}

- (void)preparePreviewOfFileAtURL:(NSURL *)url completionHandler:(void (^)(NSError *))handler {
    (void)self.view;
    self.titleLabel.stringValue = url.lastPathComponent;
    static dispatch_queue_t queue;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ queue = dispatch_queue_create("org.m7arc.preview", DISPATCH_QUEUE_SERIAL); });
    dispatch_async(queue, ^{
        @autoreleasepool {
            NSArray<NSDictionary *> *rows = @[];
            NSString *summary = @"";
            try {
                m7arc::EngineOptions engineOptions;
                engineOptions.loadUserModules = false;
                m7arc::ArchiveEngine engine(engineOptions);
                m7arc::Callbacks callbacks;
                callbacks.password = []() -> std::optional<std::string> { return std::nullopt; };
                auto snapshot = engine.list(std::string(url.fileSystemRepresentation), {}, callbacks);
                NSMutableArray<NSDictionary *> *items = [NSMutableArray array];
                const auto displayed = std::min<std::size_t>(snapshot.entries.size(), 5000);
                for (std::size_t index = 0; index < displayed; ++index) {
                    const auto &entry = snapshot.entries[index];
                    [items addObject:@{@"path": Text(entry.path),
                        @"size": entry.isDirectory ? @"—" : [NSByteCountFormatter stringFromByteCount:entry.size countStyle:NSByteCountFormatterCountStyleFile],
                        @"packed": entry.isDirectory ? @"—" : [NSByteCountFormatter stringFromByteCount:entry.packedSize countStyle:NSByteCountFormatterCountStyleFile],
                        @"method": Text(entry.method)}];
                }
                rows = items;
                summary = [NSString stringWithFormat:Localized(@"%@ · %lu items%@", @"%@ · %lu 项%@"), Text(snapshot.format),
                    (unsigned long)snapshot.entries.size(), snapshot.entries.size() > displayed
                        ? Localized(@" · First 5,000 shown", @" · 显示前 5,000 项") : @""];
            } catch (const std::exception &error) {
                summary = [Localized(@"Open in M7Arc: ", @"请使用 M7Arc 打开：") stringByAppendingString:Text(error.what())];
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                self.entries = rows;
                self.summaryLabel.stringValue = summary;
                [self.table reloadData];
                handler(nil);
            });
        }
    });
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    (void)tableView;
    return self.entries.count;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    (void)tableView;
    NSTextField *label = [NSTextField labelWithString:self.entries[row][column.identifier] ?: @""];
    label.lineBreakMode = NSLineBreakByTruncatingMiddle;
    label.toolTip = label.stringValue;
    return label;
}
@end
