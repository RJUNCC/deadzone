#pragma once

#include <QClipboard>
#include <QGuiApplication>
#include <QObject>

// QML has no clipboard of its own; the window uses this one to copy the
// overlay address and profile codes, and to paste codes back.
class Clipboard : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void setText(const QString &text) { QGuiApplication::clipboard()->setText(text); }
    Q_INVOKABLE QString text() const { return QGuiApplication::clipboard()->text(); }
};
