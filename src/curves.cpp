#include "curves.h"

#include <QJsonArray>
#include <QJsonDocument>
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

double boundary(double dx, double dy, const StickSettings &s)
{
    const double a = s.inner, b = s.split ? s.innerY : s.inner;
    switch (s.shape) {
    case StickSettings::Square: {
        // The edge of an a-by-b rectangle along this direction.
        const double x = std::abs(dx) > 1e-9 ? a / std::abs(dx) : 1e9;
        const double y = std::abs(dy) > 1e-9 ? b / std::abs(dy) : 1e9;
        return std::min(x, y);
    }
    case StickSettings::Custom: {
        // Straight interpolation between the eight drawn sizes, by angle.
        double angle = std::atan2(dy, dx);
        if (angle < 0)
            angle += 2 * M_PI;
        const double position = angle / (M_PI / 4);
        const int k = int(position) % 8;
        const double t = position - std::floor(position);
        const auto r = [&](int i) { return s.radii.value(i % 8, s.inner); };
        return r(k) + (r(k + 1) - r(k)) * t;
    }
    default:
        // A circle, or an ellipse when split.
        if (a < 1e-6 || b < 1e-6)
            return std::min(a, b);
        return 1.0 / std::sqrt((dx / a) * (dx / a) + (dy / b) * (dy / b));
    }
}

QStringList curvePresets()
{
    return {"linear", "soft", "precise", "quick", "scurve", "dual"};
}

void applyCurvePreset(StickSettings &s, const QString &id)
{
    s.custom = id == "scurve" || id == "dual";
    s.curve = id == "soft" ? 1.5 : id == "precise" ? 2.2 : id == "quick" ? 0.7 : 1.0;
    if (id == "scurve")
        s.points = {{0, 0}, {0.25, 0.1}, {0.5, 0.5}, {0.75, 0.9}, {1, 1}};
    else if (id == "dual")
        s.points = {{0, 0}, {0.25, 0.12}, {0.5, 0.28}, {0.75, 0.6}, {1, 1}};
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

namespace {

// Round-gated sticks like the DualShock 4 only reach about 71% on each axis
// at a diagonal. Stretching the circle toward a square lets a diagonal
// reach full travel on both, the way games tuned for Xbox pads expect.
QPointF stretch(QPointF out, double amount)
{
    const double length = std::hypot(out.x(), out.y());
    if (amount > 0 && length > 1e-9) {
        const double reach = 1.0 / std::max(std::abs(out.x()), std::abs(out.y())) * length;
        out *= 1.0 + (reach - 1.0) * std::clamp(amount, 0.0, 1.0);
    }
    return {std::clamp(out.x(), -1.0, 1.0), std::clamp(out.y(), -1.0, 1.0)};
}

}

QPointF stick(QPointF raw, const StickSettings &s)
{
    const double x = std::clamp(raw.x(), -1.0, 1.0);
    const double y = std::clamp(raw.y(), -1.0, 1.0);

    if (s.splitCurve) {
        // Separate curves: the deadzone, edge, and diagonal reach are applied
        // straight, then each axis is bent on its own, so steering and
        // pitch can feel different. Horizontal uses the main curve (drawn
        // or exponent), vertical its own exponent.
        StickSettings straight = s;
        straight.custom = false;
        straight.curve = 1.0;
        straight.anti = 0.0;
        straight.splitCurve = false;
        const QPointF linear = stick(raw, straight);
        StickSettings vertical = s;
        vertical.custom = false;
        vertical.curve = s.curveY;
        auto bend = [&](double v, const StickSettings &axis) {
            if (std::abs(v) < 1e-9)
                return 0.0;
            return std::copysign(s.anti + (1.0 - s.anti) * response(std::abs(v), axis), v);
        };
        return {bend(linear.x(), s), bend(linear.y(), vertical)};
    }

    QPointF out;
    if (s.shape == StickSettings::Cross) {
        // Each axis on its own: a straight push stays exactly straight.
        auto axis = [&](double v, double inner) { return std::copysign(shape(std::abs(v), inner, s.outer, s), v); };
        out = {axis(x, s.inner), axis(y, s.split ? s.innerY : s.inner)};
    } else {
        const double magnitude = std::hypot(x, y);
        if (magnitude <= 1e-9)
            return {0.0, 0.0};
        const double dx = x / magnitude, dy = y / magnitude;
        // Measuring from wherever the deadzone's edge sits along this
        // direction keeps the output continuous as the stick leaves it,
        // whatever the shape.
        const double inner = boundary(dx, dy, s);
        // Square-gated sticks can report past the unit circle in the
        // corners; the direction is kept and the length capped.
        const double length = shape(std::min(magnitude, 1.0), inner, s.outer, s);
        out = {dx * length, dy * length};
    }
    return stretch(out, s.square);
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
    const char *shapes[] = {"circle", "square", "cross", "custom"};
    QJsonArray radii;
    for (double r : s.radii)
        radii.append(r);
    return {{"shape", shapes[s.shape]}, {"inner", s.inner}, {"innerY", s.innerY}, {"split", s.split}, {"radii", radii},
            {"outer", s.outer}, {"anti", s.anti},
            {"curve", s.curve}, {"splitCurve", s.splitCurve}, {"curveY", s.curveY}, {"square", s.square}, {"custom", s.custom}, {"points", points}};
}

StickSettings stickFrom(const QJsonObject &o)
{
    StickSettings s;
    const QString shape = o.value("shape").toString();
    // Version 0.1 saved "mode": "radial" or "axial".
    if (shape == "square")
        s.shape = StickSettings::Square;
    else if (shape == "custom")
        s.shape = StickSettings::Custom;
    else if (shape == "cross" || o.value("mode").toString() == "axial")
        s.shape = StickSettings::Cross;
    s.inner = std::clamp(o.value("inner").toDouble(s.inner), 0.0, 1.0);
    s.innerY = std::clamp(o.value("innerY").toDouble(s.inner), 0.0, 1.0);
    s.split = o.value("split").toBool(false);
    s.radii = QList<double>(8, s.inner);
    const QJsonArray radii = o.value("radii").toArray();
    for (int i = 0; i < 8 && i < radii.size(); ++i)
        s.radii[i] = std::clamp(radii.at(i).toDouble(), 0.0, 1.0);
    s.outer = std::clamp(o.value("outer").toDouble(s.outer), 0.1, 1.0);
    s.anti = std::clamp(o.value("anti").toDouble(s.anti), 0.0, 0.9);
    s.curve = std::clamp(o.value("curve").toDouble(s.curve), 0.2, 4.0);
    s.splitCurve = o.value("splitCurve").toBool(false);
    s.curveY = std::clamp(o.value("curveY").toDouble(s.curve), 0.2, 4.0);
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

QList<int> Profile::defaultButtons()
{
    QList<int> map;
    for (int i = 0; i < ButtonCount; ++i)
        map.append(i);
    return map;
}

QJsonObject Profile::toJson() const
{
    QJsonArray map;
    for (int b : buttons)
        map.append(b);
    return {{"name", name}, {"match", match}, {"preset", preset}, {"color", color},
            {"left", stickJson(left)}, {"right", stickJson(right)},
            {"l2", triggerJson(l2)}, {"r2", triggerJson(r2)}, {"buttons", map}};
}

Profile Profile::fromJson(const QJsonObject &o)
{
    Profile p;
    p.name = o.value("name").toString(p.name);
    p.match = o.value("match").toString();
    p.preset = o.value("preset").toString();
    p.color = o.value("color").toString();
    p.left = stickFrom(o.value("left").toObject());
    p.right = stickFrom(o.value("right").toObject());
    p.l2 = triggerFrom(o.value("l2").toObject());
    p.r2 = triggerFrom(o.value("r2").toObject());
    const QJsonArray map = o.value("buttons").toArray();
    for (int i = 0; i < ButtonCount && i < map.size(); ++i)
        p.buttons[i] = std::clamp(map.at(i).toInt(i), int(Unmapped), int(FullR2));
    return p;
}

double Profile::value(const QString &key) const
{
    const QString group = key.section('.', 0, 0), field = key.section('.', 1);
    if (group == "left" || group == "right") {
        const StickSettings &s = group == "left" ? left : right;
        if (field == "inner") return s.inner;
        if (field == "innerY") return s.innerY;
        if (field == "split") return s.split ? 1.0 : 0.0;
        if (field == "outer") return s.outer;
        if (field == "anti") return s.anti;
        if (field == "curve") return s.curve;
        if (field == "curveY") return s.curveY;
        if (field == "splitCurve") return s.splitCurve ? 1.0 : 0.0;
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
        if (field == "inner") s.inner = std::clamp(v, 0.0, 1.0);
        else if (field == "innerY") s.innerY = std::clamp(v, 0.0, 1.0);
        else if (field == "split") s.split = v >= 0.5;
        else if (field == "outer") s.outer = std::clamp(v, 0.1, 1.0);
        else if (field == "anti") s.anti = std::clamp(v, 0.0, 0.9);
        else if (field == "curve") s.curve = std::clamp(v, 0.2, 4.0);
        else if (field == "curveY") s.curveY = std::clamp(v, 0.2, 4.0);
        else if (field == "splitCurve") s.splitCurve = v >= 0.5;
        else if (field == "square") s.square = std::clamp(v, 0.0, 1.0);
        else if (field == "shape") s.shape = StickSettings::Shape(std::clamp(int(std::lround(v)), 0, 3));
        else if (field == "custom") s.custom = v >= 0.5;
        else return false;
        return true;
    }
    if (group == "l2" || group == "r2") {
        TriggerSettings &t = group == "l2" ? l2 : r2;
        if (field == "inner") t.inner = std::clamp(v, 0.0, 1.0);
        else if (field == "outer") t.outer = std::clamp(v, 0.1, 1.0);
        else if (field == "anti") t.anti = std::clamp(v, 0.0, 0.9);
        else return false;
        return true;
    }
    return false;
}

namespace Codes {

QString encode(const Profile &profile)
{
    QJsonObject json = profile.toJson();
    json.remove("match");
    const QByteArray packed = qCompress(QJsonDocument(json).toJson(QJsonDocument::Compact), 9);
    return "DZ1-" + QString::fromLatin1(packed.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool decode(const QString &code, Profile &profile)
{
    const QString trimmed = code.trimmed();
    if (!trimmed.startsWith("DZ1-"))
        return false;
    const auto decoded = QByteArray::fromBase64Encoding(trimmed.mid(4).toLatin1(),
                                                        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded)
        return false;
    // A pasted code is untrusted: it is size-checked, then parsed with the
    // same clamping as the config file.
    const QByteArray json = qUncompress(*decoded);
    if (json.isEmpty() || json.size() > 64 * 1024)
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject() || !document.object().contains("left"))
        return false;
    profile = Profile::fromJson(document.object());
    profile.name = profile.name.left(40);
    return true;
}

}

namespace Presets {

QStringList ids()
{
    return {"rocketleague", "freestyle", "worn"};
}

namespace {

StickSettings stick(StickSettings::Shape shape, double inner, double outer, double curve, double square)
{
    StickSettings s;
    s.shape = shape;
    s.inner = s.innerY = inner;
    s.radii = QList<double>(8, inner);
    s.outer = outer;
    s.curve = curve;
    s.square = square;
    return s;
}

}

Profile make(const QString &id)
{
    Profile p;
    p.preset = id;
    // Rocket League adds a 10% deadzone to both triggers on its own
    // (TAInput.ini); starting at 10% cancels it, and full throttle arrives
    // before the trigger bottoms out.
    p.l2 = p.r2 = {0.02, 0.85, 0.10};
    // The right stick only turns the camera, which wants no surprises.
    p.right = stick(StickSettings::Circle, 0.08, 0.95, 1.0, 0.0);
    if (id == "freestyle") {
        p.name = "Freestyle";
        // A touch of curve for fine air roll and aerial corrections, with
        // the rim still reaching full on diagonals.
        p.left = stick(StickSettings::Circle, 0.05, 0.88, 1.25, 1.0);
    } else if (id == "worn") {
        p.name = "Worn stick";
        // Older sticks drift further and stop short of the rim.
        p.left = stick(StickSettings::Circle, 0.12, 0.82, 1.0, 1.0);
        p.right = stick(StickSettings::Circle, 0.12, 0.90, 1.0, 0.0);
    } else {
        p.preset = "rocketleague";
        p.name = "Rocket League";
        // Just enough deadzone to stop drift, full steer before the rim,
        // and full reach on diagonals: a diagonal is full steer and full
        // pitch at once, for half-flips and turning while boosting.
        p.left = stick(StickSettings::Circle, 0.05, 0.90, 1.0, 1.0);
    }
    return p;
}

}
