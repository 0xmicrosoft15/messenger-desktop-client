/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "codegen/style/generator.h"

#include <QtGui/QImage>
#include "codegen/style/parsed_file.h"

namespace codegen {
namespace style {
namespace {

} // namespace

Generator::Generator(const Options &options)
: parser_(std::make_unique<ParsedFile>(options))
, options_(options) {

}

int Generator::process() {
	if (!parser_->read()) {
		return -1;
	}

	const auto &result = parser_->data();
	if (!write(result)) {
		return -1;
	}
	if (options_.rebuildDependencies) {
		for (auto included : result.includes) {
			if (!write(included)) {
				return -1;
			}
		}
	}
	return 0;
}

bool Generator::write(const structure::Module &) const {
	return true;
}

Generator::~Generator() = default;

} // namespace style
} // namespace codegen
