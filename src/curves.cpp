#include "curves.h"

#include <QStringList>
#include <algorithm>
#include <cmath>

namespace Curves {

double shape(double magnitude, double inner, double outer, double anti, double curve)
{
    magnitude = std::clamp(magnitude, 0.0, 1.0);
    if (magnitude <= inner)
        return 0.0;
    // An outer edge at or inside the deadzone would divide by zero; treat it
    // as a switch that is fully on as soon as the deadzone is left.
    const double span = outer - inner;
    const double scaled = span <= 1e-6 ? 1.0 : std::clamp((magnitude - inner) / span, 0.0, 1.0);
    const double curved = std::pow(scaled, std::max(curve, 0.05));
    return anti + (1.0 - anti) * curved;
}

QPointF stick(QPointF raw, const StickSettings &s)
{
    const double x = std::clamp(raw.x(), -1.0, 1.0);
    const double y = std::clamp(raw.y(), -1.0, 1.0);

    if (!s.radial) {
        auto axis = [&](double v) { return std::copysign(shape(std::abs(v), s.inner, s.outer, s.anti, s.curve), v); };
        return {axis(x), axis(y)};
    }

    const double magnitude = std::hypot(x, y);
    if (magnitude <= s.inner || magnitude <= 1e-9)
        return {0.0, 0.0};
    // Square-gated sticks can report past the unit circle in the corners;
    // the direction is kept and the length capped at full travel.
    const double out = shape(std::min(magnitude, 1.0), s.inner, s.outer, s.anti, s.curve);
    return {std::clamp(x / magnitude * out, -1.0, 1.0), std::clamp(y / magnitude * out, -1.0, 1.0)};
}

double trigger(double raw, const TriggerSettings &s)
{
    return shape(raw, s.inner, s.outer, 0.0, 1.0);
}

}

namespace {

QJsonObject stickJson(const StickSettings &s)
{
    return {{"inner", s.inner}, {"outer", s.outer}, {"anti", s.anti}, {"curve", s.curve},
            {"mode", s.radial ? "radial" : "axial"}};
}

StickSettings stickFrom(const QJsonObject &o)
{
    StickSettings s;
    s.inner = std::clamp(o.value("inner").toDouble(s.inner), 0.0, 0.9);
    s.outer = std::clamp(o.value("outer").toDouble(s.outer), 0.1, 1.0);
    s.anti = std::clamp(o.value("anti").toDouble(s.anti), 0.0, 0.9);
    s.curve = std::clamp(o.value("curve").toDouble(s.curve), 0.2, 4.0);
    s.radial = o.value("mode").toString("radial") != "axial";
    return s;
}

QJsonObject triggerJson(const TriggerSettings &t)
{
    return {{"inner", t.inner}, {"outer", t.outer}};
}

TriggerSettings triggerFrom(const QJsonObject &o)
{
    TriggerSettings t;
    t.inner = std::clamp(o.value("inner").toDouble(t.inner), 0.0, 0.9);
    t.outer = std::clamp(o.value("outer").toDouble(t.outer), 0.1, 1.0);
    return t;
}

}

QJsonObject Profile::toJson() const
{
    return {{"left", stickJson(left)}, {"right", stickJson(right)},
            {"l2", triggerJson(l2)}, {"r2", triggerJson(r2)}};
}

Profile Profile::fromJson(const QJsonObject &o)
{
    Profile p;
    p.left = stickFrom(o.value("left").toObject());
    p.right = stickFrom(o.value("right").toObject());
    p.l2 = triggerFrom(o.value("l2").toObject());
    p.r2 = triggerFrom(o.value("r2").toObject());
    return p;
}

double Profile::value(const QString &key) const
{
    const QString group = key.section('.', 0, 0), field = key.section('.', 1);
    if (group == "left" || group == "right") {
        const StickSettings &s = group == "left" ? left : right;
        if (field == "inner") return s.inner;
        if (field == "outer") return s.outer;
        if (field == "anti") return s.anti;
        if (field == "curve") return s.curve;
        if (field == "radial") return s.radial ? 1.0 : 0.0;
    } else if (group == "l2" || group == "r2") {
        const TriggerSettings &t = group == "l2" ? l2 : r2;
        if (field == "inner") return t.inner;
        if (field == "outer") return t.outer;
    }
    return 0.0;
}

bool Profile::setValue(const QString &key, double v)
{
    const QString group = key.section('.', 0, 0), field = key.section('.', 1);
    if (group == "left" || group == "right") {
        StickSettings &s = group == "left" ? left : right;
        if (field == "inner") s.inner = std::clamp(v, 0.0, 0.9);
        else if (field == "outer") s.outer = std::clamp(v, 0.1, 1.0);
        else if (field == "anti") s.anti = std::clamp(v, 0.0, 0.9);
        else if (field == "curve") s.curve = std::clamp(v, 0.2, 4.0);
        else if (field == "radial") s.radial = v >= 0.5;
        else return false;
        return true;
    }
    if (group == "l2" || group == "r2") {
        TriggerSettings &t = group == "l2" ? l2 : r2;
        if (field == "inner") t.inner = std::clamp(v, 0.0, 0.9);
        else if (field == "outer") t.outer = std::clamp(v, 0.1, 1.0);
        else return false;
        return true;
    }
    return false;
}
