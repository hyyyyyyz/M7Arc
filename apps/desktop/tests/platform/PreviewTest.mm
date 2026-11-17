#import <AppKit/AppKit.h>
#import <QuickLookUI/QuickLookUI.h>
#include <iostream>

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 2) return 1;
        [NSApplication sharedApplication];
        NSViewController<QLPreviewingController> *controller = [[NSClassFromString(@"M7ArcPreviewController") alloc] init];
        __block BOOL completed = NO;
        __block NSError *failure = nil;
        [controller preparePreviewOfFileAtURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]]
            completionHandler:^(NSError *error) { failure = error; completed = YES; }];
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:30];
        while (!completed && deadline.timeIntervalSinceNow > 0)
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        NSArray *entries = [controller valueForKey:@"entries"];
        if (!completed || failure || entries.count == 0) return 1;
        std::cout << "Quick Look controller: " << entries.count << " archive entries rendered.\n";
        return 0;
    }
}
