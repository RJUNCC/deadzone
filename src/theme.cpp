// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

namespace {

// The fixed themes: id, name, background, foreground, accent. Surfaces and
// text shades are mixed from these, so a new theme is one line.
struct Preset { const char *id, *name, *background, *foreground, *accent; };
const Preset presets[] = {
    {"graphite", QT_TRANSLATE_NOOP("Theme", "Graphite"), "#0e0e10", "#e4e4e7", "#FFD60A"},
    {"midnight", QT_TRANSLATE_NOOP("Theme", "Midnight"), "#0a0e17", "#e2e8f0", "#38bdf8"},
    {"violet", QT_TRANSLATE_NOOP("Theme", "Violet"), "#0f0b17", "#e9e2f5", "#a78bfa"},
    {"sakura", QT_TRANSLATE_NOOP("Theme", "Sakura"), "#140c11", "#f3e2ea", "#f472b6"},
    {"crimson", QT_TRANSLATE_NOOP("Theme", "Crimson"), "#110a0b", "#f2e1e2", "#f43f5e"},
    {"ember", QT_TRANSLATE_NOOP("Theme", "Ember"), "#120c0a", "#f1e4dc", "#fb923c"},
    {"forest", QT_TRANSLATE_NOOP("Theme", "Forest"), "#0a110d", "#dfeae2", "#4ade80"},
    {"lagoon", QT_TRANSLATE_NOOP("Theme", "Lagoon"), "#081213", "#dcefee", "#2dd4bf"},
    {"mono", QT_TRANSLATE_NOOP("Theme", "Mono"), "#0a0a0a", "#ededed", "#ffffff"},
    {"paper", QT_TRANSLATE_NOOP("Theme", "Paper"), "#f3f1ec", "#1c1b1a", "#2563eb"},
    {"sand", QT_TRANSLATE_NOOP("Theme", "Sand"), "#efe7da", "#2b2620", "#c2410c"},
};

const Preset *findPreset(const QString &id)
{
    for (const Preset &p : presets)
        if (id == QLatin1String(p.id))
            return &p;
    return nullptr;
}

double luminance(const QColor &c)
{
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
}

QString mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t).name();
}

}

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl, const QString &settingsPath)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory),
      m_settingsPath(!settingsPath.isEmpty() ? settingsPath
                     : directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/deadzone/appearance.json"
                                           : QString()) {
    loadSettings();
    apply();
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QString Theme::readColor(const QString &path, const QString &key, const QString &fallback) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(QStringLiteral(R"re(^\s*%1\s*=\s*["'](#[0-9a-fA-F]{6})["'])re").arg(key),
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return fallback;
}

QString Theme::readAccent(const QString &path) {
    return readColor(path, "accent", "#FFD60A");
}

QHash<QString, QString> Theme::palette(const QString &background, const QString &foreground) {
    const QColor bg(background), fg(foreground);
    const bool light = luminance(bg) > luminance(fg);
    // Light themes lift cards above the page with white; dark ones with the
    // foreground, a little at a time.
    const QColor card = light ? QColor(mix(bg, Qt::white, .7)) : QColor(mix(bg, fg, .045));
    return {
        {"background", bg.name()},
        {"sunken", mix(bg, card, .45)},
        {"surface", card.name()},
        {"raised", mix(card, fg, light ? .04 : .03)},
        {"hover", mix(card, fg, light ? .08 : .06)},
        {"border", mix(card, fg, light ? .1 : .045)},
        {"line", mix(card, fg, light ? .14 : .07)},
        {"lineStrong", mix(card, fg, light ? .22 : .12)},
        {"text", fg.name()},
        {"textSecondary", mix(bg, fg, .84)},
        {"muted", mix(bg, fg, light ? .62 : .55)},
        {"faint", mix(bg, fg, light ? .48 : .4)},
        {"scrim", "#cc" + bg.name().mid(1)},
    };
}

QVariantList Theme::themes() const {
    QVariantList list{QVariantMap{{"id", "omarchy"}, {"name", tr("Omarchy")}, {"background", m_desktopBackground},
                                  {"surface", palette(m_desktopBackground, m_desktopForeground).value("surface")},
                                  {"accent", m_desktopAccent}}};
    for (const Preset &p : presets)
        list.append(QVariantMap{{"id", p.id}, {"name", tr(p.name)}, {"background", p.background},
                                {"surface", palette(p.background, p.foreground).value("surface")}, {"accent", p.accent}});
    return list;
}

void Theme::apply() {
    const Preset *preset = findPreset(m_name);
    const QString bg = preset ? preset->background : m_desktopBackground;
    const QString fg = preset ? preset->foreground : m_desktopForeground;
    m_accent = preset ? preset->accent : m_desktopAccent;
    m_colors = palette(bg, fg);
    m_light = luminance(QColor(bg)) > luminance(QColor(fg));
}

QString Theme::card() const {
    QColor c(surface());
    if (m_particles)
        c.setAlphaF(m_light ? 0.86 : 0.8);
    return c.name(QColor::HexArgb);
}

void Theme::setName(const QString &name) {
    const QString id = findPreset(name) ? name : QStringLiteral("omarchy");
    if (id == m_name)
        return;
    m_name = id;
    apply();
    saveSettings();
    emit changed();
}

void Theme::setAnimations(bool on) {
    if (on == m_animations)
        return;
    m_animations = on;
    saveSettings();
    emit changed();
}

void Theme::setParticles(bool on) {
    if (on == m_particles)
        return;
    m_particles = on;
    saveSettings();
    emit changed();
}

void Theme::loadSettings() {
    if (m_settingsPath.isEmpty())
        return;
    QFile file(m_settingsPath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    const QString name = o.value("theme").toString();
    m_name = findPreset(name) ? name : QStringLiteral("omarchy");
    m_animations = o.value("animations").toBool(true);
    m_particles = o.value("particles").toBool(true);
}

void Theme::saveSettings() const {
    if (m_settingsPath.isEmpty())
        return;
    QDir().mkpath(QFileInfo(m_settingsPath).absolutePath());
    QSaveFile file(m_settingsPath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{"theme", m_name}, {"animations", m_animations}, {"particles", m_particles}}).toJson());
    file.commit();
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    return luminance(QColor(m_accent)) > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const QString colors = m_directory + "/theme/colors.toml";
    const QString accent = readAccent(colors);
    const QString background = readColor(colors, "background", "#0e0e10");
    const QString foreground = readColor(colors, "foreground", "#e4e4e7");
    if (accent != m_desktopAccent || background != m_desktopBackground || foreground != m_desktopForeground) {
        m_desktopAccent = accent;
        m_desktopBackground = background;
        m_desktopForeground = foreground;
        apply();
        emit changed();
    }
}
