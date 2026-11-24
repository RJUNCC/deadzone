#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QVariantList>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    // The whole palette. "omarchy" follows the desktop theme's colors; the
    // others are fixed. Every color the window draws comes from here.
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QVariantList themes READ themes CONSTANT)
    Q_PROPERTY(bool light READ light NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString sunken READ sunken NOTIFY changed)
    Q_PROPERTY(QString surface READ surface NOTIFY changed)
    // Cards: the surface, a touch see-through while particles drift behind.
    Q_PROPERTY(QString card READ card NOTIFY changed)
    Q_PROPERTY(QString raised READ raised NOTIFY changed)
    Q_PROPERTY(QString hover READ hover NOTIFY changed)
    Q_PROPERTY(QString border READ border NOTIFY changed)
    Q_PROPERTY(QString line READ line NOTIFY changed)
    Q_PROPERTY(QString lineStrong READ lineStrong NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString textSecondary READ textSecondary NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString faint READ faint NOTIFY changed)
    Q_PROPERTY(QString scrim READ scrim NOTIFY changed)
    // Motion: transitions between tabs and states, and the particles.
    Q_PROPERTY(bool animations READ animations WRITE setAnimations NOTIFY changed)
    Q_PROPERTY(bool particles READ particles WRITE setParticles NOTIFY changed)
    // A companion along the top of the page: "none", "car", "cat", or "dog".
    Q_PROPERTY(QString buddy READ buddy WRITE setBuddy NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {},
                   const QString &settingsPath = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }

    QString name() const { return m_name; }
    void setName(const QString &name);
    QVariantList themes() const;
    bool light() const { return m_light; }
    QString background() const { return m_colors.value("background"); }
    QString sunken() const { return m_colors.value("sunken"); }
    QString surface() const { return m_colors.value("surface"); }
    QString card() const;
    QString raised() const { return m_colors.value("raised"); }
    QString hover() const { return m_colors.value("hover"); }
    QString border() const { return m_colors.value("border"); }
    QString line() const { return m_colors.value("line"); }
    QString lineStrong() const { return m_colors.value("lineStrong"); }
    QString text() const { return m_colors.value("text"); }
    QString textSecondary() const { return m_colors.value("textSecondary"); }
    QString muted() const { return m_colors.value("muted"); }
    QString faint() const { return m_colors.value("faint"); }
    QString scrim() const { return m_colors.value("scrim"); }
    bool animations() const { return m_animations; }
    void setAnimations(bool on);
    bool particles() const { return m_particles; }
    void setParticles(bool on);
    QString buddy() const { return m_buddy; }
    void setBuddy(const QString &buddy);

    // Every surface and text shade, mixed from three colors so each theme
    // stays consistent: the window background, the foreground, the accent.
    static QHash<QString, QString> palette(const QString &background, const QString &foreground);
    static QString readColor(const QString &path, const QString &key, const QString &fallback);

    static QString readAccent(const QString &path);

signals:
    void changed();

private:
    void reload();
    void apply();
    void loadSettings();
    void saveSettings() const;
    void refreshRounding();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QString m_desktopAccent = "#FFD60A", m_desktopBackground = "#0e0e10", m_desktopForeground = "#e4e4e7";
    QString m_name = "omarchy";
    QString m_settingsPath;
    QHash<QString, QString> m_colors;
    bool m_light = false;
    bool m_animations = true;
    bool m_particles = true;
    QString m_buddy = "car";
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
