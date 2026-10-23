/*
 * ControlServer.h - local JSON control socket for driving LMMS from another program
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_GUI_CONTROL_SERVER_H
#define LMMS_GUI_CONTROL_SERVER_H

#include <QJsonObject>
#include <QObject>

class QLocalServer;
class QLocalSocket;

namespace lmms
{

class Clip;
class Track;

namespace gui
{

/**
 * @brief A local socket that lets another program on the same machine drive LMMS while it runs.
 *
 * It is off unless the environment variable LMMS_CONTROL_SOCKET names a socket path (a Unix
 * socket, or a named pipe on Windows). Each request is one line of JSON, an object with a "cmd"
 * and its arguments; each answer is one line of JSON with "ok" and either "result" or "error",
 * and the request's "id" if it had one. Requests run on the GUI thread, like the user's own
 * actions, so they see and change exactly what the GUI does.
 */
class ControlServer : public QObject
{
	Q_OBJECT
public:
	explicit ControlServer(QObject* parent = nullptr);
	~ControlServer() override;

	//! Starts the server if LMMS_CONTROL_SOCKET is set; returns nullptr otherwise
	static ControlServer* createFromEnvironment(QObject* parent);

	bool listen(const QString& path);

private slots:
	void acceptConnection();
	void readRequests(QLocalSocket* socket);

private:
	QJsonObject handle(const QJsonObject& request);

	QJsonObject status() const;
	QJsonObject listTracks() const;
	QJsonObject setTrack(const QJsonObject& args);
	QJsonObject seek(const QJsonObject& args);
	QJsonObject getNotes(const QJsonObject& args) const;
	QJsonObject setNotes(const QJsonObject& args);
	QJsonObject reloadSamples(const QJsonObject& args);
	QJsonObject openProject(const QJsonObject& args);
	QJsonObject saveProject(const QJsonObject& args);

	Track* findTrack(const QJsonObject& args) const;
	Clip* findClip(Track* track, const QJsonObject& args) const;

	QLocalServer* m_server;
	bool m_busy;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_CONTROL_SERVER_H
