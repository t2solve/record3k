#pragma once

#include <QLabel>
#include <QPoint>
#include <QMouseEvent>

// QLabel that reports mouse press/move/release positions in label-local pixel
// coordinates. TaggingWindow displays frames at native resolution (no KeepAspectRatio
// scaling), so these coordinates are already image pixel coordinates -- no scale
// correction needed at the call site.
class ClickableImageLabel : public QLabel {
    Q_OBJECT
public:
    explicit ClickableImageLabel(QWidget* parent = nullptr) : QLabel(parent) {}

signals:
    void pressed(QPoint pos);
    void moved(QPoint pos);
    void released(QPoint pos);

protected:
    void mousePressEvent(QMouseEvent* event) override {
        emit pressed(event->pos());
        QLabel::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        emit moved(event->pos());
        QLabel::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        emit released(event->pos());
        QLabel::mouseReleaseEvent(event);
    }
};
