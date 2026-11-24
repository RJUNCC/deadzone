#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QSocketNotifier>
#include <QString>
#include <atomic>
#include <vector>

#include "curves.h"

#include <linux/input.h>

// What the controller is doing right now, before and after the profile.
struct LiveState {
    QPointF leftRaw, leftOut, rightRaw, rightOut;
    double l2Raw = 0, l2Out = 0, r2Raw = 0, r2Out = 0;
    quint32 buttons = 0;   // One bit per LiveButton.
    double rate = 0;       // Reports a second, measured while the controller is moving.
};

// Bits in LiveState::buttons, named for a PlayStation pad (cross is Xbox A).
enum LiveButton {
    Cross = 0, Circle, Square, Triangle, L1, R1, Share, Options, PS, L3, R3,
    DpadUp, DpadDown, DpadLeft, DpadRight
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

    // All are safe to call from any thread.
    void setProfile(const Profile &profile);
    LiveState live() const;
    // Holding PS and pressing left or right on the d-pad steps through
    // profiles instead of reaching the game.
    void setComboEnabled(bool enabled) { m_comboEnabled = enabled; }
    // Which LiveButton is jump, for spotting flips.
    void setJumpButton(int bit) { m_jumpBit = bit; }
    // 0 silences rumble from games; 1 passes it through unchanged.
    void setRumble(double strength) { m_rumble = strength; }

public slots:
    void start();
    void stop();

signals:
    void profileStep(int step);
    // A second jump press soon after a first: a flip or a double jump.
    // gap is the time between the two presses; x and y are the left stick
    // as the game received it at the second press.
    void flip(double gapMs, double x, double y);
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
    void handleKey(int code, int value);
    void handleHat(int code, int value);
    void setButton(int bit, bool down);
    void noteReport(const timeval &time);
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

    std::atomic<bool> m_comboEnabled{true};
    std::atomic<double> m_rumble{1.0};
    std::atomic<int> m_jumpBit{0};
    double m_firstJump = -1;
    double m_lastEventTime = 0;
    bool m_modeHeld = false, m_comboUsed = false;
    quint32 m_buttons = 0;
    std::vector<double> m_reports;   // Recent report times, for the rate.
    double m_rate = 0;

    mutable QMutex m_mutex;
    Profile m_profile;
    LiveState m_live;
};
