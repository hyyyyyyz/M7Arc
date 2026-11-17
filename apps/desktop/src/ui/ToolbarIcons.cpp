#include "ToolbarIcons.h"
#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <cmath>

namespace m7arc::ui {
namespace {
class OutlineIcon final : public QIconEngine {
  public:
    explicit OutlineIcon(ToolbarGlyph glyph) : glyph_(glyph) {}
    QIconEngine* clone() const override {
        return new OutlineIcon(glyph_);
    }
    QString key() const override {
        return "M7ArcOutline";
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1);
    }
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QPixmap image(QSize(std::ceil(size.width() * scale), std::ceil(size.height() * scale)));
        image.setDevicePixelRatio(scale);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        paint(&painter, QRect(QPoint{}, size), mode, state);
        return image;
    }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        const auto palette = QGuiApplication::palette();
        const auto color = mode == QIcon::Disabled   ? palette.color(QPalette::Disabled, QPalette::WindowText)
                           : mode == QIcon::Selected ? palette.color(QPalette::HighlightedText)
                                                     : palette.color(QPalette::WindowText);
        const qreal side = qMin(rect.width(), rect.height());
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x() + (rect.width() - side) / 2, rect.y() + (rect.height() - side) / 2);
        painter->scale(side / 24, side / 24);
        painter->setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        auto line = [painter](qreal x1, qreal y1, qreal x2, qreal y2) {
            painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));
        };
        switch (glyph_) {
        case ToolbarGlyph::Add:
            painter->drawRoundedRect(QRectF(3.5, 3.5, 17, 17), 3.8, 3.8);
            line(7.5, 12, 16.5, 12);
            line(12, 7.5, 12, 16.5);
            break;
        case ToolbarGlyph::Extract: {
            QPainterPath tray;
            tray.moveTo(4, 14.5);
            tray.lineTo(4, 18.5);
            tray.quadTo(4, 20.5, 6, 20.5);
            tray.lineTo(18, 20.5);
            tray.quadTo(20, 20.5, 20, 18.5);
            tray.lineTo(20, 14.5);
            painter->drawPath(tray);
            line(12, 3.5, 12, 15.5);
            line(7.5, 11, 12, 15.5);
            line(12, 15.5, 16.5, 11);
            break;
        }
        case ToolbarGlyph::Test: {
            QPainterPath shield;
            shield.moveTo(12, 2.8);
            shield.lineTo(20, 6);
            shield.lineTo(20, 11);
            shield.cubicTo(20, 16, 16.5, 19.3, 12, 21.2);
            shield.cubicTo(7.5, 19.3, 4, 16, 4, 11);
            shield.lineTo(4, 6);
            shield.closeSubpath();
            painter->drawPath(shield);
            line(8, 11.8, 10.7, 14.5);
            line(10.7, 14.5, 16.2, 9);
            break;
        }
        case ToolbarGlyph::Copy: {
            painter->drawRoundedRect(QRectF(8, 8, 12, 13), 2.2, 2.2);
            QPainterPath back;
            back.moveTo(4, 16);
            back.lineTo(4, 5);
            back.quadTo(4, 3, 6, 3);
            back.lineTo(15, 3);
            back.quadTo(17, 3, 17, 5);
            painter->drawPath(back);
            break;
        }
        case ToolbarGlyph::Move:
            line(3.5, 12, 20, 12);
            line(14.3, 6.3, 20, 12);
            line(20, 12, 14.3, 17.7);
            break;
        case ToolbarGlyph::Delete: {
            line(4, 6.8, 20, 6.8);
            QPainterPath bin;
            bin.moveTo(6.2, 6.8);
            bin.lineTo(7.1, 19);
            bin.quadTo(7.2, 21, 9.2, 21);
            bin.lineTo(14.8, 21);
            bin.quadTo(16.8, 21, 16.9, 19);
            bin.lineTo(17.8, 6.8);
            painter->drawPath(bin);
            painter->drawRoundedRect(QRectF(9, 3, 6, 3.8), 1.5, 1.5);
            line(10, 10.5, 10.4, 17.2);
            line(14, 10.5, 13.6, 17.2);
            break;
        }
        case ToolbarGlyph::Info:
            painter->drawEllipse(QRectF(3, 3, 18, 18));
            line(12, 10.7, 12, 17);
            painter->setBrush(color);
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(QPointF(12, 7), 1.05, 1.05);
            break;
        case ToolbarGlyph::Formats:
            painter->drawRoundedRect(QRectF(3.2, 3.5, 17.6, 4.5), 1.2, 1.2);
            painter->drawRoundedRect(QRectF(4.5, 8, 15, 12.5), 1.8, 1.8);
            line(9.2, 12, 14.8, 12);
            break;
        case ToolbarGlyph::Split:
            painter->drawRoundedRect(QRectF(3, 4, 18, 16), 3, 3);
            line(12, 4, 12, 20);
            break;
        }
        painter->restore();
    }

  private:
    ToolbarGlyph glyph_;
};
} // namespace
QIcon toolbarIcon(ToolbarGlyph glyph) {
    return QIcon(new OutlineIcon(glyph));
}
} // namespace m7arc::ui
