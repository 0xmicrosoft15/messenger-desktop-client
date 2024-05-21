/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_credits.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"
#if _DEBUG
#include "base/random.h"
#endif

namespace Api {
namespace {

[[nodiscard]] Data::CreditsHistoryEntry HistoryFromTL(
		const MTPStarsTransaction &tl) {
	using HistoryPeerTL = MTPDstarsTransactionPeer;
	return Data::CreditsHistoryEntry{
		.id = qs(tl.data().vid()),
		.credits = tl.data().vstars().v,
		.date = base::unixtime::parse(tl.data().vdate().v),
		.peerType = tl.data().vpeer().match([](const HistoryPeerTL &) {
			return Data::CreditsHistoryEntry::PeerType::Peer;
		}, [](const MTPDstarsTransactionPeerPlayMarket &) {
			return Data::CreditsHistoryEntry::PeerType::PlayMarket;
		}, [](const MTPDstarsTransactionPeerFragment &) {
			return Data::CreditsHistoryEntry::PeerType::Fragment;
		}, [](const MTPDstarsTransactionPeerAppStore &) {
			return Data::CreditsHistoryEntry::PeerType::AppStore;
		}, [](const MTPDstarsTransactionPeerUnsupported &) {
			return Data::CreditsHistoryEntry::PeerType::Unsupported;
		}, [](const MTPDstarsTransactionPeerPremiumBot &) {
			return Data::CreditsHistoryEntry::PeerType::PremiumBot;
		}),
		.peerId = tl.data().vpeer().match([](const HistoryPeerTL &p) {
			return peerFromMTP(p.vpeer());
		}, [](const auto &) {
			return PeerId(0);
		}),
	};
}

[[nodiscard]] Data::CreditsStatusSlice StatusFromTL(
		const MTPpayments_StarsStatus &status,
		not_null<PeerData*> peer) {
	peer->owner().processUsers(status.data().vusers());
	peer->owner().processChats(status.data().vchats());
	return Data::CreditsStatusSlice{
		.list = ranges::views::all(
			status.data().vhistory().v
		) | ranges::views::transform(HistoryFromTL) | ranges::to_vector,
		.balance = status.data().vbalance().v,
		.allLoaded = status.data().vnext_offset().has_value(),
		.token = qs(status.data().vnext_offset().value_or_empty()),
	};
}

} // namespace

CreditsTopupOptions::CreditsTopupOptions(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

rpl::producer<rpl::no_value, QString> CreditsTopupOptions::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		using TLOption = MTPStarsTopupOption;
		_api.request(MTPpayments_GetStarsTopupOptions(
		)).done([=](const MTPVector<TLOption> &result) {
			_options = ranges::views::all(
				result.v
			) | ranges::views::transform([](const TLOption &option) {
				return Data::CreditTopupOption{
					.credits = option.data().vstars().v,
					.product = qs(
						option.data().vstore_product().value_or_empty()),
					.currency = qs(option.data().vcurrency()),
					.amount = option.data().vamount().v,
				};
			}) | ranges::to_vector;
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return lifetime;
	};
}

CreditsStatus::CreditsStatus(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

void CreditsStatus::request(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done) {
	if (_requestId) {
		return;
	}

	using TLResult = MTPpayments_StarsStatus;

	_requestId = _api.request(MTPpayments_GetStarsStatus(
		_peer->isSelf() ? MTP_inputPeerSelf() : _peer->input
	)).done([=](const TLResult &result) {
		_requestId = 0;
#if _DEBUG
		done({ .balance = uint64(base::RandomIndex(9999)) });
		return;
#endif
		done(StatusFromTL(result, _peer));
	}).fail([=] {
		_requestId = 0;
		done({});
	}).send();
}

CreditsHistory::CreditsHistory(not_null<PeerData*> peer, bool in, bool out)
: _peer(peer)
, _flags(HistoryTL::Flags(0)
	| (in ? HistoryTL::Flag::f_inbound : HistoryTL::Flags(0))
	| (out ? HistoryTL::Flag::f_outbound : HistoryTL::Flags(0)))
, _api(&peer->session().api().instance()) {
}

void CreditsHistory::request(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done) {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPpayments_GetStarsTransactions(
		MTP_flags(_flags),
		_peer->isSelf() ? MTP_inputPeerSelf() : _peer->input,
		MTP_string(token)
	)).done([=](const MTPpayments_StarsStatus &result) {
		_requestId = 0;
#if _DEBUG
		done({
			.list = [&] {
				auto a = std::vector<Data::CreditsHistoryEntry>();
				const auto isIn = _flags & HistoryTL::Flag::f_inbound;
				const auto isOut = _flags & HistoryTL::Flag::f_outbound;
				for (auto i = 0; i < base::RandomIndex(10) + 1; i++) {
					const auto type = (isIn && isOut)
						? base::RandomIndex(4)
						: isOut
						? 0
						: (base::RandomIndex(3) + 1);
					a.push_back(Data::CreditsHistoryEntry{
						.id = QString::number(base::RandomValue<uint64>()),
						.credits = uint64(
							std::max(base::RandomIndex(15000), 1)),
						.date = base::unixtime::parse(
							std::abs(base::RandomValue<TimeId>())),
						.peerType = ((type == 0)
							? Data::CreditsHistoryEntry::PeerType::Peer
							: (type == 1)
							? Data::CreditsHistoryEntry::PeerType::PlayMarket
							: (type == 2)
							? Data::CreditsHistoryEntry::PeerType::Fragment
							: Data::CreditsHistoryEntry::PeerType::AppStore),
						.peerId = (type == 0)
							? peerFromUser(5000233800)
							: PeerId(0),
					});
				}
				return a;
			}(),
			.balance = 47890,
			.allLoaded = !token.isEmpty(),
			.token = token.isEmpty()
				? QString::number(base::RandomValue<uint64>())
				: QString(),
		});
		return;
#endif
		done(StatusFromTL(result, _peer));
	}).fail([=] {
		_requestId = 0;
		done({});
	}).send();
}

Data::CreditTopupOptions CreditsTopupOptions::options() const {
	return _options;
}

} // namespace Api
