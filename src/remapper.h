#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QSocketNotifier>
#include <QString>
#include <vector>

#include "curves.h"

#include <linux/input.h>

// What the controller is doing right now, before and after the profile.
struct LiveState {
    QPointF leftRaw, leftOut, rightRaw, rightOut;
    double l2Raw = 0, l2Out = 0, r2Raw = 0, r2Out = 0;
};

// Owns one physical controller. While active it grabs the controller so
// nothing else reads it, and replays it as a virtual Xbox 360 pad with the
// profile applied; rumble from games travels back the other way. While
// inactive it only reads, so the window can still preview the profile.
// Lives on its own thread so the UI never delays input.
class Remapper : public QObject {
    Q_OBJECT

public:
    Remapper(const QString &path, const QString &driver, bool active, const Profile &profile);
    ~Remapper() override;

    // Both are safe to call from any thread.
    void setProfile(const Profile &profile);
    LiveState live() const;

public slots:
    void start();
    void stop();

signals:
    void started(bool active);
    void failed(const QString &message);
    void disconnected();

private:
    enum Axis { LX, LY, RX, RY, L2, R2, AxisCount };

    void readSource();
    void readVirtual();
    bool createVirtual(bool rumble);
    void resync();
    double normalize(int axis, int value) const;
    void flush();
    void emitEvent(int type, int code, int value);
    void uploadEffect(int requestId);
    void eraseEffect(int requestId);

    QString m_path, m_driver;
    bool m_active = false;
    bool m_swapFaces = false;
    bool m_dropped = false;
    int m_source = -1, m_virtual = -1;
    QSocketNotifier *m_sourceNotifier = nullptr, *m_virtualNotifier = nullptr;

    // Range of each source axis, so any controller normalizes the same way.
    struct Range { int min = 0, max = 255; bool present = false; };
    Range m_ranges[AxisCount];
    int m_codes[AxisCount] = {};
    double m_raw[AxisCount] = {};
    int m_sent[AxisCount] = {};
    std::vector<input_event> m_pending;
    QHash<int, int> m_effects;   // Virtual effect id to the id on the real controller.

    mutable QMutex m_mutex;
    Profile m_profile;
    LiveState m_live;
};
