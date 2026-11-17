#pragma once
#include <QHash>
#include <QString>
#include <QVector>
#include <functional>

namespace m7arc::app {
// Maps command identifiers ("extract", "hash:SHA-256", "sort-1") to handlers. Menu items, toolbar
// buttons and shortcuts only name the command; what it does is registered once, by whoever owns
// the behavior.
class CommandRegistry {
  public:
    using Handler = std::function<void()>;
    using ArgumentHandler = std::function<void(const QString& argument)>;
    using Guard = std::function<bool(const QString& id)>;
    // Exact identifier. Registering an identifier again replaces its handler.
    void add(const QString& id, Handler handler);
    // Every identifier that starts with `prefix`; the handler receives the rest. An exact
    // registration wins over a prefix, and a longer prefix over a shorter one.
    void addPrefix(const QString& prefix, ArgumentHandler handler);
    // Consulted before each dispatch of a known command; returning false drops the command.
    void setGuard(Guard guard) {
        guard_ = std::move(guard);
    }
    bool contains(const QString& id) const;
    // Returns false for an unknown identifier. A command stopped by the guard counts as handled.
    bool dispatch(const QString& id) const;

  private:
    struct PrefixEntry {
        QString prefix;
        ArgumentHandler handler;
    };
    QHash<QString, Handler> exact_;
    QVector<PrefixEntry> prefixes_;
    Guard guard_;
};
} // namespace m7arc::app
