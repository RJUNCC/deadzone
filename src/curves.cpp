#include "curves.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

QList<QPointF> StickSettings::defaultPoints()
{
    return {{0.0, 0.0}, {0.25, 0.25}, {0.5, 0.5}, {0.75, 0.75}, {1.0, 1.0}};
}

namespace Curves {

double response(double scaled, const StickSettings &s)
{
    scaled = std::clamp(scaled, 0.0, 1.0);
    if (!s.custom || s.points.size() < 2)
        return std::pow(scaled, std::max(s.curve, 0.05));
    // Straight lines between the drawn points: what you draw is exactly what
    // games get, with no overshoot between points.
    const QList<QPointF> &p = s.points;
    if (scaled <= p.first().x())
        return std::clamp(p.first().y(), 0.0, 1.0);
    for (int i = 1; i < p.size(); ++i) {
        if (scaled <= p.at(i).x()) {
            const double span = p.at(i).x() - p.at(i - 1).x();
            const double t = span <= 1e-9 ? 1.0 : (scaled - p.at(i - 1).x()) / span;
            return std::clamp(p.at(i - 1).y() + t * (p.at(i).y() - p.at(i - 1).y()), 0.0, 1.0);
        }
    }
    return std::clamp(p.last().y(), 0.0, 1.0);
}

namespace {

double rescale(double magnitude, double inner, double outer)
{
    // An outer edge at or inside the deadzone would divide by zero; treat it
    // as a switch that is fully on as soon as the deadzone is left.
    const double span = outer - inner;
    return span <= 1e-6 ? 1.0 : std::clamp((magnitude - inner) / span, 0.0, 1.0);
}

}

double shape(double magnitude, double inner, double outer, double anti, double curve)
{
    magnitude = std::clamp(magnitude, 0.0, 1.0);
    if (magnitude <= inner)
        return 0.0;
    return anti + (1.0 - anti) * std::pow(rescale(magnitude, inner, outer), std::max(curve, 0.05));
}

double shape(double magnitude, double inner, double outer, const StickSettings &s)
{
    magnitude = std::clamp(magnitude, 0.0, 1.0);
    if (magnitude <= inner)
        return 0.0;
    return s.anti + (1.0 - s.anti) * response(rescale(magnitude, inner, outer), s);
}

QPointF stick(QPointF raw, const StickSettings &s)
{
    const double x = std::clamp(raw.x(), -1.0, 1.0);
    const double y = std::clamp(raw.y(), -1.0, 1.0);

    QPointF out;
    if (s.shape == StickSettings::Cross) {
        // Each axis on its own: a straight push stays exactly straight.
        auto axis = [&](double v) { return std::copysign(shape(std::abs(v), s.inner, s.outer, s), v); };
        out = {axis(x), axis(y)};
    } else {
        const double magnitude = std::hypot(x, y);
        if (magnitude <= 1e-9)
            return {0.0, 0.0};
        const double dx = x / magnitude, dy = y / magnitude;
        // A square deadzone reaches further on diagonals: along this
        // direction its edge sits at inner / max(|dx|, |dy|). Measuring from
        // that edge keeps the output continuous as the stick leaves it.
        double inner = s.inner;
        if (s.shape == StickSettings::Square)
            inner = std::min(s.inner / std::max(std::abs(dx), std::abs(dy)), s.outer - 1e-3);
        // Square-gated sticks can report past the unit circle in the
        // corners; the direction is kept and the length capped.
        const double length = shape(std::min(magnitude, 1.0), inner, s.outer, s);
        out = {dx * length, dy * length};
    }

    // Round-gated sticks like the DualShock 4 only reach about 71% on each
    // axis at a diagonal. Stretching the circle toward a square lets a
    // diagonal reach full travel on both, the way games tuned for Xbox pads
    // expect.
    const double length = std::hypot(out.x(), out.y());
    if (s.square > 0 && length > 1e-9) {
        const double reach = 1.0 / std::max(std::abs(out.x()), std::abs(out.y())) * length;
        out *= 1.0 + (reach - 1.0) * std::clamp(s.square, 0.0, 1.0);
    }
    return {std::clamp(out.x(), -1.0, 1.0), std::clamp(out.y(), -1.0, 1.0)};
}

double trigger(double raw, const TriggerSettings &s)
{
    return shape(raw, s.inner, s.outer, s.anti, 1.0);
}

}

namespace {

QJsonObject stickJson(const StickSettings &s)
{
    QJsonArray points;
    for (const QPointF &p : s.points)
        points.append(QJsonArray{p.x(), p.y()});
    const char *shapes[] = {"circle", "square", "cross"};
    return {{"shape", shapes[s.shape]}, {"inner", s.inner}, {"outer", s.outer}, {"anti", s.anti},
            {"curve", s.curve}, {"square", s.square}, {"custom", s.custom}, {"points", points}};
}

StickSettings stickFrom(const QJsonObject &o)
{
    StickSettings s;
    const QString shape = o.value("shape").toString();
    // Version 0.1 saved "mode": "radial" or "axial".
    if (shape == "square")
        s.shape = StickSettings::Square;
    else if (shape == "cross" || o.value("mode").toString() == "axial")
        s.shape = StickSettings::Cross;
    s.inner = std::clamp(o.value("inner").toDouble(s.inner), 0.0, 0.9);
    s.outer = std::clamp(o.value("outer").toDouble(s.outer), 0.1, 1.0);
    s.anti = std::clamp(o.value("anti").toDouble(s.anti), 0.0, 0.9);
    s.curve = std::clamp(o.value("curve").toDouble(s.curve), 0.2, 4.0);
    s.square = std::clamp(o.value("square").toDouble(s.square), 0.0, 1.0);
    s.custom = o.value("custom").toBool(false);
    const QJsonArray points = o.value("points").toArray();
    if (points.size() >= 2) {
        QList<QPointF> parsed;
        for (const QJsonValue &v : points)
            parsed.append({std::clamp(v.toArray().at(0).toDouble(), 0.0, 1.0),
                           std::clamp(v.toArray().at(1).toDouble(), 0.0, 1.0)});
        std::sort(parsed.begin(), parsed.end(), [](QPointF a, QPointF b) { return a.x() < b.x(); });
        s.points = parsed;
    }
    return s;
}

QJsonObject triggerJson(const TriggerSettings &t)
{
    return {{"inner", t.inner}, {"outer", t.outer}, {"anti", t.anti}};
}

TriggerSettings triggerFrom(const QJsonObject &o)
{
    TriggerSettings t;
    t.inner = std::clamp(o.value("inner").toDouble(t.inner), 0.0, 0.9);
    t.outer = std::clamp(o.value("outer").toDouble(t.outer), 0.1, 1.0);
    t.anti = std::clamp(o.value("anti").toDouble(t.anti), 0.0, 0.9);
    return t;
}

}

QJsonObject Profile::toJson() const
{
    return {{"name", name}, {"match", match}, {"preset", preset},
            {"left", stickJson(left)}, {"right", stickJson(right)},
            {"l2", triggerJson(l2)}, {"r2", triggerJson(r2)}};
}

Profile Profile::fromJson(const QJsonObject &o)
{
    Profile p;
    p.name = o.value("name").toString(p.name);
    p.match = o.value("match").toString();
    p.preset = o.value("preset").toString();
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
        if (field == "square") return s.square;
        if (field == "shape") return s.shape;
        if (field == "custom") return s.custom ? 1.0 : 0.0;
    } else if (group == "l2" || group == "r2") {
        const TriggerSettings &t = group == "l2" ? l2 : r2;
        if (field == "inner") return t.inner;
        if (field == "outer") return t.outer;
        if (field == "anti") return t.anti;
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
        else if (field == "square") s.square = std::clamp(v, 0.0, 1.0);
        else if (field == "shape") s.shape = StickSettings::Shape(std::clamp(int(std::lround(v)), 0, 2));
        else if (field == "custom") s.custom = v >= 0.5;
        else return false;
        return true;
    }
    if (group == "l2" || group == "r2") {
        TriggerSettings &t = group == "l2" ? l2 : r2;
        if (field == "inner") t.inner = std::clamp(v, 0.0, 0.9);
        else if (field == "outer") t.outer = std::clamp(v, 0.1, 1.0);
        else if (field == "anti") t.anti = std::clamp(v, 0.0, 0.9);
        else return false;
        return true;
    }
    return false;
}

namespace Presets {

QStringList ids()
{
    return {"default", "rocketleague", "shooter", "racing"};
}

Profile make(const QString &id)
{
    Profile p;
    p.preset = id;
    if (id == "rocketleague") {
        p.name = "Rocket League";
        p.match = "RocketLeague.exe";
        // Just enough deadzone to stop drift, full steer before the rim, and
        // a square range so a diagonal is full steer and full pitch at once:
        // half-flips, wavedashes, and boosting while turning hard.
        p.left = {StickSettings::Circle, 0.05, 0.90, 0.0, 1.0, 1.0, false, StickSettings::defaultPoints()};
        // The right stick only turns the camera, which wants no surprises.
        p.right = {StickSettings::Circle, 0.08, 0.95, 0.0, 1.0, 0.0, false, StickSettings::defaultPoints()};
        // Rocket League adds a 10% deadzone to both triggers on its own
        // (TAInput.ini); starting at 10% cancels it, and full throttle
        // arrives before the trigger bottoms out.
        p.l2 = {0.02, 0.85, 0.10};
        p.r2 = {0.02, 0.85, 0.10};
    } else if (id == "shooter") {
        p.name = "Shooter";
        p.left = {StickSettings::Circle, 0.06, 0.95, 0.0, 1.0, 0.0, false, StickSettings::defaultPoints()};
        // A gentle curve gives fine aim near the center and fast turns at the rim.
        p.right = {StickSettings::Circle, 0.06, 0.95, 0.0, 1.6, 0.0, false, StickSettings::defaultPoints()};
        p.l2 = {0.05, 0.60, 0.0};
        p.r2 = {0.05, 0.60, 0.0};
    } else if (id == "racing") {
        p.name = "Racing";
        // Cross keeps steering independent of how far up or down the stick sits.
        p.left = {StickSettings::Cross, 0.03, 0.95, 0.0, 1.3, 0.0, false, StickSettings::defaultPoints()};
        p.right = {StickSettings::Circle, 0.08, 0.95, 0.0, 1.0, 0.0, false, StickSettings::defaultPoints()};
        p.l2 = {0.02, 0.98, 0.0};
        p.r2 = {0.02, 0.98, 0.0};
    } else {
        p.preset = "default";
    }
    return p;
}

}
