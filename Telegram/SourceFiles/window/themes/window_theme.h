/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
namespace Theme {
namespace internal {

constexpr auto FromLegacyBackgroundId(int32 legacyId) -> WallPaperId {
	return uint64(0xFFFFFFFF00000000ULL) | uint64(uint32(legacyId));
}

constexpr auto kUninitializedBackground = FromLegacyBackgroundId(-999);
constexpr auto kTestingThemeBackground = FromLegacyBackgroundId(-666);
constexpr auto kTestingDefaultBackground = FromLegacyBackgroundId(-665);
constexpr auto kTestingEditorBackground = FromLegacyBackgroundId(-664);
constexpr auto kLegacyBackgroundId = int32(-111);

} // namespace internal

constexpr auto kThemeBackground = internal::FromLegacyBackgroundId(-2);
constexpr auto kCustomBackground = internal::FromLegacyBackgroundId(-1);
constexpr auto kInitialBackground = internal::FromLegacyBackgroundId(0);
constexpr auto kDefaultBackground = internal::FromLegacyBackgroundId(105);

struct Cached {
	QByteArray colors;
	QByteArray background;
	bool tiled = false;
	int32 paletteChecksum = 0;
	int32 contentChecksum = 0;
};
struct Saved {
	QString pathRelative;
	QString pathAbsolute;
	QByteArray content;
	Cached cache;
};
bool Load(Saved &&saved);
void Unload();

struct Instance {
	style::palette palette;
	QImage background;
	Cached cached;
	bool tiled = false;
};

struct Preview {
	QString pathRelative;
	QString pathAbsolute;
	Instance instance;
	QByteArray content;
	QImage preview;
};

bool Apply(const QString &filepath);
bool Apply(std::unique_ptr<Preview> preview);
void ApplyDefaultWithPath(const QString &themePath);
bool ApplyEditedPalette(const QString &path, const QByteArray &content);
void KeepApplied();
QString NightThemePath();
bool IsNightMode();
void SetNightModeValue(bool nightMode);
void ToggleNightMode();
void ToggleNightMode(const QString &themePath);
bool IsNonDefaultBackground();
void Revert();

bool LoadFromFile(const QString &file, Instance *out, QByteArray *outContent);
bool IsPaletteTestingPath(const QString &path);

struct BackgroundUpdate {
	enum class Type {
		New,
		Changed,
		Start,
		TestingTheme,
		RevertingTheme,
		ApplyingTheme,
	};

	BackgroundUpdate(Type type, bool tiled) : type(type), tiled(tiled) {
	}
	bool paletteChanged() const {
		return (type == Type::TestingTheme || type == Type::RevertingTheme);
	}
	Type type;
	bool tiled;
};

class ChatBackground : public base::Observable<BackgroundUpdate> {
public:
	ChatBackground();

	// This method is allowed to (and should) be called before start().
	void setThemeData(QImage &&themeImage, bool themeTile);

	// This method is setting the default (themed) image if none was set yet.
	void start();
	void setImage(WallPaperId id, QImage &&image = QImage());
	void setTile(bool tile);
	void setTileDayValue(bool tile);
	void setTileNightValue(bool tile);
	void setThemeAbsolutePath(const QString &path);
	QString themeAbsolutePath() const;
	void reset();

	void setTestingTheme(Instance &&theme);
	void saveAdjustableColors();
	void setTestingDefaultTheme();
	void revert();

	WallPaperId id() const;
	const QPixmap &pixmap() const {
		return _pixmap;
	}
	const QPixmap &pixmapForTiled() const {
		return _pixmapForTiled;
	}
	bool tile() const;
	bool tileDay() const;
	bool tileNight() const;

private:
	struct AdjustableColor {
		AdjustableColor(style::color data);

		style::color item;
		QColor original;
	};

	void ensureStarted();
	void saveForRevert();
	void setPreparedImage(QImage &&image);
	void writeNewBackgroundSettings();

	void adjustPaletteUsingBackground(const QImage &img);
	void restoreAdjustableColors();

	void setNightModeValue(bool nightMode);
	bool nightMode() const;
	void toggleNightMode(std::optional<QString> themePath);
	void keepApplied(const QString &path, bool write);
	bool isNonDefaultThemeOrBackground();
	bool isNonDefaultBackground();

	friend bool IsNightMode();
	friend void SetNightModeValue(bool nightMode);
	friend void ToggleNightMode();
	friend void ToggleNightMode(const QString &themePath);
	friend void KeepApplied();
	friend bool IsNonDefaultBackground();

	WallPaperId _id = internal::kUninitializedBackground;
	QPixmap _pixmap;
	QPixmap _pixmapForTiled;
	bool _nightMode = false;
	bool _tileDayValue = false;
	bool _tileNightValue = true;

	QString _themeAbsolutePath;
	QImage _themeImage;
	bool _themeTile = false;

	WallPaperId _idForRevert = internal::kUninitializedBackground;
	QImage _imageForRevert;
	bool _tileForRevert = false;

	std::vector<AdjustableColor> _adjustableColors;

};

ChatBackground *Background();

void ComputeBackgroundRects(QRect wholeFill, QSize imageSize, QRect &to, QRect &from);

bool CopyColorsToPalette(const QString &path, const QByteArray &themeContent);

bool ReadPaletteValues(const QByteArray &content, Fn<bool(QLatin1String name, QLatin1String value)> callback);

} // namespace Theme
} // namespace Window
