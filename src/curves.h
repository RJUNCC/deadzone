#pragma once

#include <QJsonObject>
#include <QPointF>

// How a stick's position becomes the position games see. All values are
// fractions of full travel, so they mean the same thing on every controller.
struct StickSettings {
    double inner = 0.06;   // Below this the stick reads as centered.
    double outer = 0.95;   // At or past this the stick reads as fully pushed.
    double anti = 0.0;     // The smallest output once past the deadzone, for games with their own.
    double curve = 1.0;    // Exponent: 1 is linear, above 1 is finer near the center.
    bool radial = true;    // Radial treats the stick as a circle; axial treats each axis alone.

    bool operator==(const StickSettings &) const = default;
};

struct TriggerSettings {
    double inner = 0.02;
    double outer = 0.98;

    bool operator==(const TriggerSettings &) const = default;
};

struct Profile {
    StickSettings left, right;
    TriggerSettings l2, r2;

    bool operator==(const Profile &) const = default;

    QJsonObject toJson() const;
    static Profile fromJson(const QJsonObject &object);

    // Settings addressed by a dotted key ("left.inner", "r2.outer") so QML
    // can bind one slider per key without a property for each.
    double value(const QString &key) const;
    bool setValue(const QString &key, double value);
};

namespace Curves {
// Maps a magnitude in 0..1 through the deadzones, curve, and anti-deadzone.
double shape(double magnitude, double inner, double outer, double anti, double curve);
// x and y run from -1 to 1; the result stays in that square.
QPointF stick(QPointF raw, const StickSettings &settings);
// t runs from 0 to 1.
double trigger(double raw, const TriggerSettings &settings);
}
