#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QTcpServer>

class QTcpSocket;

// Serves the OBS overlay on this machine only: the page itself at /, and a
// stream of the controller's state at /events that the page listens to.
// OBS loads it as a Browser Source; nothing leaves the computer.
class OverlayServer : public QObject {
    Q_OBJECT

public:
    explicit OverlayServer(QObject *parent = nullptr);
    // Where /recording?name= reads saved input recordings from.
    void setRecordingsDirectory(const QString &directory) { m_recordings = directory; }

    // Returns false, with the reason in error(), if the port is taken.
    bool start(quint16 port);
    void stop();
    bool running() const { return m_server.isListening(); }
    QString error() const { return m_error; }
    int clients() const { return m_streams.size(); }
    quint16 port() const { return m_server.serverPort(); }

    // Sends one state, as JSON, to every listening overlay.
    void publish(const QByteArray &json);

signals:
    void clientsChanged();

private:
    void accept();
    void respond(QTcpSocket *socket);

    QTcpServer m_server;
    QList<QTcpSocket *> m_streams;
    QByteArray m_last;
    QString m_error;
    QString m_recordings;
};
