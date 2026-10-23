/*
 * ControlServer.cpp - local JSON control socket for driving LMMS from another program
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

#include "ControlServer.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "panning.h"
#include "PathUtil.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleDecoder.h"
#include "SampleTrack.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

//! Thrown by a command to answer with an error
struct ControlError
{
	QString message;
};

QString trackTypeName(Track::Type type)
{
	switch (type)
	{
		case Track::Type::Instrument: return "instrument";
		case Track::Type::Pattern: return "pattern";
		case Track::Type::Sample: return "sample";
		case Track::Type::Automation: return "automation";
		case Track::Type::HiddenAutomation: return "hidden-automation";
		default: return "other";
	}
}

FloatModel* volumeModel(Track* track)
{
	if (auto it = dynamic_cast<InstrumentTrack*>(track)) { return it->volumeModel(); }
	if (auto st = dynamic_cast<SampleTrack*>(track)) { return st->volumeModel(); }
	return nullptr;
}

FloatModel* panningModel(Track* track)
{
	if (auto it = dynamic_cast<InstrumentTrack*>(track)) { return it->panningModel(); }
	if (auto st = dynamic_cast<SampleTrack*>(track)) { return st->panningModel(); }
	return nullptr;
}

//! A position in ticks from "tick", or from "bar" (counted from 1, as the GUI shows it)
tick_t positionArg(const QJsonObject& args, const QString& tickKey = "tick")
{
	if (args.contains(tickKey)) { return args.value(tickKey).toInt(); }
	if (args.contains("bar"))
	{
		return static_cast<tick_t>((args.value("bar").toDouble() - 1) * Engine::getSong()->ticksPerBar());
	}
	throw ControlError{QString("give \"%1\" or \"bar\"").arg(tickKey)};
}

QJsonObject clipJson(const Clip* clip)
{
	auto out = QJsonObject{
		{"name", clip->name()},
		{"pos", static_cast<int>(clip->startPosition())},
		{"len", static_cast<int>(clip->length())},
		{"muted", clip->isMuted()}};
	if (auto sc = dynamic_cast<const SampleClip*>(clip)) { out["file"] = PathUtil::toAbsolute(sc->sampleFile()); }
	if (auto mc = dynamic_cast<const MidiClip*>(clip)) { out["notes"] = static_cast<int>(mc->notes().size()); }
	return out;
}

} // namespace




ControlServer::ControlServer(QObject* parent)
	: QObject(parent)
	, m_server(new QLocalServer(this))
	, m_busy(false)
{
	m_server->setSocketOptions(QLocalServer::UserAccessOption);
	connect(m_server, &QLocalServer::newConnection, this, &ControlServer::acceptConnection);
}




ControlServer::~ControlServer()
{
	m_server->close();
}




ControlServer* ControlServer::createFromEnvironment(QObject* parent)
{
	const auto path = qEnvironmentVariable("LMMS_CONTROL_SOCKET");
	if (path.isEmpty()) { return nullptr; }

	auto server = new ControlServer(parent);
	if (!server->listen(path))
	{
		delete server;
		return nullptr;
	}
	return server;
}




bool ControlServer::listen(const QString& path)
{
	// A socket file left behind by a crashed LMMS would block listening
	QLocalServer::removeServer(path);
	if (!m_server->listen(path))
	{
		qWarning() << "ControlServer: cannot listen on" << path << ":" << m_server->errorString();
		return false;
	}
	qInfo() << "ControlServer: listening on" << m_server->fullServerName();
	return true;
}




void ControlServer::acceptConnection()
{
	while (QLocalSocket* socket = m_server->nextPendingConnection())
	{
		connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readRequests(socket); });
		connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
	}
}




void ControlServer::readRequests(QLocalSocket* socket)
{
	while (socket->canReadLine())
	{
		const auto line = socket->readLine().trimmed();
		if (line.isEmpty()) { continue; }

		auto error = QJsonParseError{};
		const auto doc = QJsonDocument::fromJson(line, &error);
		auto answer = QJsonObject{};
		if (!doc.isObject())
		{
			answer = {{"ok", false}, {"error", "a request is one JSON object per line: " + error.errorString()}};
		}
		else if (m_busy && doc.object().value("cmd").toString() != "status")
		{
			// Loading a project processes events, so another request can arrive before it ends
			answer = {{"ok", false}, {"error", "busy with an earlier request"}};
		}
		else
		{
			const bool wasBusy = m_busy;
			m_busy = true;
			answer = handle(doc.object());
			m_busy = wasBusy;
		}
		if (doc.object().contains("id")) { answer["id"] = doc.object().value("id"); }

		socket->write(QJsonDocument(answer).toJson(QJsonDocument::Compact) + '\n');
		socket->flush();
	}
}




QJsonObject ControlServer::handle(const QJsonObject& request)
{
	const auto cmd = request.value("cmd").toString();
	Song* song = Engine::getSong();
	try
	{
		// Loading a project processes events, so requests can arrive while its tracks are half built
		if (song->isLoadingProject() && cmd != "status")
		{
			throw ControlError{"a project is loading; ask again when status says \"loading\": false"};
		}

		auto result = QJsonObject{};
		if (cmd == "status") { result = status(); }
		else if (cmd == "tracks") { result = listTracks(); }
		else if (cmd == "set_track") { result = setTrack(request); }
		else if (cmd == "play")
		{
			if (request.contains("tick") || request.contains("bar")) { seek(request); }
			if (!song->isPlaying()) { song->playSong(); }
			result = status();
		}
		else if (cmd == "pause")
		{
			if (song->isPlaying()) { song->togglePause(); }
			result = status();
		}
		else if (cmd == "stop")
		{
			song->stop();
			result = status();
		}
		else if (cmd == "seek") { result = seek(request); }
		else if (cmd == "get_notes") { result = getNotes(request); }
		else if (cmd == "set_notes") { result = setNotes(request); }
		else if (cmd == "reload_samples") { result = reloadSamples(request); }
		else if (cmd == "open") { result = openProject(request); }
		else if (cmd == "save") { result = saveProject(request); }
		else
		{
			throw ControlError{"unknown cmd \"" + cmd + "\"; the commands are status, tracks, set_track, play, "
				"pause, stop, seek, get_notes, set_notes, reload_samples, open and save"};
		}
		return {{"ok", true}, {"result", result}};
	}
	catch (const ControlError& e)
	{
		return {{"ok", false}, {"error", e.message}};
	}
}




QJsonObject ControlServer::status() const
{
	Song* song = Engine::getSong();
	const auto tick = static_cast<int>(song->getPlayPos(Song::PlayMode::Song));
	return {
		{"project", song->projectFileName()},
		{"modified", song->isModified()},
		{"loading", song->isLoadingProject()},
		{"playing", song->isPlaying()},
		{"paused", song->isPaused()},
		{"tick", tick},
		{"bar", 1.0 + static_cast<double>(tick) / song->ticksPerBar()},
		{"ticks_per_bar", song->ticksPerBar()},
		{"bpm", song->getTempo()},
		{"length_ticks", static_cast<int>(song->length()) * song->ticksPerBar()}};
}




QJsonObject ControlServer::listTracks() const
{
	auto tracks = QJsonArray{};
	for (Track* track : Engine::getSong()->tracks())
	{
		auto t = QJsonObject{
			{"name", track->name()},
			{"type", trackTypeName(track->type())},
			{"muted", track->isMuted()},
			{"solo", track->isSolo()}};
		if (auto vol = volumeModel(track)) { t["volume"] = vol->value(); }
		if (auto pan = panningModel(track)) { t["pan"] = pan->value(); }
		auto clips = QJsonArray{};
		for (const Clip* clip : track->getClips()) { clips.append(clipJson(clip)); }
		t["clips"] = clips;
		tracks.append(t);
	}
	return {{"tracks", tracks}};
}




QJsonObject ControlServer::setTrack(const QJsonObject& args)
{
	Track* track = findTrack(args);
	if (args.contains("volume") || args.contains("pan"))
	{
		auto vol = volumeModel(track);
		auto pan = panningModel(track);
		if (!vol || !pan) { throw ControlError{"track \"" + track->name() + "\" has no volume or pan"}; }
		if (args.contains("volume")) { vol->setValue(static_cast<float>(args.value("volume").toDouble())); }
		if (args.contains("pan")) { pan->setValue(static_cast<float>(args.value("pan").toDouble())); }
	}
	if (args.contains("muted")) { track->setMuted(args.value("muted").toBool()); }
	if (args.contains("solo")) { track->setSolo(args.value("solo").toBool()); }

	auto out = QJsonObject{{"name", track->name()}, {"muted", track->isMuted()}, {"solo", track->isSolo()}};
	if (auto vol = volumeModel(track)) { out["volume"] = vol->value(); }
	if (auto pan = panningModel(track)) { out["pan"] = pan->value(); }
	return out;
}




QJsonObject ControlServer::seek(const QJsonObject& args)
{
	Song* song = Engine::getSong();
	const auto ticks = std::max<tick_t>(0, positionArg(args));
	song->getTimeline(Song::PlayMode::Song).setTicks(ticks);
	if (!song->isPlaying())
	{
		// The GUI keeps the stopped position here too; see TimeLineWidget::mouseMoveEvent
		song->getTimeline(Song::PlayMode::None).setTicks(ticks);
	}
	return status();
}




QJsonObject ControlServer::getNotes(const QJsonObject& args) const
{
	auto clip = dynamic_cast<MidiClip*>(findClip(findTrack(args), args));
	if (!clip) { throw ControlError{"that clip holds no notes"}; }

	auto notes = QJsonArray{};
	for (const Note* note : clip->notes())
	{
		notes.append(QJsonObject{
			{"pos", static_cast<int>(note->pos())},
			{"len", static_cast<int>(note->length())},
			{"key", note->key()},
			{"vol", note->getVolume()},
			{"pan", note->getPanning()}});
	}
	auto out = clipJson(clip);
	out["notes"] = notes;
	return out;
}




QJsonObject ControlServer::setNotes(const QJsonObject& args)
{
	auto clip = dynamic_cast<MidiClip*>(findClip(findTrack(args), args));
	if (!clip) { throw ControlError{"that clip holds no notes"}; }

	// Check every note before touching the clip, so a bad one changes nothing
	auto notes = std::vector<Note>{};
	for (const auto& value : args.value("notes").toArray())
	{
		const auto n = value.toObject();
		const int key = n.value("key").toInt(-1);
		const int len = n.value("len").toInt();
		if (key < 0 || key >= NumKeys || len <= 0 || !n.contains("pos"))
		{
			throw ControlError{"each note needs \"pos\", \"len\" > 0 and \"key\" 0-127, in ticks from the clip's start"};
		}
		notes.emplace_back(TimePos{len}, TimePos{n.value("pos").toInt()}, key,
			static_cast<volume_t>(std::clamp(n.value("vol").toInt(DefaultVolume), 0, int{MaxVolume})),
			static_cast<panning_t>(std::clamp(n.value("pan").toInt(DefaultPanning), int{PanningLeft}, int{PanningRight})));
	}

	clip->clearNotes();
	for (const auto& note : notes) { clip->addNote(note, false); }
	clip->rearrangeAllNotes();
	Engine::getSong()->setModified();
	return getNotes(args);
}




QJsonObject ControlServer::reloadSamples(const QJsonObject& args)
{
	Track* track = findTrack(args);
	if (track->type() != Track::Type::Sample) { throw ControlError{"\"" + track->name() + "\" is not a sample track"}; }

	auto clips = std::vector<SampleClip*>{};
	if (args.contains("tick") || args.contains("bar") || args.contains("clip"))
	{
		clips.push_back(dynamic_cast<SampleClip*>(findClip(track, args)));
	}
	else
	{
		for (Clip* clip : track->getClips()) { clips.push_back(dynamic_cast<SampleClip*>(clip)); }
	}

	auto reloaded = QJsonArray{};
	for (SampleClip* clip : clips)
	{
		const auto file = PathUtil::toAbsolute(args.contains("file") ? args.value("file").toString() : clip->sampleFile());
		if (!QFileInfo::exists(file)) { throw ControlError{"no file at " + file}; }

		// Decode here rather than with SampleBuffer::fromFile, which shows a message box when it fails
		auto decoded = SampleDecoder::decode(file);
		if (!decoded) { throw ControlError{"cannot decode " + file}; }
		auto buffer = std::make_shared<SampleBuffer>(
			std::move(decoded->data), decoded->sampleRate, PathUtil::toShortestRelative(file));

		// Swaps the buffer under the audio engine's lock, keeping the clip's place and offset
		clip->setSampleBuffer(std::move(buffer));
		reloaded.append(clipJson(clip));
	}
	return {{"name", track->name()}, {"clips", reloaded}};
}




QJsonObject ControlServer::openProject(const QJsonObject& args)
{
	Song* song = Engine::getSong();
	const auto file = args.value("path").toString();
	if (!QFileInfo::exists(file)) { throw ControlError{"no project at " + file}; }
	if (song->isModified() && !args.value("discard_changes").toBool())
	{
		throw ControlError{"the open project has unsaved changes; save it, or pass \"discard_changes\": true"};
	}

	song->stop();
	song->loadProject(file);
	return status();
}




QJsonObject ControlServer::saveProject(const QJsonObject& args)
{
	Song* song = Engine::getSong();
	const auto file = args.contains("path") ? args.value("path").toString() : song->projectFileName();
	if (file.isEmpty()) { throw ControlError{"the project has no file yet; pass \"path\""}; }
	if (!song->guiSaveProjectAs(file)) { throw ControlError{"could not save " + file}; }
	return status();
}




Track* ControlServer::findTrack(const QJsonObject& args) const
{
	const auto name = args.value("track").toString();
	for (Track* track : Engine::getSong()->tracks())
	{
		if (track->name() == name) { return track; }
	}
	throw ControlError{"no track named \"" + name + "\""};
}




Clip* ControlServer::findClip(Track* track, const QJsonObject& args) const
{
	const auto& clips = track->getClips();
	if (args.contains("clip"))
	{
		const auto name = args.value("clip").toString();
		for (Clip* clip : clips)
		{
			if (clip->name() == name) { return clip; }
		}
		throw ControlError{"\"" + track->name() + "\" has no clip named \"" + name + "\""};
	}

	// The clip starting at the position, else the one playing there
	const auto ticks = positionArg(args);
	for (Clip* clip : clips)
	{
		if (clip->startPosition() == ticks) { return clip; }
	}
	for (Clip* clip : clips)
	{
		if (clip->startPosition() <= ticks && ticks < clip->endPosition()) { return clip; }
	}
	throw ControlError{QString("\"%1\" has no clip at tick %2").arg(track->name()).arg(ticks)};
}

} // namespace lmms::gui
