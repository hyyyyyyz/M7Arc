#import <Automator/Automator.h>
#include <iostream>

int main(int argc, char **argv) {
    @autoreleasepool {
        for (int index = 1; index < argc; ++index) {
            NSError *error = nil;
            AMWorkflow *workflow = [[AMWorkflow alloc] initWithContentsOfURL:
                [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[index]]] error:&error];
            if (!workflow || error || workflow.actions.count != 1) {
                std::cerr << (error ? error.localizedDescription.UTF8String : "The workflow action is missing.") << '\n';
                return 1;
            }
        }
        std::cout << "Automator loaded all three Finder Quick Actions.\n";
        return 0;
    }
}
