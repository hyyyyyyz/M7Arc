#include "CommandRegistry.h"

namespace m7arc::app {
void CommandRegistry::add(const QString& id, Handler handler) {
    exact_.insert(id, std::move(handler));
}
void CommandRegistry::addPrefix(const QString& prefix, ArgumentHandler handler) {
    for (auto& entry : prefixes_)
        if (entry.prefix == prefix) {
            entry.handler = std::move(handler);
            return;
        }
    prefixes_.append({prefix, std::move(handler)});
}
bool CommandRegistry::contains(const QString& id) const {
    if (exact_.contains(id))
        return true;
    for (const auto& entry : prefixes_)
        if (id.startsWith(entry.prefix))
            return true;
    return false;
}
bool CommandRegistry::dispatch(const QString& id) const {
    const auto exact = exact_.constFind(id);
    const PrefixEntry* best = nullptr;
    if (exact == exact_.constEnd())
        for (const auto& entry : prefixes_)
            if (id.startsWith(entry.prefix) && (!best || entry.prefix.size() > best->prefix.size()))
                best = &entry;
    if (exact == exact_.constEnd() && !best)
        return false;
    if (guard_ && !guard_(id))
        return true;
    if (best)
        best->handler(id.mid(best->prefix.size()));
    else
        exact.value()();
    return true;
}
} // namespace m7arc::app
