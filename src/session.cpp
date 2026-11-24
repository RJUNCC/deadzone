#include "session.h"

#include <QCoreApplication>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Session", text);
}

// The value below which this share of the list falls.
double percentile(QList<double> values, double share)
{
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    return values.at(std::min<qsizetype>(values.size() - 1, qsizetype(share * values.size())));
}

QString percent(double v)
{
    return QString::number(std::lround(v * 100)) + "%";
}

QVariantMap suggestion(const QString &text, const QString &why, const QVariantMap &settings)
{
    return {{"text", text}, {"why", why}, {"settings", settings}};
}

}

void Session::add(QPointF left, QPointF right, double l2, double r2)
{
    ++m_samples;
    addStick(m_left, left);
    addStick(m_right, right);
    addTrigger(m_l2, l2);
    addTrigger(m_r2, r2);
}

void Session::addStick(Stick &s, QPointF raw)
{
    const double travel = std::min(1.0, std::hypot(raw.x(), raw.y()));

    // Resting: half a second near the center without moving. That is drift,
    // not a thumb.
    s.recent.push_back(travel);
    if (s.recent.size() > 30)
        s.recent.pop_front();
    if (s.recent.size() == 30) {
        const auto [low, high] = std::minmax_element(s.recent.begin(), s.recent.end());
        if (*high < 0.3 && *high - *low < 0.03)
            s.rest.append(travel);
    }

    const int col = std::clamp(int((raw.x() + 1) / 2 * Grid), 0, Grid - 1);
    const int row = std::clamp(int((raw.y() + 1) / 2 * Grid), 0, Grid - 1);
    ++s.cells[row * Grid + col];

    if (travel > 0.6) {
        const int bin = int((std::atan2(raw.y(), raw.x()) + M_PI) / (2 * M_PI) * Bins) % Bins;
        s.reach[bin] = std::max(s.reach[bin], travel);
    }
    if (travel > 0.15) {
        ++s.active;
        if (travel < 0.5)
            ++s.fine;
        if (travel > 0.85) {
            ++s.full;
            // Within 20 degrees of a diagonal.
            const double angle = std::fmod(std::abs(std::atan2(raw.y(), raw.x())), M_PI / 2);
            if (std::abs(angle - M_PI / 4) < M_PI / 9)
                ++s.diagonalFull;
        }
    }
}

void Session::addTrigger(Trigger &t, double value)
{
    t.peak = std::max(t.peak, value);
    if (value < 0.15)
        t.rest.append(value);
    else
        ++t.pressed;
}

void Session::stickSuggestions(const Stick &s, const StickSettings &settings, const QString &side,
                               const QString &name, QVariantList &out)
{
    // Drift: the deadzone should sit just above how far the resting stick
    // wanders, nearly all of the time.
    if (s.rest.size() >= 120) {
        const double drift = percentile(s.rest, 0.995);
        const double wanted = std::clamp(std::ceil((drift + 0.02) * 100) / 100, 0.02, 0.5);
        if (wanted > settings.inner + 0.005)
            out.append(suggestion(tr("%1: raise the deadzone to %2").arg(name, percent(wanted)),
                                  tr("Resting, it wandered up to %1, past the deadzone of %2, so the game saw small movements you did not make.").arg(percent(drift), percent(settings.inner)),
                                  {{side + ".inner", wanted}}));
        else if (wanted < settings.inner - 0.02)
            out.append(suggestion(tr("%1: lower the deadzone to %2").arg(name, percent(wanted)),
                                  tr("Resting, it never wandered past %1, so a deadzone of %2 hides more of the stick than it needs to.").arg(percent(drift), percent(settings.inner)),
                                  {{side + ".inner", wanted}}));
    }

    // Reach: the outer edge should sit where every direction still gets to.
    int covered = 0;
    double weakest = 1.0;
    for (double reach : s.reach)
        if (reach > 0.6) {
            ++covered;
            weakest = std::min(weakest, reach);
        }
    if (covered >= 24) {
        const double edge = std::clamp(std::floor((weakest - 0.02) * 100) / 100, 0.5, 1.0);
        if (edge < settings.outer - 0.01)
            out.append(suggestion(tr("%1: lower the outer edge to %2").arg(name, percent(edge)),
                                  tr("In some directions it only reached %1, short of the outer edge of %2, so full tilt never arrived there.").arg(percent(weakest), percent(settings.outer)),
                                  {{side + ".outer", edge}}));
    }

    if (s.active < 600)
        return;
    // Hard diagonals with a round range lose about 29% on each axis.
    const double diagonals = double(s.diagonalFull) / s.active;
    if (diagonals > 0.08 && settings.square < 0.9)
        out.append(suggestion(tr("%1: raise diagonal reach to 100%").arg(name),
                              tr("%1 of the time it was pushed hard into a diagonal, where a round stick only sends about 71% on each axis.").arg(percent(diagonals)),
                              {{side + ".square", 1.0}}));

    // Where the time goes: mostly small movements want a gentler center,
    // mostly full tilt wants no curve in the way.
    const double fine = double(s.fine) / s.active, full = double(s.full) / s.active;
    if (fine > 0.45 && !settings.custom && settings.curve < 1.3)
        out.append(suggestion(tr("%1: try the Soft center curve").arg(name),
                              tr("%1 of its movement was small, under half travel. A softer center spreads those small movements over more of the stick.").arg(percent(fine)),
                              {{side + ".custom", 0.0}, {side + ".curve", 1.5}}));
    else if (full > 0.6 && !settings.custom && settings.curve > 1.2)
        out.append(suggestion(tr("%1: straighten the response curve").arg(name),
                              tr("%1 of its movement was near full tilt, where a curve changes little but makes the way there slower.").arg(percent(full)),
                              {{side + ".custom", 0.0}, {side + ".curve", 1.0}}));
}

QVariantList Session::suggestions(const Profile &profile) const
{
    QVariantList out;
    // About a minute at the window's 60 samples a second.
    if (m_samples < 3600)
        return out;
    stickSuggestions(m_left, profile.left, "left", tr("Left stick"), out);
    stickSuggestions(m_right, profile.right, "right", tr("Right stick"), out);

    const struct { const Trigger &t; const TriggerSettings &settings; const char *key; QString name; } triggers[] = {
        {m_l2, profile.l2, "l2", tr("Left trigger")}, {m_r2, profile.r2, "r2", tr("Right trigger")}};
    for (const auto &[t, settings, key, name] : triggers) {
        if (t.pressed < 120)
            continue;
        const QString k = QString::fromLatin1(key);
        // Never pressed all the way: full throttle should arrive where the
        // finger actually stops.
        const double edge = std::clamp(std::floor((t.peak - 0.03) * 100) / 100, 0.5, 1.0);
        if (edge < settings.outer - 0.01)
            out.append(suggestion(tr("%1: lower the outer edge to %2").arg(name, percent(edge)),
                                  tr("It never went past %1, so the full press at %2 never arrived.").arg(percent(t.peak), percent(settings.outer)),
                                  {{k + ".outer", edge}}));
        const double rest = percentile(t.rest, 0.995);
        const double wanted = std::clamp(std::ceil((rest + 0.02) * 100) / 100, 0.0, 0.3);
        if (rest > 0.01 && wanted > settings.inner + 0.005)
            out.append(suggestion(tr("%1: raise the deadzone to %2").arg(name, percent(wanted)),
                                  tr("A resting finger pressed it up to %1, past the deadzone of %2.").arg(percent(rest), percent(settings.inner)),
                                  {{k + ".inner", wanted}}));
    }
    return out;
}

QVariantList Session::heatmap(bool left) const
{
    const Stick &s = left ? m_left : m_right;
    // The center cell holds all the resting time; it is left out of the
    // scale so the rest of the map is not washed out.
    const int center = (Grid / 2) * Grid + Grid / 2;
    int peak = 1;
    for (int i = 0; i < Grid * Grid; ++i)
        if (i != center && i != center - 1 && i != center - Grid && i != center - Grid - 1)
            peak = std::max(peak, s.cells[i]);
    QVariantList cells;
    for (int i = 0; i < Grid * Grid; ++i)
        cells.append(std::min(1.0, std::sqrt(double(s.cells[i]) / peak)));
    return cells;
}
