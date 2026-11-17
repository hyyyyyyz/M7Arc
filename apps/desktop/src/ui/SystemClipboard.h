#pragma once
#include "app/ClipboardPort.h"
namespace m7arc::ui {
// The application's file clipboard on top of the system clipboard: local file URLs, plus a marker
// format when the files were cut.
class SystemClipboard final : public app::ClipboardPort {
  public:
    void setFiles(const QStringList& paths, bool cut) override;
    QStringList files() const override;
    bool isCut() const override;
    void clear() override;
};
} // namespace m7arc::ui
