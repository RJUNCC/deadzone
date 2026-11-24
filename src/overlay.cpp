#include "overlay.h"

#include <QFile>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QUrl>

OverlayServer::OverlayServer(QObject *parent) : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &OverlayServer::accept);
}

// Open connections are children of the server, which is destroyed after the
// list of streams; closing one then would reach into that list. Cut them
// loose from this object and close them first.
OverlayServer::~OverlayServer()
{
    const auto sockets = m_server.findChildren<QTcpSocket *>();
    for (QTcpSocket *socket : sockets) {
        disconnect(socket, nullptr, this, nullptr);
        delete socket;
    }
    m_streams.clear();
}

bool OverlayServer::start(quint16 port)
{
    stop();
    // Localhost only: the overlay is for OBS on this computer, not the network.
    if (!m_server.listen(QHostAddress::LocalHost, port)) {
        m_error = m_server.errorString();
        return false;
    }
    m_error.clear();
    return true;
}

void OverlayServer::stop()
{
    for (QTcpSocket *socket : std::as_const(m_streams))
        socket->deleteLater();
    m_streams.clear();
    m_server.close();
    emit clientsChanged();
}

void OverlayServer::accept()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { respond(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            if (m_streams.removeAll(socket) > 0)
                emit clientsChanged();
            socket->deleteLater();
        });
    }
}

void OverlayServer::respond(QTcpSocket *socket)
{
    // Only the request line matters; OBS sends nothing worth reading after it.
    if (!socket->canReadLine() || m_streams.contains(socket))
        return;
    const QList<QByteArray> request = socket->readLine().trimmed().split(' ');
    socket->readAll();
    const QByteArray path = request.value(1).split('?').value(0);

    if (path == "/events") {
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\n"
                      "Connection: keep-alive\r\n\r\n");
        if (!m_last.isEmpty())
            socket->write("data: " + m_last + "\n\n");
        m_streams.append(socket);
        emit clientsChanged();
        return;
    }

    QByteArray body, type = "text/html; charset=utf-8", status = "200 OK";
    if (path == "/" || path == "/overlay") {
        QFile page(":/overlay.html");
        if (page.open(QIODevice::ReadOnly))
            body = page.readAll();
    } else if (path == "/recording") {
        // Only a plain recording name, never a path out of the folder.
        const QByteArray query = request.value(1).split('?').value(1);
        QString name;
        for (const QByteArray &pair : query.split('&'))
            if (pair.startsWith("name="))
                name = QUrl::fromPercentEncoding(pair.mid(5));
        static const QRegularExpression safe("^inputs-[0-9-]+\\.json$");
        QFile file(m_recordings + "/" + name);
        if (!m_recordings.isEmpty() && safe.match(name).hasMatch() && file.open(QIODevice::ReadOnly)) {
            body = file.readAll();
            type = "application/json";
        } else {
            status = "404 Not Found";
            body = "Not found";
            type = "text/plain";
        }
    } else if (path == "/state") {
        body = m_last.isEmpty() ? "{}" : m_last;
        type = "application/json";
    } else {
        status = "404 Not Found";
        body = "Not found";
        type = "text/plain";
    }
    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: "
                  + QByteArray::number(body.size()) + "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n" + body);
    socket->disconnectFromHost();
}

void OverlayServer::publish(const QByteArray &json)
{
    if (json == m_last)
        return;
    m_last = json;
    const QByteArray event = "data: " + json + "\n\n";
    for (QTcpSocket *socket : std::as_const(m_streams))
        socket->write(event);
}
