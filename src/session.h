#pragma once

#include <QPointF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <deque>

#include "curves.h"

// Watches how a player actually uses the controller during real play and
// turns it into suggestions: how much the sticks drift at rest, how far
// they reach in each direction, where on the stick time is spent, and how
// deep the triggers go. It sees only the controller, never the game, so it
// says nothing about whether play got better.
class Session {
public:
    static constexpr int Grid = 24;   // Heatmap cells across.
    static constexpr int Bins = 36;   // Directions around the rim.

    void add(QPointF left, QPointF right, double l2, double r2);
    int samples() const { return m_samples; }

    // Suggestions for this profile, each a map with text, why, and the
    // settings ("key" to value) that apply it. Empty when nothing needs
    // changing or too little was seen.
    QVariantList suggestions(const Profile &profile) const;
    // What was measured, for the controller's history: drift at rest and
    // the weakest reach toward the rim, per stick; -1 where too little was seen.
    QVariantMap measurements() const;
    // Time spent in each cell of the stick's square, 0..1, row by row.
    QVariantList heatmap(bool left) const;

private:
    struct Stick {
        std::deque<double> recent;   // The last half second of travel, to spot rest.
        QList<double> rest;          // Travel while resting.
        double reach[Bins] = {};
        int cells[Grid * Grid] = {};
        int active = 0, fine = 0, full = 0, diagonalFull = 0;
    };
    struct Trigger {
        double peak = 0;
        QList<double> rest;
        int pressed = 0;
    };

    void addStick(Stick &stick, QPointF raw);
    void addTrigger(Trigger &trigger, double value);
    static void stickSuggestions(const Stick &stick, const StickSettings &settings, const QString &side,
                                 const QString &name, QVariantList &out);

    Stick m_left, m_right;
    Trigger m_l2, m_r2;
    int m_samples = 0;
};
