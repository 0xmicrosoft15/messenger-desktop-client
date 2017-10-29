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
#pragma once

#include <deque>
#include "base/algorithm.h"

namespace base {

template <typename Type, typename Compare = std::less<>>
class flat_set;

template <typename Type, typename Compare = std::less<>>
class flat_multi_set;

template <typename Type, typename Compare, typename iterator_impl>
class flat_multi_set_iterator_impl;

template <typename Type, typename Compare, typename iterator_impl>
class flat_multi_set_iterator_impl {
public:
	using iterator_category = typename iterator_impl::iterator_category;

	using value_type = typename flat_multi_set<Type, Compare>::value_type;
	using difference_type = typename iterator_impl::difference_type;
	using pointer = typename flat_multi_set<Type, Compare>::pointer;
	using reference = typename flat_multi_set<Type, Compare>::reference;

	flat_multi_set_iterator_impl(iterator_impl impl = iterator_impl())
		: _impl(impl) {
	}
	template <typename other_iterator_impl>
	flat_multi_set_iterator_impl(
			const flat_multi_set_iterator_impl<
				Type,
				Compare,
				other_iterator_impl> &other) : _impl(other._impl) {
	}

	reference operator*() const {
		return *_impl;
	}
	pointer operator->() const {
		return std::addressof(**this);
	}
	flat_multi_set_iterator_impl &operator++() {
		++_impl;
		return *this;
	}
	flat_multi_set_iterator_impl operator++(int) {
		return _impl++;
	}
	flat_multi_set_iterator_impl &operator--() {
		--_impl;
		return *this;
	}
	flat_multi_set_iterator_impl operator--(int) {
		return _impl--;
	}
	flat_multi_set_iterator_impl &operator+=(difference_type offset) {
		_impl += offset;
		return *this;
	}
	flat_multi_set_iterator_impl operator+(difference_type offset) const {
		return _impl + offset;
	}
	flat_multi_set_iterator_impl &operator-=(difference_type offset) {
		_impl -= offset;
		return *this;
	}
	flat_multi_set_iterator_impl operator-(difference_type offset) const {
		return _impl - offset;
	}
	template <typename other_iterator_impl>
	difference_type operator-(
			const flat_multi_set_iterator_impl<
				Type,
				Compare,
				other_iterator_impl> &right) const {
		return _impl - right._impl;
	}
	reference operator[](difference_type offset) const {
		return _impl[offset];
	}

	template <typename other_iterator_impl>
	bool operator==(
			const flat_multi_set_iterator_impl<
				Type,
				Compare,
				other_iterator_impl> &right) const {
		return _impl == right._impl;
	}
	template <typename other_iterator_impl>
	bool operator!=(
			const flat_multi_set_iterator_impl<
				Type,
				Compare,
				other_iterator_impl> &right) const {
		return _impl != right._impl;
	}
	template <typename other_iterator_impl>
	bool operator<(
			const flat_multi_set_iterator_impl<
				Type,
				Compare,
				other_iterator_impl> &right) const {
		return _impl < right._impl;
	}

private:
	iterator_impl _impl;
	friend class flat_multi_set<Type, Compare>;
	friend class flat_set<Type, Compare>;

	template <
		typename OtherType,
		typename OtherCompare,
		typename other_iterator_impl>
	friend class flat_multi_set_iterator_impl;

	Type &wrapped() {
		return _impl->wrapped();
	}

};

template <typename Type, typename Compare>
class flat_multi_set {
	class const_wrap {
	public:
		constexpr const_wrap(const Type &value)
		: _value(value) {
		}
		constexpr const_wrap(Type &&value)
		: _value(std::move(value)) {
		}
		inline constexpr operator const Type&() const {
			return _value;
		}
		constexpr Type &wrapped() {
			return _value;
		}

	private:
		Type _value;

	};

	class compare {
	public:
		template <
			typename OtherType1,
			typename OtherType2,
			typename = std::enable_if_t<
				!std::is_same_v<std::decay_t<OtherType1>, const_wrap> &&
				!std::is_same_v<std::decay_t<OtherType2>, const_wrap>>>
		inline constexpr auto operator()(
				OtherType1 &&a,
				OtherType2 &b) const {
			return Compare()(
				std::forward<OtherType1>(a),
				std::forward<OtherType2>(b));
		}
		inline constexpr auto operator()(
				const const_wrap &a,
				const const_wrap &b) const {
			return operator()(
				static_cast<const Type&>(a),
				static_cast<const Type&>(b));
		}
		template <
			typename OtherType,
			typename = std::enable_if_t<
				!std::is_same_v<std::decay_t<OtherType>, const_wrap>>>
		inline constexpr auto operator()(
				const const_wrap &a,
				OtherType &&b) const {
			return operator()(
				static_cast<const Type&>(a),
				std::forward<OtherType>(b));
		}
		template <
			typename OtherType,
			typename = std::enable_if_t<
				!std::is_same_v<std::decay_t<OtherType>, const_wrap>>>
		inline constexpr auto operator()(
				OtherType &&a,
				const const_wrap &b) const {
			return operator()(
				std::forward<OtherType>(a),
				static_cast<const Type&>(b));
		}

	};

	using impl = std::deque<const_wrap>;

public:
	using value_type = Type;
	using size_type = typename impl::size_type;
	using difference_type = typename impl::difference_type;
	using pointer = const Type*;
	using reference = const Type&;

	using iterator = flat_multi_set_iterator_impl<
		Type,
		Compare,
		typename impl::iterator>;
	using const_iterator = flat_multi_set_iterator_impl<
		Type,
		Compare,
		typename impl::const_iterator>;
	using reverse_iterator = flat_multi_set_iterator_impl<
		Type,
		Compare,
		typename impl::reverse_iterator>;
	using const_reverse_iterator = flat_multi_set_iterator_impl<
		Type,
		Compare,
		typename impl::const_reverse_iterator>;

	flat_multi_set() = default;

	template <
		typename Iterator,
		typename = typename std::iterator_traits<Iterator>::iterator_category>
	flat_multi_set(Iterator first, Iterator last) : _impl(first, last) {
		base::sort(_impl, compare());
	}

	flat_multi_set(std::initializer_list<Type> iter)
		: flat_multi_set(iter.begin(), iter.end()) {
	}

	size_type size() const {
		return _impl.size();
	}
	bool empty() const {
		return _impl.empty();
	}
	void clear() {
		_impl.clear();
	}

	iterator begin() {
		return _impl.begin();
	}
	iterator end() {
		return _impl.end();
	}
	const_iterator begin() const {
		return _impl.begin();
	}
	const_iterator end() const {
		return _impl.end();
	}
	const_iterator cbegin() const {
		return _impl.cbegin();
	}
	const_iterator cend() const {
		return _impl.cend();
	}
	reverse_iterator rbegin() {
		return _impl.rbegin();
	}
	reverse_iterator rend() {
		return _impl.rend();
	}
	const_reverse_iterator rbegin() const {
		return _impl.rbegin();
	}
	const_reverse_iterator rend() const {
		return _impl.rend();
	}
	const_reverse_iterator crbegin() const {
		return _impl.crbegin();
	}
	const_reverse_iterator crend() const {
		return _impl.crend();
	}

	reference front() const {
		return *begin();
	}
	reference back() const {
		return *(end() - 1);
	}

	iterator insert(const Type &value) {
		if (empty() || compare()(value, front())) {
			_impl.push_front(value);
			return begin();
		} else if (!compare()(value, back())) {
			_impl.push_back(value);
			return (end() - 1);
		}
		auto where = getUpperBound(value);
		return _impl.insert(where, value);
	}
	iterator insert(Type &&value) {
		if (empty() || compare()(value, front())) {
			_impl.push_front(std::move(value));
			return begin();
		} else if (!compare()(value, back())) {
			_impl.push_back(std::move(value));
			return (end() - 1);
		}
		auto where = getUpperBound(value);
		return _impl.insert(where, std::move(value));
	}
	template <typename... Args>
	iterator emplace(Args&&... args) {
		return insert(Type(std::forward<Args>(args)...));
	}

	bool removeOne(const Type &value) {
		if (empty()
			|| compare()(value, front())
			|| compare()(back(), value)) {
			return false;
		}
		auto where = getLowerBound(value);
		if (compare()(value, *where)) {
			return false;
		}
		_impl.erase(where);
		return true;
	}
	int removeAll(const Type &value) {
		if (empty()
			|| compare()(value, front())
			|| compare()(back(), value)) {
			return 0;
		}
		auto range = getEqualRange(value);
		if (range.first == range.second) {
			return 0;
		}
		_impl.erase(range.first, range.second);
		return (range.second - range.first);
	}

	iterator erase(const_iterator where) {
		return _impl.erase(where._impl);
	}
	iterator erase(const_iterator from, const_iterator till) {
		return _impl.erase(from._impl, till._impl);
	}

	iterator findFirst(const Type &value) {
		if (empty()
			|| compare()(value, front())
			|| compare()(back(), value)) {
			return end();
		}
		auto where = getLowerBound(value);
		return compare()(value, *where) ? _impl.end() : where;
	}

	const_iterator findFirst(const Type &value) const {
		if (empty()
			|| compare()(value, front())
			|| compare()(back(), value)) {
			return end();
		}
		auto where = getLowerBound(value);
		return compare()(value, *where) ? _impl.end() : where;
	}

	template <
		typename OtherType,
		typename = typename Compare::is_transparent>
	iterator findFirst(const OtherType &value) {
		if (empty()
			|| compare()(value, front())
			|| compare()(back(), value)) {
			return end();
		}
		auto where = getLowerBound(value);
		return compare()(value, *where) ? _impl.end() : where;
	}

	template <
		typename OtherType,
		typename = typename Compare::is_transparent>
	const_iterator findFirst(const OtherType &value) const {
		if (empty()
			|| compare()(value, front())
			|| compare()(back(), value)) {
			return end();
		}
		auto where = getLowerBound(value);
		return compare()(value, *where) ? _impl.end() : where;
	}

	bool contains(const Type &value) const {
		return findFirst(value) != end();
	}
	int count(const Type &value) const {
		if (empty()
			|| compare()(value, front())
			|| compare()(back(), value)) {
			return 0;
		}
		auto range = getEqualRange(value);
		return (range.second - range.first);
	}

	template <typename Action>
	auto modify(iterator which, Action action) {
		auto result = action(which.wrapped());
		for (auto i = which + 1, e = end(); i != e; ++i) {
			if (compare()(*i, *which)) {
				std::swap(i.wrapped(), which.wrapped());
			} else {
				break;
			}
		}
		for (auto i = which, b = begin(); i != b;) {
			--i;
			if (compare()(*which, *i)) {
				std::swap(i.wrapped(), which.wrapped());
			} else {
				break;
			}
		}
		return result;
	}

	template <
		typename Iterator,
		typename = typename std::iterator_traits<Iterator>::iterator_category>
	void merge(Iterator first, Iterator last) {
		_impl.insert(_impl.end(), first, last);
		base::sort(_impl, compare());
	}

	void merge(const flat_multi_set<Type, Compare> &other) {
		merge(other.begin(), other.end());
	}

	void merge(std::initializer_list<Type> list) {
		merge(list.begin(), list.end());
	}

private:
	impl _impl;
	friend class flat_set<Type, Compare>;

	typename impl::iterator getLowerBound(const Type &value) {
		return base::lower_bound(_impl, value, compare());
	}
	typename impl::const_iterator getLowerBound(const Type &value) const {
		return base::lower_bound(_impl, value, compare());
	}
	template <
		typename OtherType,
		typename = typename Compare::is_transparent>
	typename impl::iterator getLowerBound(const OtherType &value) {
		return base::lower_bound(_impl, value, compare());
	}
	template <
		typename OtherType,
		typename = typename Compare::is_transparent>
	typename impl::const_iterator getLowerBound(const OtherType &value) const {
		return base::lower_bound(_impl, value, compare());
	}
	typename impl::iterator getUpperBound(const Type &value) {
		return base::upper_bound(_impl, value, compare());
	}
	typename impl::const_iterator getUpperBound(const Type &value) const {
		return base::upper_bound(_impl, value, compare());
	}
	std::pair<
		typename impl::iterator,
		typename impl::iterator
	> getEqualRange(const Type &value) {
		return base::equal_range(_impl, value, compare());
	}
	std::pair<
		typename impl::const_iterator,
		typename impl::const_iterator
	> getEqualRange(const Type &value) const {
		return base::equal_range(_impl, value, compare());
	}

};

template <typename Type, typename Compare>
class flat_set : private flat_multi_set<Type, Compare> {
	using parent = flat_multi_set<Type, Compare>;
	using compare = typename parent::compare;

public:
	using iterator = typename parent::iterator;
	using const_iterator = typename parent::const_iterator;
	using reverse_iterator = typename parent::reverse_iterator;
	using const_reverse_iterator = typename parent::const_reverse_iterator;
	using value_type = typename parent::value_type;
	using size_type = typename parent::size_type;
	using difference_type = typename parent::difference_type;
	using pointer = typename parent::pointer;
	using reference = typename parent::reference;

	flat_set() = default;

	template <
		typename Iterator,
		typename = typename std::iterator_traits<Iterator>::iterator_category
	>
	flat_set(Iterator first, Iterator last) : parent(first, last) {
		finalize();
	}

	flat_set(std::initializer_list<Type> iter) : parent(iter.begin(), iter.end()) {
		finalize();
	}

	using parent::parent;
	using parent::size;
	using parent::empty;
	using parent::clear;
	using parent::begin;
	using parent::end;
	using parent::cbegin;
	using parent::cend;
	using parent::rbegin;
	using parent::rend;
	using parent::crbegin;
	using parent::crend;
	using parent::front;
	using parent::back;
	using parent::contains;
	using parent::erase;

	iterator insert(const Type &value) {
		if (this->empty() || compare()(value, this->front())) {
			this->_impl.push_front(value);
			return this->begin();
		} else if (compare()(this->back(), value)) {
			this->_impl.push_back(value);
			return (this->end() - 1);
		}
		auto where = this->getLowerBound(value);
		if (compare()(value, *where)) {
			return this->_impl.insert(where, value);
		}
		return this->end();
	}
	iterator insert(Type &&value) {
		if (this->empty() || compare()(value, this->front())) {
			this->_impl.push_front(std::move(value));
			return this->begin();
		} else if (compare()(this->back(), value)) {
			this->_impl.push_back(std::move(value));
			return (this->end() - 1);
		}
		auto where = this->getLowerBound(value);
		if (compare()(value, *where)) {
			return this->_impl.insert(where, std::move(value));
		}
		return this->end();
	}
	template <typename... Args>
	iterator emplace(Args&&... args) {
		return this->insert(Type(std::forward<Args>(args)...));
	}

	bool remove(const Type &value) {
		return this->removeOne(value);
	}

	iterator find(const Type &value) {
		return this->findFirst(value);
	}
	const_iterator find(const Type &value) const {
		return this->findFirst(value);
	}
	template <
		typename OtherType,
		typename = typename Compare::is_transparent>
	iterator find(const OtherType &value) {
		return this->findFirst(value);
	}
	template <
		typename OtherType,
		typename = typename Compare::is_transparent>
	const_iterator find(const OtherType &value) const {
		return this->findFirst(value);
	}

	template <typename Action>
	void modify(iterator which, Action action) {
		action(which.wrapped());
		for (auto i = iterator(which + 1), e = end(); i != e; ++i) {
			if (compare()(*i, *which)) {
				std::swap(i.wrapped(), which.wrapped());
			} else if (!compare()(*which, *i)) {
				erase(which);
				return;
			} else{
				break;
			}
		}
		for (auto i = which, b = begin(); i != b;) {
			--i;
			if (compare()(*which, *i)) {
				std::swap(i.wrapped(), which.wrapped());
			} else if (!compare()(*i, *which)) {
				erase(which);
				return;
			} else {
				break;
			}
		}
	}

	template <
		typename Iterator,
		typename = typename std::iterator_traits<Iterator>::iterator_category>
	void merge(Iterator first, Iterator last) {
		parent::merge(first, last);
		finalize();
	}

	void merge(const flat_multi_set<Type, Compare> &other) {
		merge(other.begin(), other.end());
	}

	void merge(std::initializer_list<Type> list) {
		merge(list.begin(), list.end());
	}

private:
	void finalize() {
		this->_impl.erase(
			std::unique(
				this->_impl.begin(),
				this->_impl.end(),
				[](auto &&a, auto &&b) { return !compare()(a, b); }),
			this->_impl.end());
	}

};

} // namespace base
