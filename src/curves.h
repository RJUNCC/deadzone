#pragma once

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

// How a stick's position becomes the position games see. All values are
// fractions of full travel, so they mean the same thing on every controller.
struct StickSettings {
    // The shape of the area that reads as centered.
    enum Shape { Circle = 0, Square = 1, Cross = 2, Custom = 3 };

    Shape shape = Circle;
    double inner = 0.06;   // Size of the deadzone; its width when split.
    double innerY = 0.06;  // Its height, used only when split.
    bool split = false;    // Separate horizontal and vertical sizes.
    QList<double> radii = QList<double>(8, 0.06);   // Custom shape: size toward right, down-right, down, ... clockwise.
    double outer = 0.95;   // At or past this the stick reads as fully pushed.
    double anti = 0.0;     // The smallest output once past the deadzone, for games with their own.
    double curve = 1.0;    // Exponent: 1 is linear, above 1 is finer near the center.
    bool splitCurve = false;   // Vertical gets its own exponent; the curve above becomes horizontal only.
    double curveY = 1.0;
    double square = 0.0;   // 0 keeps a round range; 1 lets diagonals reach full travel on both axes.
    bool custom = false;   // Use the drawn curve instead of the exponent.
    QList<QPointF> points = defaultPoints();   // The drawn curve, from (0,0)-ish to (1,1)-ish.

    static QList<QPointF> defaultPoints();
    bool operator==(const StickSettings &) const = default;
};

struct TriggerSettings {
    double inner = 0.02;
    double outer = 0.98;
    double anti = 0.0;

    bool operator==(const TriggerSettings &) const = default;
};

struct Profile {
    QString name = "Default";
    QString match;          // Switch to this profile while a process with this in its command line runs.
    QString preset;         // Which preset it started from, for its tips.
    QString color;          // Shown on the lightbar when switched to; empty picks one by position.
    StickSettings left, right;
    TriggerSettings l2, r2;

    bool operator==(const Profile &) const = default;

    QJsonObject toJson() const;
    static Profile fromJson(const QJsonObject &object);

    // Settings addressed by a dotted key ("left.inner", "r2.outer") so QML
    // can bind one control per key without a property for each.
    double value(const QString &key) const;
    bool setValue(const QString &key, double value);
};

namespace Curves {
// The exponent or drawn curve, for a value already past the deadzone (0..1).
double response(double scaled, const StickSettings &settings);
// How far the deadzone reaches along a unit direction (dx, dy).
double boundary(double dx, double dy, const StickSettings &settings);
// Sets the response to a named preset: linear, soft, precise, quick, scurve, dual.
void applyCurvePreset(StickSettings &settings, const QString &id);
QStringList curvePresets();
// Maps a magnitude in 0..1 through the deadzones, curve, and anti-deadzone.
double shape(double magnitude, double inner, double outer, double anti, double curve);
double shape(double magnitude, double inner, double outer, const StickSettings &settings);
// x and y run from -1 to 1; the result stays in that square.
QPointF stick(QPointF raw, const StickSettings &settings);
// t runs from 0 to 1.
double trigger(double raw, const TriggerSettings &settings);
}

namespace Codes {
// A profile as a short text code to paste anywhere, and back. Decoding
// returns false for anything that is not one.
QString encode(const Profile &profile);
bool decode(const QString &code, Profile &profile);
}

namespace Presets {
// Built-in starting points, each with tips for its game.
QStringList ids();
Profile make(const QString &id);
}
