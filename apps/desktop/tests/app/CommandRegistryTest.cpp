#include "app/CommandRegistry.h"
#include <iostream>
#include <stdexcept>

using m7arc::app::CommandRegistry;

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main() {
    try {
        CommandRegistry commands;
        QStringList calls;
        commands.add("copy", [&] { calls << "copy"; });
        commands.add("view-file", [&] { calls << "view-file"; });
        commands.addPrefix("view-", [&](const QString& mode) { calls << "mode:" + mode; });
        commands.addPrefix("favorite:", [&](const QString& slot) { calls << "go:" + slot; });
        commands.addPrefix("favorite-set:", [&](const QString& slot) { calls << "set:" + slot; });
        commands.addPrefix("hash:", [&](const QString& name) { calls << "hash:" + name; });
        commands.addPrefix("hash:", [&](const QString& name) { calls << "digest:" + name; });

        require(commands.dispatch("copy") && commands.dispatch("view-file") && commands.dispatch("view-large"),
                "known commands must dispatch");
        require(commands.dispatch("favorite:3") && commands.dispatch("favorite-set:3") &&
                    commands.dispatch("hash:CRC32"),
                "prefix commands must dispatch");
        require(calls == QStringList({"copy", "view-file", "mode:large", "go:3", "set:3", "digest:CRC32"}),
                "Exact names win over prefixes, prefixes pass their argument, re-registering replaces");
        require(!commands.dispatch("unknown") && !commands.contains("unknown") && commands.contains("view-x"),
                "Unknown identifiers are reported");

        calls.clear();
        QStringList asked;
        commands.setGuard([&](const QString& id) {
            asked << id;
            return id != "copy";
        });
        require(commands.dispatch("copy") && commands.dispatch("view-small"), "Guarded command is still handled");
        require(calls == QStringList({"mode:small"}) && asked == QStringList({"copy", "view-small"}),
                "A refused guard must stop the handler and nothing else");
        require(!commands.dispatch("nothing") && asked.size() == 2, "The guard is not asked about unknown ids");
        std::cout << "Command registry dispatches exact, prefix and guarded commands\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
