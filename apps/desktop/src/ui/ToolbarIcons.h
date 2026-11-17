#pragma once
#include <QIcon>

namespace m7arc::ui {
enum class ToolbarGlyph { Add, Extract, Test, Copy, Move, Delete, Info, Formats, Split };
QIcon toolbarIcon(ToolbarGlyph glyph);
} // namespace m7arc::ui
