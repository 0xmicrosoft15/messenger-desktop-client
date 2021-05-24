/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "calls/group/calls_group_viewport.h"
#include "ui/gl/gl_surface.h"

#include <QtGui/QOpenGLBuffer>
#include <QtGui/QOpenGLShaderProgram>

namespace Calls::Group {

class Viewport::RendererGL final : public Ui::GL::Renderer {
public:
	explicit RendererGL(not_null<Viewport*> owner);

	void free(const Textures &textures);

	void init(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) override;

	void deinit(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) override;

	void resize(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f,
		int w,
		int h) override;

	void paint(
		not_null<QOpenGLWidget*> widget,
		not_null<QOpenGLFunctions*> f) override;

private:
	void fillBackground(not_null<QOpenGLFunctions*> f);
	void paintTile(
		not_null<QOpenGLFunctions*> f,
		not_null<VideoTile*> tile);
	void freeTextures(not_null<QOpenGLFunctions*> f);
	[[nodiscard]] QRect tileGeometry(not_null<VideoTile*> tile) const;
	void ensureARGB32Program();

	const not_null<Viewport*> _owner;

	GLfloat _factor = 1.;
	QSize _viewport;
	std::optional<QOpenGLBuffer> _frameBuffer;
	std::optional<QOpenGLBuffer> _bgBuffer;
	std::optional<QOpenGLShaderProgram> _argb32Program;
	std::optional<QOpenGLShaderProgram> _yuv420Program;
	std::optional<QOpenGLShaderProgram> _bgProgram;
	QOpenGLShader *_frameVertexShader = nullptr;

	std::vector<GLfloat> _bgTriangles;
	std::vector<Textures> _texturesToFree;

};

} // namespace Calls::Group
