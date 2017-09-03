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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "catch.hpp"

#include "base/algorithm.h"

TEST_CASE("push_back_safe_remove_if tests", "[base::algorithm]") {
	auto v = std::vector<int>();

	SECTION("doesn't change an empty vector") {
		base::push_back_safe_remove_if(v, [](int) { return true; });
		REQUIRE(v.empty());
	}

	v.insert(v.end(), { 1, 2, 3, 4, 5, 4, 3, 2, 1 });

	SECTION("allows to push_back from predicate") {
		base::push_back_safe_remove_if(v, [&v](int value) {
			v.push_back(value);
			return (value % 2) == 1;
		});
		auto expected = std::vector<int> { 2, 4, 4, 2, 1, 2, 3, 4, 5, 4, 3, 2, 1 };
		REQUIRE(v == expected);
	}

	SECTION("allows to push_back while removing all") {
		base::push_back_safe_remove_if(v, [&v](int value) {
			if (value == 5) {
				v.push_back(value);
			}
			return true;
		});
		auto expected = std::vector<int> { 5 };
		REQUIRE(v == expected);
	}
}