#include "stdafx.h"
#include "../../common/tables.h"
#include "packet.h"
#include "item.h"
#include "char.h"
#include "item_manager.h"
#include "desc.h"
#include "char_manager.h"
#include "banword.h"
#include "buffer_manager.h"
#include "desc_client.h"
#include "config.h"
#include "event.h"
#include "locale_service.h"
#include <fstream>
#include "questmanager.h"
#include "sectree_manager.h"
#include "sectree.h"
#include "config.h"
#include <msl/random.h>
#include "../../common/utils.h"

#ifdef ENABLE_IKASHOP_RENEWAL

#include "ikarus_shop.h"
#include "ikarus_shop_manager.h"

static const std::set<int> ALLOWED_SPAWN_MAPS = { 1,21,41 };

template<class DerivedType, class BaseType>
DerivedType DerivedFromBase(const BaseType& base) {
	static_assert(std::is_base_of<BaseType, DerivedType>::value); // DerivedType must be inherited by BaseType
	DerivedType temporary{};
	static_cast<BaseType&>(temporary) = base;
	return temporary;
}


// #define ENABLE_IKASHOP_GM_PROTECTION
static bool CheckGMLevel(LPCHARACTER ch) 
{
	return
#ifdef ENABLE_IKASHOP_GM_PROTECTION
		ch->GetGMLevel() == GM_PLAYER || test_server;
#else
		true;
#endif
}

bool MatchAttributes(const TPlayerItemAttribute* pAttributesFilter,const TPlayerItemAttribute* pAttributesItem)
{
	static auto findAttribute = [](auto type, auto val, auto attr){
		for (int j = 0; j < ITEM_ATTRIBUTE_NORM_NUM; j++)
			if (attr[j].bType == type)
				return attr[j].sValue >= val;
		return false;
	};

	auto matchFilterAttribute = [&](auto filter){
		return filter.bType == 0 || findAttribute(filter.bType, filter.sValue, pAttributesItem);
	};

	return std::all_of(pAttributesFilter, pAttributesFilter + ITEM_ATTRIBUTE_NORM_NUM, matchFilterAttribute);
}

std::string StringToLower(const char* name, size_t len)
{
	// std::tolower handles ints so transform reject it as operand
	static auto lower = [](auto&& ch){ return std::tolower(ch); };
	// filling string inserting lower characters
	std::string res = {name, len};
	std::transform(res.begin(), res.end(), res.begin(), lower);
	return res;
}

bool IsGoodSalePrice(const ikashop::TPriceInfo price) {
	if (price.yang >= GOLD_MAX || price.yang < 0)
		return false;
#ifdef ENABLE_CHEQUE_SYSTEM
	else if (price.cheque >= CHEQUE_MAX || price.cheque < 0)
		return false;
#endif
	return true;
}

bool MatchItemName(std::string stName, const char* table , const size_t tablelen)
{
	const auto lower = StringToLower(table, tablelen);
	return lower.find(stName) != std::string::npos;
}

bool CheckCharacterActions(LPCHARACTER ch)
{
	if(!ch || !ch->CanHandleItem())
		return false;

	if(ch->GetExchange())
		return false;

	if(ch->GetSafebox())
		return false;

	if(ch->GetShop())
		return false;

#if defined(ENABLE_ACCE_SYSTEM) || defined(ENABLE_ACCE_COSTUME_SYSTEM) || defined(ENABLE_SASH_SYSTEM) || defined(__SASH_SYSTEM) || defined(ACCE_SYSTEM) || defined(__ACCE_SYSTEM__)
	if(ch->IsAcceOpened())
		return false;
#endif

	if(quest::CQuestManager::Instance().GetPCForce(ch->GetPlayerID())->IsRunning())
		return false;

	return true;
}

bool MatchFilterType(const ikashop::TShopItem& info, const ikashop::TFilterInfo& filter, TItemTable* table)
{
	if(filter.type == 0)
		return true;

	static auto MatchWeapon = [](const ikashop::TShopItem& info, const ikashop::TFilterInfo& filter, TItemTable* table) -> bool {
		if(table->bType != ITEM_WEAPON)
			return false;
		if(filter.subtype == 0)
			return true;
		switch(filter.subtype)
		{
			case ikashop::SEARCH_SHOP_SUBTYPE_WEAPON_SWORD:
				return table->bSubType == WEAPON_SWORD
					&& IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_SHAMAN) 
					&& !IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_WARRIOR) 
					&& !IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_ASSASSIN) 
					&& !IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_SURA);
			case ikashop::SEARCH_SHOP_SUBTYPE_WEAPON_BLADE:
				return table->bSubType == WEAPON_SWORD 
					&& IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_SHAMAN)
					&& IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_WARRIOR)
					&& IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_ASSASSIN)
					&& !IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_SURA);
			case ikashop::SEARCH_SHOP_SUBTYPE_WEAPON_TWOHANDED:
				return table->bSubType == WEAPON_TWO_HANDED;
			case ikashop::SEARCH_SHOP_SUBTYPE_WEAPON_DAGGER:
				return table->bSubType == WEAPON_DAGGER;
			case ikashop::SEARCH_SHOP_SUBTYPE_WEAPON_BOW:
				return table->bSubType == WEAPON_BOW;
			case ikashop::SEARCH_SHOP_SUBTYPE_WEAPON_BELL:
				return table->bSubType == WEAPON_BELL;
			case ikashop::SEARCH_SHOP_SUBTYPE_WEAPON_FAN:
				return table->bSubType == WEAPON_FAN;
#ifdef ENABLE_WOLFMAN_CHARACTER
			case ikashop::SEARCH_SHOP_SUBTYPE_WEAPON_CLAW:
				return table->bSubType == WEAPON_CLAW;
#endif
		}
		return false;
	};

	static auto MatchArmor = [](const ikashop::TShopItem& info, const ikashop::TFilterInfo& filter, TItemTable* table) {
		if(table->bType != ITEM_ARMOR || table->bSubType != ARMOR_BODY)
			return false;
		if(filter.subtype == 0)
			return true;
		switch(filter.subtype)
		{
			case ikashop::SEARCH_SHOP_SUBTYPE_ARMOR_WARRIOR:
				return !IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_WARRIOR);
			case ikashop::SEARCH_SHOP_SUBTYPE_ARMOR_SURA:
				return !IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_SURA);
			case ikashop::SEARCH_SHOP_SUBTYPE_ARMOR_ASSASSIN:
				return !IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_ASSASSIN);
			case ikashop::SEARCH_SHOP_SUBTYPE_ARMOR_SHAMAN:
				return !IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_SHAMAN);
#ifdef ENABLE_WOLFMAN_CHARACTER
			case ikashop::SEARCH_SHOP_SUBTYPE_ARMOR_WOLFMAN:
				return !IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_WOLFMAN);
#endif
		}
		return false;
	};

	static auto MatchJewel = [](const ikashop::TShopItem& info, const ikashop::TFilterInfo& filter, TItemTable* table) {
		if (table->bType != ITEM_ARMOR || table->bSubType == ARMOR_BODY)
			return false;
		if (filter.subtype == 0)
			return true;
		switch(filter.subtype)
		{
			case ikashop::SEARCH_SHOP_SUBTYPE_JEWEL_HELM:
				return table->bSubType == ARMOR_HEAD;
			case ikashop::SEARCH_SHOP_SUBTYPE_JEWEL_SHIELD:
				return table->bSubType == ARMOR_SHIELD;
			case ikashop::SEARCH_SHOP_SUBTYPE_JEWEL_EAR:
				return table->bSubType == ARMOR_EAR;
			case ikashop::SEARCH_SHOP_SUBTYPE_JEWEL_NECK:
				return table->bSubType == ARMOR_NECK;
			case ikashop::SEARCH_SHOP_SUBTYPE_JEWEL_WRIST:
				return table->bSubType == ARMOR_WRIST;
			case ikashop::SEARCH_SHOP_SUBTYPE_JEWEL_SHOES:
				return table->bSubType == ARMOR_FOOTS;
		}
		return false;
	};

	static auto MatchCostume = [](const ikashop::TShopItem& info, const ikashop::TFilterInfo& filter, TItemTable* table) {
		if(table->bType != ITEM_COSTUME)
			return false;
		if(filter.subtype == 0)
			return true;
		switch(filter.subtype)
		{
		case ikashop::SEARCH_SHOP_SUBTYPE_COSTUME_BODY:
			return table->bSubType == COSTUME_BODY;
		case ikashop::SEARCH_SHOP_SUBTYPE_COSTUME_WEAPON:
			return table->bSubType == COSTUME_WEAPON;
		case ikashop::SEARCH_SHOP_SUBTYPE_COSTUME_HAIR:
			return table->bSubType == COSTUME_HAIR;
#ifdef ENABLE_ACCE_COSTUME_SYSTEM
		case ikashop::SEARCH_SHOP_SUBTYPE_COSTUME_SASH:
			return table->bSubType == COSTUME_ACCE;
#endif
		}
		return false;
	};

	static auto MatchPet = [](const ikashop::TShopItem& info, const ikashop::TFilterInfo& filter, TItemTable* table) {
		if(table->bType != ITEM_PET || (table->bSubType != PET_EGG && table->bSubType != PET_UPBRINGING && table->bSubType != PET_PAY))
			return false;
		if(filter.subtype == 0)
			return true;
		switch(filter.subtype)
		{
		case ikashop::SEARCH_SHOP_SUBTYPE_PET_EGGS:
			return table->bSubType == PET_EGG;
		case ikashop::SEARCH_SHOP_SUBTYPE_PET_SEALS:
			return table->bSubType == PET_UPBRINGING || table->bSubType == PET_PAY;
		}
		return false;
	};

	switch(filter.type)
	{
		case ikashop::SEARCH_SHOP_TYPE_WEAPON:
			return MatchWeapon(info, filter, table);
		case ikashop::SEARCH_SHOP_TYPE_ARMOR:
			return MatchArmor(info, filter, table);
		case ikashop::SEARCH_SHOP_TYPE_JEWEL:
			return MatchJewel(info, filter, table);
		case ikashop::SEARCH_SHOP_TYPE_COSTUME:
			return MatchCostume(info, filter, table);
		case ikashop::SEARCH_SHOP_TYPE_PET:
			return MatchPet(info, filter, table);
		case ikashop::SEARCH_SHOP_TYPE_ALCHEMY:
			return table->bType == ITEM_DS;
		case ikashop::SEARCH_SHOP_TYPE_BOOK:
			return table->bType == ITEM_SKILLBOOK;
		case ikashop::SEARCH_SHOP_TYPE_STONE:
			return table->bType == ITEM_METIN;
		case ikashop::SEARCH_SHOP_TYPE_FISH:
			return table->bType == ITEM_FISH;
		case ikashop::SEARCH_SHOP_TYPE_MINERALS:
			return table->bType == ITEM_USE && (table->bSubType == USE_PUT_INTO_ACCESSORY_SOCKET || table->bSubType == USE_PUT_INTO_BELT_SOCKET);
		case ikashop::SEARCH_SHOP_TYPE_MOUNT:
			return table->bType == ITEM_COSTUME && table->bSubType == COSTUME_MOUNT;
	}

	return false;
}

bool MatchSpecial(const ikashop::TShopItem& info, const ikashop::TFilterInfo& filter, TItemTable* table)
{
	if(table->bType == ITEM_DS)
	{
		if(filter.alchemyGrade != 0)
		{
			const auto grade = (table->dwVnum / 1000) % 10;
			return grade+1 >= filter.alchemyGrade;
		}
	}

#ifdef ENABLE_ACCE_COSTUME_SYSTEM
	if(table->bType == ITEM_COSTUME && table->bSubType == COSTUME_ACCE)
	{
		if(filter.sashGrade != 0)
		{
			switch(filter.sashGrade)
			{
			case ikashop::SEARCH_SHOP_SASH_GRADE_VALUE0:
				return info.alSockets[ACCE_ABSORPTION_SOCKET] >= 10;
			case ikashop::SEARCH_SHOP_SASH_GRADE_VALUE1:
				return info.alSockets[ACCE_ABSORPTION_SOCKET] >= 15;
			case ikashop::SEARCH_SHOP_SASH_GRADE_VALUE2:
				return info.alSockets[ACCE_ABSORPTION_SOCKET] >= 20;
			case ikashop::SEARCH_SHOP_SASH_GRADE_VALUE3:
				return info.alSockets[ACCE_ABSORPTION_SOCKET] == 25;
			}
			return false;
		}
	}
#endif

	return true;
}

bool MatchFilter(const ikashop::TShopItem& info, const ikashop::TFilterInfo& filter, const std::string& filtername)
{
	auto table = ITEM_MANAGER::instance().GetTable(info.vnum);
	if (!table)
	{
		sys_err("CANNOT FIND ITEM TABLE [%d]", info.vnum);
		return false;
	}

	// filter by type / subtype
	if(!MatchFilterType(info, filter, table))
		return false;

	// filter by level
	if ((filter.levelstart != 0 || filter.levelend != 0))
	{
		const auto level = table->aLimits[0].bType == LIMIT_LEVEL ?
			table->aLimits[0].lValue :
			table->aLimits[1].bType == LIMIT_LEVEL ?
			table->aLimits[1].lValue : 0;
		if (filter.levelstart != 0 && filter.levelstart > level)
			return false;
		if (filter.levelend != 0 && filter.levelend < level)
			return false;
	}

	// filter by price
	if (filter.pricestart.yang != 0 || filter.pricend.yang != 0)
	{
		const auto total = info.price.GetTotalYangAmount();
		if (filter.pricestart.yang != 0 && total < filter.pricestart.yang)
			return false;

		if (filter.pricend.yang != 0 && total > filter.pricend.yang)
			return false;
	}

	// filter by name
	if (!filtername.empty() && !MatchItemName(filtername, table->szLocaleName, strnlen(table->szLocaleName, ITEM_NAME_MAX_LEN)))
		return false;

	// filter by attrs
	if (!MatchAttributes(filter.attrs, info.aAttr))
		return false;

	// filter bu special
	if (!MatchSpecial(info, filter, table))
		return false;

	return true;
}

long long GetAuctionMinRaise(ikashop::CShopManager::AUCTION_HANDLE auction)
{
	auto bestoff = auction->GetBestOffer();
	if(!bestoff)
		return auction->GetInfo().price.GetTotalYangAmount();

	const auto minRaise = bestoff->price;
	const auto money = minRaise.GetTotalYangAmount();
	if(money < 1000)
		return money + 1000;

	const auto percentage = 1.0 + static_cast<double>(OFFLINESHOP_AUCTION_RAISE_PERCENTAGE) / 100.0;
	return static_cast<long long>(static_cast<double>(money) * percentage);
}

void MoveAndDestroyInstance(LPITEM item, BYTE window, WORD pos)
{
	auto ch = item->GetOwner();
	item->RemoveFromCharacter();
	item->SetWindow(window);
	item->SetCell(ch, pos);
	item->SetSkipSave(false);
	item->Save();
	ITEM_MANAGER::instance().FlushDelayedSave(item);
	item->SetSkipSave(true);
	M2_DESTROY_ITEM(item->RemoveFromCharacter());
}

bool CheckOfferPrice(const ikashop::TPriceInfo& offerPrice, const ikashop::TPriceInfo& itemPrice)
{
	const auto offer = offerPrice.GetTotalYangAmount();
	const auto item = itemPrice.GetTotalYangAmount();
	const auto minOffer = item * OFFLINESHOP_MIN_OFFER_PERCENTAGE / 100;
	return minOffer <= offer && offer < item;
}

namespace ikashop
{
	EVENTFUNC(func_offlineshop_update_duration)
	{
		auto& manager = ikashop::GetManager();
		manager.UpdateShopsDuration();
		manager.UpdateAuctionsDuration();
		manager.ClearSearchTimeMap();
		return OFFLINESHOP_DURATION_UPDATE_TIME;
	}

	ikashop::CShopManager& GetManager()
	{
		return ikashop::CShopManager::instance();
	}

	CShopManager::SHOP_HANDLE CShopManager::PutsNewShop(const TShopInfo& info)
	{
		

		auto& shop = m_mapShops[info.ownerid] = std::make_shared<ikashop::CShop>();
		shop->SetDuration(info.duration);
		shop->SetOwnerPID(info.ownerid);
		shop->SetName(info.name);
#ifdef EXTEND_IKASHOP_ULTIMATE
		shop->SetDecoration(info.decoration);
		shop->SetDecorationDuration(info.decoration_time);
		shop->SetLockIndex(info.lock_index);
#endif
#ifdef ENABLE_IKASHOP_ENTITIES
		shop->SetSpawn(info.spawn);
		CreateShopEntity(shop);
#endif
		return shop;
	}

	void CShopManager::PutsAuction(const TAuctionInfo& auction)
	{
		auto obj = std::make_shared<CAuction>();
		obj->SetInfo(auction);
		m_mapAuctions[auction.owner] = obj;
#ifdef ENABLE_IKASHOP_ENTITIES
	#ifdef EXTEND_IKASHOP_ULTIMATE
		CreateAuctionEntity(obj);
	#endif
#endif
	}

	void CShopManager::PutsAuctionOffer(const TAuctionOfferInfo& offer)
	{
		if(auto cache = GetAuctionByOwnerID(offer.ownerid))
			cache->AddOffer(offer);
	}

	CShopManager::SHOP_HANDLE CShopManager::GetShopByOwnerID(DWORD pid)
	{
		auto it = m_mapShops.find(pid);
		return it != m_mapShops.end() ? it->second : nullptr;
	}

	CShopManager::AUCTION_HANDLE CShopManager::GetAuctionByOwnerID(DWORD pid)
	{
		auto it = m_mapAuctions.find(pid);
		return it != m_mapAuctions.end() ? it->second : nullptr;
	}

	CShopManager::CShopManager()
	{
		auto* info = AllocEventInfo<event_info_data>();
		m_eventShopDuration = event_create(func_offlineshop_update_duration, info, OFFLINESHOP_DURATION_UPDATE_TIME);
	}

	CShopManager::~CShopManager()
	{
		Destroy();
	}

#ifdef EXTEND_IKASHOP_ULTIMATE
	long long CShopManager::GetAveragePrice(DWORD vnum, DWORD count)
	{
		static auto RoundedValue = [](auto value) {
			auto roundValue = 0;
			if (value >= 100'000'000)
				roundValue = 1'000'000;
			else if (value >= 10'000'000)
				roundValue = 100'000;
			else if (value >= 1'000'000)
				roundValue = 10'000;
			if (roundValue == 0)
				return value;
			return value - (value % roundValue);
		};

		auto cacheIter = m_priceAverageMap.find(vnum);
		if(cacheIter != m_priceAverageMap.end())
			return cacheIter->second * count;

		auto iter = m_saleHistoryMap.find(vnum);
		if(iter == m_saleHistoryMap.end())
			return 0;

		auto& sales = iter->second;
		const auto sellingCount = static_cast<long double>(sales.size());
		
		const auto currentTime = std::time(nullptr);

		double result = 0.0;
		for (auto& sale : sales)
		{
			if(sale.datetime + OFFLINESHOP_EXPIRING_SALE_HISTORY > currentTime)
			{
				const auto price = static_cast<long double>(sale.price) / sale.count;
				result += price / sellingCount;
			}
		}
		
		const auto value = RoundedValue(static_cast<long long>(result));
		m_priceAverageMap[vnum] = value;
		return value * count;
	}

	long long CShopManager::GetNormalizedPrice(DWORD vnum, DWORD count, long long price)
	{
		const auto avg = GetAveragePrice(vnum, count);
		if(avg == 0)
			return price;

		const auto minprice = avg / 100 * 50;
		const auto maxprice = avg / 100 * 150;
		return std::max(minprice, std::min(price, maxprice));
	}
#endif

	void CShopManager::Destroy()
	{
		// deleting event
		if(m_eventShopDuration)
			event_cancel(&m_eventShopDuration);
		m_eventShopDuration = nullptr;

		// clearing containers
		m_mapOffersByBuyer.clear();
		m_mapSafeboxs.clear();
		m_mapShops.clear();
		m_mapOffer.clear();
	}

	void CShopManager::_RemoveShopOffers(SHOP_HANDLE shop)
	{
		// storing all buyers's pids
		std::set<DWORD> buyers;

		// iterating over shop's offers
		for (auto& iter : shop->GetOffers())
		{
			auto& offer = iter.second;

			// for each offer removing from buyer's offer list
			DWORD buyer = offer->buyerid;
			buyers.emplace(buyer);

			// searching buyer into map
			auto it = m_mapOffersByBuyer.find(buyer);
			if (it != m_mapOffersByBuyer.end())
			{
				// removing offer from vector
				auto& offers = it->second;
				std::erase_if(offers, [=](auto&& boffer) { return boffer->offerid == offer->offerid; });
			}

			// removing offer from offer map
			m_mapOffer.erase(offer->offerid);
		}

		// refreshing offers page for buyers
		for (auto& pid : buyers)
			if (auto chBuyer = CHARACTER_MANAGER::instance().FindByPID(pid))
				if (chBuyer->IsLookingIkarusShopOfferList())
					RecvOfferListRequestPacket(chBuyer);
	}

#ifdef ENABLE_IKASHOP_ENTITIES
#ifdef EXTEND_IKASHOP_PRO
	void CShopManager::_UpdateEntity(SHOP_HANDLE shop)
	{
		DestroyShopEntity(shop);
		CreateShopEntity(shop);
	}
#endif

	void CShopManager::CreateShopEntity(SHOP_HANDLE shop)
	{
		auto& spawn = shop->GetSpawn();
		if (auto sectree = SECTREE_MANAGER::Instance().Get(spawn.map, spawn.x, spawn.y))
		{
			auto entity = std::make_shared<ShopEntity>();
			entity->SetShopName(shop->GetName());
			entity->SetMapIndex(spawn.map);
			entity->SetXYZ(spawn.x, spawn.y, 0);
			entity->SetShop(shop);
#ifdef EXTEND_IKASHOP_ULTIMATE
			entity->SetShopType(shop->GetDecoration());
#endif
			sectree->InsertEntity(entity.get());
			entity->UpdateSectree();
			shop->SetEntity(entity);

			m_entityByVID[entity->GetVID()] = entity;
		}
	}

	void CShopManager::DestroyShopEntity(SHOP_HANDLE shop)
	{
		if(auto entity = shop->GetEntity())
		{
			m_entityByVID.erase(entity->GetVID());
			if (entity->GetSectree())
			{
				entity->ViewCleanup();
				entity->GetSectree()->RemoveEntity(entity.get());
			}
			entity->Destroy();
		}
	}

	void CShopManager::EncodeInsertShopEntity(const ShopEntity& shop, LPCHARACTER ch)
	{
		if (!ch->GetDesc())
			return;

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader	= SUBHEADER_GC_INSERT_SHOP_ENTITY;
		pack.size = sizeof(pack)+ sizeof(TSubPacketGCInsertShopEntity);

		const auto& pos = shop.GetXYZ();

		TSubPacketGCInsertShopEntity subpack{};
		subpack.vid = shop.GetVID();
		subpack.type = shop.GetShopType();
		subpack.x = pos.x;
		subpack.y = pos.y;
		subpack.z = pos.z;
		str_to_cstring(subpack.name, shop.GetShopName());

		ch->GetDesc()->BufferedPacket(&pack, sizeof(pack));
		ch->GetDesc()->Packet(&subpack, sizeof(subpack));
	}

	void CShopManager::EncodeRemoveShopEntity(const ShopEntity& shop, LPCHARACTER ch)
	{
		if (!ch->GetDesc())
			return;

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader	= SUBHEADER_GC_REMOVE_SHOP_ENTITY;
		pack.size = sizeof(pack)+ sizeof(TSubPacketGCRemoveShopEntity);

		TSubPacketGCRemoveShopEntity subpack{};
		subpack.vid = shop.GetVID();

		ch->GetDesc()->BufferedPacket(&pack, sizeof(pack));
		ch->GetDesc()->Packet(&subpack, sizeof(subpack));
	}

#ifdef EXTEND_IKASHOP_ULTIMATE
	void CShopManager::CreateAuctionEntity(AUCTION_HANDLE auction)
	{
		auto& spawn = auction->GetSpawn();
		if (auto sectree = SECTREE_MANAGER::Instance().Get(spawn.map, spawn.x, spawn.y))
		{
			auto entity = std::make_shared<ShopEntity>();
			entity->SetShopName(auction->GetInfo().ownername);
			entity->SetShopType(999);
			entity->SetMapIndex(spawn.map);
			entity->SetXYZ(spawn.x, spawn.y, 0);
			sectree->InsertEntity(entity.get());
			entity->UpdateSectree();
			entity->SetAuction(auction);
			auction->SetEntity(entity);
			m_entityByVID[entity->GetVID()] = entity;
		}
	}

	void CShopManager::DestroyAuctionEntity(AUCTION_HANDLE auction)
	{
		if (auto entity = auction->GetEntity())
		{
			m_entityByVID.erase(entity->GetVID());
			if (entity->GetSectree())
			{
				entity->ViewCleanup();
				entity->GetSectree()->RemoveEntity(entity.get());
			}
			entity->Destroy();
		}
	}
#endif
#endif

	CShopManager::SAFEBOX_HANDLE CShopManager::GetShopSafeboxByOwnerID(DWORD pid)
	{
		auto it = m_mapSafeboxs.find(pid);
		return it != m_mapSafeboxs.end() ? it->second : nullptr;
	}

	//offers
	bool CShopManager::PutsNewOffer(OFFER_HANDLE offer)
	{
		// registering offer by buyer
		auto& offers = m_mapOffersByBuyer[offer->buyerid];
		for(auto& boffer : offers)
			if(boffer->offerid == offer->offerid)
				return false;

		// adding offer to buyer list
		offers.emplace_back(offer);
		// registering offer by offer id
		m_mapOffer[offer->offerid] = offer;
		return true;
	}

#ifdef EXTEND_IKASHOP_ULTIMATE
	bool CShopManager::UsePrivateShopKey(LPCHARACTER ch, LPITEM item)
	{
		if(!CheckCharacterActions(ch) || !ch->GetIkarusShop() || item->IsExchanging() || item->isLocked())
			return false;

		auto shop = ch->GetIkarusShop();
		if(shop->GetLockIndex() >= CShop::PAGE_COUNT * CShop::GRID_SIZE){
			SendPopupMessage(ch, "IKASHOP_ULTIMATE_PRIVATE_SHOP_KEY_SHOP_FULL_UNLOCKED");
			return false;
		}

		shop->SetLockIndex(shop->GetLockIndex() + 1);

		TPacketDGNewIkarusShop pack{};
		pack.bSubHeader = ikashop::SUBHEADER_GD_SHOP_UNLOCK_CELL;

		TEMP_BUFFER buffer;
		buffer.write(pack);
		buffer.write(ch->GetPlayerID());

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buffer.read_peek(), buffer.size());
		return true;
	}

	bool CShopManager::UseSearchShopPremium(LPCHARACTER ch, LPITEM item)
	{
		if(!CheckCharacterActions(ch) || item->IsExchanging() || item->isLocked())
			return false;

		if(ch->FindAffect(AFFECT_SEARCH_SHOP_PREMIUM) != nullptr){
			SendPopupMessage(ch, "IKASHOP_ULTIMATE_SEARCH_SHOP_PREMIUM_ALREADY_ACTIVE");
			return false;
		}

		ch->AddAffect(AFFECT_SEARCH_SHOP_PREMIUM, POINT_NONE, 0, AFF_NONE, OFFSHOP_SEARCH_PREMIUM_TIME, 0, false);
		return true;
	}
#endif

	//db packets exchanging
	void CShopManager::SendShopBuyDBPacket(DWORD buyerid, DWORD ownerid, DWORD itemid, bool success)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_BUY_ITEM;

		TSubPacketGDBuyItem subpack{};
		subpack.guestid = buyerid;
		subpack.ownerid = ownerid;
		subpack.itemid = itemid;
		subpack.success = success;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		
		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopBuyDBPacket(DWORD buyerid, DWORD ownerid,DWORD itemid)
	{
		// searching shop
		auto shop = GetShopByOwnerID(ownerid);
		if(!shop)
			return false;
		
		// searching item
		auto shopItem = shop->GetItem(itemid);
		if(!shopItem)
			return false;

		if (auto ch = CHARACTER_MANAGER::instance().FindByPID(buyerid))
		{
			

			auto itemInstance = shopItem->CreateItem();
			if (!itemInstance)
			{
				sys_err("cannot create item ( itemid %u , vnum %u, shop owner %u, buyer %u )", itemid, shopItem->GetInfo().vnum, ownerid, buyerid );
				return false;
			}

			TItemPos pos;
			if (!ch->CanTakeInventoryItem(itemInstance, &pos))
			{
				itemInstance->SetSkipSave(true);
				M2_DESTROY_ITEM(itemInstance);

				SendShopSafeboxAddItemDBPacket(ch->GetPlayerID(), *shopItem);
				SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_SAFEBOX_RECV_ITEM");
			}

			else
			{
				itemInstance->AddToCharacter(ch, pos); 
				itemInstance->SetSkipSave(false);
				itemInstance->Save();
				ITEM_MANAGER::instance().FlushDelayedSave(itemInstance);
			}

			shop->BuyItem(shopItem->GetID());
			SendSearchResultDeleteClientPacket(ch, itemid);
		}

		else
		{
			
			shop->BuyItem(shopItem->GetID());
		}

		return true;
	}

	void CShopManager::SendShopEditItemDBPacket(DWORD ownerid, DWORD itemid, const TPriceInfo& price)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_EDIT_ITEM;

		TSubPacketGDEditItem subpack{};
		subpack.ownerid = ownerid;
		subpack.itemid = itemid;
		subpack.price = price;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		
		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopEditItemClientPacket(LPCHARACTER ch, DWORD itemid, const TPriceInfo& price)
	{
		if(!ch || !ch->GetIkarusShop())
			return false;

		if(!CheckGMLevel(ch))
			return false;

		if(!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_EDIT_ITEM))
			return false;

		if (!IsGoodSalePrice(price))
			return false;

		auto shop = ch->GetIkarusShop();
		auto shopItem = shop->GetItem(itemid);
		if(!shopItem)
			return false;

		// patch with warp check
		ch->SetIkarusShopUseTime();

#ifndef ENABLE_CHEQUE_SYSTEM
		auto& itemPrice = shopItem->GetPrice();
		if(price.yang == itemPrice.yang)
			return true;
#endif
		SendShopEditItemDBPacket(shop->GetOwnerPID(), itemid, price);
		return true;
	}

	void CShopManager::SendShopRemoveItemDBPacket(DWORD ownerid, DWORD itemid)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_REMOVE_ITEM;

		TSubPacketGDRemoveItem subpack{};
		subpack.ownerid = ownerid;
		subpack.itemid = itemid;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		
		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopRemoveItemDBPacket(DWORD ownerid, DWORD itemid, bool requester)
	{
		// searching shop
		auto shop = GetShopByOwnerID(ownerid);
		if(!shop)
			return false;

		// searching item
		auto shopItem = shop->GetItem(itemid);
		if(!shopItem)
			return false;

		// removing from shop
		shop->RemoveItem(itemid);

		// checking for requester
		if(requester)
		{
			static auto pos = TItemPos{};
			// checking if character can obtain the item
			auto ch = shop->FindOwnerCharacter();
			auto item = shopItem->CreateItem();

			if(!item || !ch || !ch->CanTakeInventoryItem(item, &pos))
			{
				// cleaning up item
				if(item){
					item->SetSkipSave(true);
					M2_DESTROY_ITEM(item);
				}

				// sending item to safebox
				SendShopSafeboxAddItemDBPacket(ownerid, *shopItem);
			}

			else
			{
				item->AddToCharacter(ch, pos);
				item->SetSkipSave(false);
				item->Save();
				ITEM_MANAGER::instance().FlushDelayedSave(item);
			}
		}

		return true;
	}

	void CShopManager::SendShopAddItemDBPacket(DWORD ownerid, const TShopItem& iteminfo)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_ADD_ITEM;

		TSubPacketGDAddItem subpack{};
		subpack.ownerid = ownerid;
		subpack.sitem = iteminfo;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		
		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopAddItemDBPacket(DWORD ownerid, const TShopItem& iteminfo)
	{
		// searching shop
		auto shop = GetShopByOwnerID(ownerid);
		if(!shop)
			return false;

		// adding item to shop
		
		return shop->AddItem(iteminfo);
	}

	//SHOPS
	void CShopManager::SendShopForceCloseDBPacket(DWORD pid, bool gm = false)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_SHOP_FORCE_CLOSE;

		TSubPacketGDShopForceClose subpack{};
		subpack.ownerid = pid;
		subpack.gm = gm;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		
		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopForceCloseDBPacket(DWORD pid)
	{
		// getting shop
		auto shop = GetShopByOwnerID(pid);
		if(!shop)
			return false;

		// kicking out guests
		shop->KickAllGuests();
		_RemoveShopOffers(shop);
#ifdef ENABLE_IKASHOP_ENTITIES
		DestroyShopEntity(shop);
#endif
		shop->Clear();

		// searching character
		if (auto ch = CHARACTER_MANAGER::instance().FindByPID(pid)) {
			ch->SetIkarusShop(nullptr);
			SendShopOpenMyShopNoShopClientPacket(ch);
		}

		m_mapShops.erase(shop->GetOwnerPID());
		return true;
	}

	void CShopManager::SendShopLockBuyItemDBPacket(LPCHARACTER buyer, DWORD ownerid, ITEM_HANDLE item, long long TotalPriceSeen) //patch seen price check
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader = SUBHEADER_GD_BUY_LOCK_ITEM;

		TSubPacketGDLockBuyItem subpack{};
#ifdef EXTEND_IKASHOP_ULTIMATE
		subpack.accountid = buyer->GetDesc()->GetAccountTable().id;
		subpack.normalizedPrice = GetNormalizedPrice(item->GetInfo().vnum, item->GetInfo().count, TotalPriceSeen);
#endif
		subpack.guestid = buyer->GetPlayerID();
		subpack.ownerid = ownerid;
		subpack.itemid = item->GetID();
		subpack.seenprice = TotalPriceSeen;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		
		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopLockedBuyItemDBPacket(DWORD buyerid, DWORD ownerid,DWORD itemid)
	{
		auto SendDBFeedback = [&](auto success){
			SendShopBuyDBPacket(buyerid, ownerid, itemid, success);
			return success;
		};

		auto shop = GetShopByOwnerID(ownerid);
		auto ch	= CHARACTER_MANAGER::instance().FindByPID(buyerid);

		if(!ch || !shop)
			return SendDBFeedback(false);

		auto item = shop->GetItem(itemid);
		if(!item || !item->CanBuy(ch))
			return SendDBFeedback(false);

		const auto& price = item->GetPrice();
		ch->PointChange(POINT_GOLD, -price.yang);
#ifdef ENABLE_CHEQUE_SYSTEM
		ch->PointChange(POINT_CHEQUE, -price.cheque);
#endif
		return SendDBFeedback(true);
	}

	bool CShopManager::RecvShopExpiredDBPacket(DWORD pid)
	{
		auto shop = GetShopByOwnerID(pid);
		if (!shop)
			return false;

		// kicking out guests
		shop->KickAllGuests();
		_RemoveShopOffers(shop);

#ifdef ENABLE_IKASHOP_ENTITIES
		DestroyShopEntity(shop);
#endif

#ifndef EXTEND_IKASHOP_PRO
		if (auto ch = CHARACTER_MANAGER::instance().FindByPID(pid))
			ch->SetIkarusShop(NULL);

		shop->Clear();
		m_mapShops.erase(m_mapShops.find(shop->GetOwnerPID()));
#else
		shop->SetDuration(0);
		shop->RefreshToOwner();
#endif
		return true;
	}

	void CShopManager::SendShopCreateNewDBPacket(const TShopInfo& shop)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_SHOP_CREATE_NEW;

		TSubPacketGDShopCreateNew subpack{};
		subpack.shop = shop;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopCreateNewDBPacket(const TShopInfo& info)
	{
		// checking shop already exists
		if(m_mapShops.contains(info.ownerid))
			return false;

		// making shop
		auto shop = PutsNewShop(info);

		if (auto owner = shop->FindOwnerCharacter()){
			owner->SetIkarusShop(shop);
			SendShopOpenMyShopClientPacket(owner);
		}

		m_mapShops[info.ownerid] = shop;
		return true;
	}

	void CShopManager::SendShopChangeNameDBPacket(DWORD ownerid, const char* name)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_SHOP_CHANGE_NAME;

		TSubPacketGDShopChangeName subpack{};
		subpack.ownerid	= ownerid;
		strncpy(subpack.name, name, sizeof(subpack.name));

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		
		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopChangeNameDBPacket(DWORD ownerid, const char* name)
	{
		auto shop = GetShopByOwnerID(ownerid);
		if(!shop)
			return false;

		shop->SetName(name);
		shop->RefreshToOwner();

#ifdef ENABLE_IKASHOP_ENTITIES
#ifdef EXTEND_IKASHOP_PRO
		_UpdateEntity(shop);
#endif
#endif		
		return true;
	}

	//OFFER
	void CShopManager::SendShopOfferNewDBPacket(const TOfferInfo& offer, ITEM_HANDLE item)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_OFFER_CREATE;

		TSubPacketGDOfferCreate subpack{};
		subpack.ownerid = offer.ownerid;
		subpack.itemid = offer.itemid;
		subpack.offer = offer;
		subpack.vnum = item->GetInfo().vnum;
		subpack.count = item->GetInfo().count;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		
		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopOfferNewDBPacket(const TOfferInfo& offer)
	{
		// searching shop
		auto shop = GetShopByOwnerID(offer.ownerid);
		if(!shop)
			return false;

		// making shared offer
		auto soffer = std::make_shared<TOfferInfo>(offer);

		// registering offer into shop
		
		if(!shop->AddOffer(soffer))
			return false;

		// registering offer into map
		if(!PutsNewOffer(soffer))
			return false;

		// notify buyer
		if(auto ch = CHARACTER_MANAGER::instance().FindByPID(offer.buyerid))
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_PRIVATE_OFFER_CREATED");

		// updating counters
		if (auto ch = CHARACTER_MANAGER::instance().FindByPID(offer.ownerid))
			if (ch->IsLookingShopOwner())
				SendBoardCountersClientPacket(ch);

		return true;
	}

	void CShopManager::SendShopOfferNotifiedDBPacket(DWORD offerid, DWORD ownerid)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_OFFER_NOTIFIED;

		TSubPacketGDOfferNotified subpack{};
		subpack.offerid	= offerid;
		subpack.ownerid	= ownerid;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopOfferNotifiedDBPacket(DWORD offerid, DWORD ownerid)
	{
		// searching shop
		auto shop = GetShopByOwnerID(ownerid);
		if(!shop)
			return false;

		// updating offer
		for (auto& iter : shop->GetOffers())
		{
			auto& offer = iter.second;

			if (offer->offerid == offerid)
			{				
				offer->notified = true;
				break;
			}
		}

		return true;
	}

	void CShopManager::SendShopOfferAcceptDBPacket(const TOfferInfo& offer)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_OFFER_ACCEPT;

		TSubPacketGDOfferNotified subpack{};
		subpack.ownerid = offer.ownerid;
		subpack.offerid = offer.offerid;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	void CShopManager::SendShopOfferCancelDBPacket(const TOfferInfo& offer, DWORD requester)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_OFFER_CANCEL;

		TSubPacketGDOfferCancel subpack{};
		subpack.ownerid = offer.ownerid;
		subpack.offerid = offer.offerid;
		subpack.requester = requester;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopOfferCancelDBPacket(DWORD offerid, DWORD ownerid, bool removing) //ikashop-updated 05/08/19
	{
		// searching offer handle
		auto it = m_mapOffer.find(offerid);

		// checking it's found
		if(it == m_mapOffer.end())
			return false;

		// copying handle
		auto currentOffer = it->second;

		// removing from map offer by id
		m_mapOffer.erase(it);

		// removing offer from shop
		if(auto shop = GetShopByOwnerID(ownerid))
			shop->RemoveOffer(offerid);

		// removing offer from map by buyer
		const auto buyiter = m_mapOffersByBuyer.find(currentOffer->buyerid);
		if (buyiter != m_mapOffersByBuyer.end())
		{
			auto& vec = buyiter->second;
			std::erase_if(vec, [&](auto&& offer){ return offer->offerid == offerid; });

			// removing from map if empty
			if (buyiter->second.empty())
				m_mapOffersByBuyer.erase(buyiter);
		}

		// refreshing owner view
		if (!removing)
		{
			if(auto chOwner = CHARACTER_MANAGER::Instance().FindByPID(ownerid)){
				if(chOwner->IsLookingIkarusShopOfferList())
					RecvOfferListRequestPacket(chOwner);
				// updating counters
				if (chOwner->IsLookingShopOwner())
					SendBoardCountersClientPacket(chOwner);
			}
		}

		// refreshing buyer view
		if(auto chBuyer = CHARACTER_MANAGER::Instance().FindByPID(currentOffer->buyerid))
			if (chBuyer->IsLookingIkarusShopOfferList())
				RecvOfferListRequestPacket(chBuyer);

		return true;
	}

	bool CShopManager::RecvShopOfferAcceptDBPacket(DWORD offerid, DWORD ownerid)
	{
		// searching shop
		auto shop = GetShopByOwnerID(ownerid);
		if(!shop)
			return false;

		// searching offer handle
		auto it = m_mapOffer.find(offerid);
		if(it == m_mapOffer.end())
			return false;

		// copying handle & removing from map
		auto offer = it->second;
		m_mapOffer.erase(it);

		// accepting the offer		
		shop->AcceptOffer(offer);

		//removing offer from offer by buyer
		auto buyIter = m_mapOffersByBuyer.find(offer->buyerid);
		if (buyIter != m_mapOffersByBuyer.end())
		{
			// removing using find which interrupts the search at first match
			auto& offerVec = buyIter->second;
			auto offerIt = std::find_if(offerVec.begin(), offerVec.end(), [&](auto&& current) { return current->offerid == offerid; });
			if(offerIt != offerVec.end())
				offerVec.erase(offerIt);

			// removing from map if vector is empty
			if (offerVec.empty())
				m_mapOffersByBuyer.erase(buyIter);
		}
		
		// notifying to buyer
		if(auto chBuyer = CHARACTER_MANAGER::Instance().FindByPID(offer->buyerid))
			if (chBuyer->IsLookingIkarusShopOfferList())
				RecvOfferListRequestPacket(chBuyer);

		// notifying to owner
		if(auto chOwner = CHARACTER_MANAGER::Instance().FindByPID(ownerid)){
			if (chOwner->IsLookingIkarusShopOfferList())
				RecvOfferListRequestPacket(chOwner);
			// updating counters
			if (chOwner->IsLookingShopOwner())
				SendBoardCountersClientPacket(chOwner);
		}

		return true;
	}

	bool CShopManager::RecvShopSafeboxAddItemDBPacket(DWORD ownerid, DWORD itemid, const TShopPlayerItem& item)
	{
		auto ch = CHARACTER_MANAGER::instance().FindByPID(ownerid);
		auto safebox = ch && ch->GetIkarusShopSafebox() ? ch->GetIkarusShopSafebox() : GetShopSafeboxByOwnerID(ownerid);
		if(!safebox)
			return false;

		safebox->AddItem(item);
		if(ch && ch->GetIkarusShopSafebox() && ch->GetDesc())
		{
			if(auto insertedItem = safebox->GetItem(itemid))
			{
				TSubPacketGCShoppingSafeboxAddItem subpack{};
				subpack.item = static_cast<const TShopPlayerItem&>(insertedItem->GetInfo());
			
				TPacketGCNewIkarusShop pack{};
				pack.header = HEADER_GC_NEW_OFFLINESHOP;
				pack.size = sizeof(pack) + sizeof(subpack);
				pack.subheader = SUBHEADER_GC_SHOP_SAFEBOX_ADD_ITEM;

				ch->GetDesc()->BufferedPacket(&pack, sizeof(pack));
				ch->GetDesc()->Packet(&subpack, sizeof(subpack));
			}
		}

		if (ch && ch->IsLookingShopOwner())
			SendBoardCountersClientPacket(ch);

		
		return true;
	}

	bool CShopManager::SendShopSafeboxAddItemDBPacket(DWORD ownerid, const CShopItem& item) {
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader = SUBHEADER_GD_SAFEBOX_ADD_ITEM;


		TSubPacketGDSafeboxAddItem subpack{};
		subpack.ownerid = ownerid;
		subpack.itemid = item.GetID();
		subpack.item = static_cast<const TShopPlayerItem&>(item.GetInfo());

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
		return true;
	}

	bool CShopManager::RecvShopSafeboxAddValutesDBPacket(DWORD ownerid, const TValutesInfo& valute)
	{
		auto ch = CHARACTER_MANAGER::instance().FindByPID(ownerid);
		auto safebox = ch && ch->GetIkarusShopSafebox() ? ch->GetIkarusShopSafebox() : GetShopSafeboxByOwnerID(ownerid);

		if(!safebox)
			return false;

		safebox->AddValute(valute);
		if(ch && ch->GetIkarusShopSafebox())
			safebox->RefreshToOwner(ch);

		if (ch && ch->IsLookingShopOwner())
			SendBoardCountersClientPacket(ch);

		return true;
	}

	void CShopManager::SendShopSafeboxGetItemDBPacket(DWORD ownerid, DWORD itemid)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_SAFEBOX_GET_ITEM;

		TSubPacketGDSafeboxGetItem subpack{};
		subpack.ownerid	= ownerid;
		subpack.itemid	= itemid;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		
		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	void CShopManager::SendShopSafeboxGetValutesDBPacket(DWORD ownerid, const TValutesInfo& valutes)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader	= SUBHEADER_GD_SAFEBOX_GET_VALUTES;

		TSubPacketGDSafeboxGetValutes subpack{};
		subpack.ownerid	= ownerid;
		subpack.valute = valutes;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
	}

	bool CShopManager::RecvShopSafeboxLoadDBPacket(DWORD ownerid, const TValutesInfo& valute, const std::vector<TShopPlayerItem>& items)
	{
		// resetting/inserting safebox
		auto& safebox = m_mapSafeboxs[ownerid] = std::make_shared<CShopSafebox>();
		safebox->SetValuteAmount(valute);

		// adding items
		for (auto& item : items)
			safebox->AddItem(item);

		return true;
	}

	bool CShopManager::RecvShopSafeboxExpiredItemDBPacket(DWORD ownerid, DWORD itemid) {
		if (auto data = GetShopSafeboxByOwnerID(ownerid)) 
		{
			data->RemoveItem(itemid);
			data->RefreshToOwner();
			// updating counters
			if(auto ch = CHARACTER_MANAGER::instance().FindByPID(ownerid))
				if (ch->IsLookingShopOwner())
					SendBoardCountersClientPacket(ch);
		} 
		
		return true;
	}

	bool CShopManager::RecvShopSafeboxGetItemConfirm(DWORD ownerid, DWORD itemid)
	{
		auto _SendFeedbackToDB = [&](auto result){
			TPacketGDNewIkarusShop pack{};
			pack.bSubHeader = ikashop::SUBHEADER_GD_SAFEBOX_GET_TIME_FEEDBACK;

			ikashop::TSubPacketGDSafeboxGetItemFeedback subpack{};
			subpack.ownerid = ownerid;
			subpack.itemid = itemid;
			subpack.result = result;

			TEMP_BUFFER buffer;
			buffer.write(&pack, sizeof(pack));
			buffer.write(&subpack, sizeof(subpack));
			db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buffer.read_peek(), buffer.size());
			return result;
		};

		// searching safebox
		auto safebox = GetShopSafeboxByOwnerID(ownerid);
		if(!safebox)
			return _SendFeedbackToDB(false);

		// searching item
		auto item = safebox->GetItem(itemid);
		if(!item)
			return _SendFeedbackToDB(false);
		
		// searching character
		auto ch = CHARACTER_MANAGER::instance().FindByPID(ownerid);
		if(!ch)
			return _SendFeedbackToDB(false);

		// creating item
		auto itemInstance = item->CreateItem();
		if(!itemInstance)
			return _SendFeedbackToDB(false);

		// checking item can be moved to inventory
		static TItemPos pos{};
		if(!ch->CanTakeInventoryItem(itemInstance, &pos))
		{
			itemInstance->SetSkipSave(true);
			M2_DESTROY_ITEM(itemInstance);
			return _SendFeedbackToDB(false);
		}

		// giving item to character
		if (!safebox->RemoveItem(itemid))
		{
			itemInstance->SetSkipSave(true);
			M2_DESTROY_ITEM(itemInstance);
			return _SendFeedbackToDB(false);
		}

		if (ch->IsLookingShopOwner())
			SendBoardCountersClientPacket(ch);

		// refreshing to owner
		if(auto ch = CHARACTER_MANAGER::instance().FindByPID(ownerid))
		{
			TPacketGCNewIkarusShop pack{};
			pack.header = HEADER_GC_NEW_OFFLINESHOP;
			pack.subheader = SUBHEADER_GC_SHOP_SAFEBOX_REMOVE_ITEM;
			pack.size = sizeof(pack) + sizeof(TSubPacketGCShopSafeboxRemoveItem);

			TSubPacketGCShopSafeboxRemoveItem subpack{};
			subpack.itemid = itemid;

			if(ch->GetDesc())
			{
				ch->GetDesc()->BufferedPacket(&pack, sizeof(pack));
				ch->GetDesc()->Packet(&subpack, sizeof(subpack));
			}
		}

		itemInstance->AddToCharacter(ch, pos);
		itemInstance->SetSkipSave(false);
		itemInstance->Save();
		ITEM_MANAGER::Instance().FlushDelayedSave(itemInstance);
		return _SendFeedbackToDB(true);
	}

	//AUCTION
	void CShopManager::SendAuctionCreateDBPacket(const TAuctionInfo& auction)
	{
		

		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader = SUBHEADER_GD_AUCTION_CREATE;

		TSubPacketGDAuctionCreate subpack{};
		subpack.auction = auction;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP , 0 , buff.read_peek() , buff.size());
	}

	void CShopManager::SendAuctionAddOfferDBPacket(const TAuctionOfferInfo& offer)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader = SUBHEADER_GD_AUCTION_ADD_OFFER;

		TSubPacketGDAuctionAddOffer subpack{};
		subpack.offer = offer;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP , 0 , buff.read_peek() , buff.size());
	}

	bool CShopManager::RecvAuctionCreateDBPacket(const TAuctionInfo& auction)
	{
		

		//check if exist
		if(m_mapAuctions.find(auction.owner) != m_mapAuctions.end())
			return false;

		//set info
		auto& obj = m_mapAuctions[auction.owner] = std::make_shared<CAuction>();
		obj->SetInfo(auction);

#ifdef ENABLE_IKASHOP_ENTITIES
#ifdef EXTEND_IKASHOP_ULTIMATE
		CreateAuctionEntity(obj);
#endif
#endif

		//check about owner
		if (auto ch = CHARACTER_MANAGER::instance().FindByPID(auction.owner))
		{
			ch->SetIkarusAuction(obj);
			SendAuctionOpenAuctionClientPacket(ch, *obj);
		}

		return true;
	}

	bool CShopManager::RecvAuctionAddOfferDBPacket(const TAuctionOfferInfo& offer)
	{
		

		//check if exists
		auto auction = GetAuctionByOwnerID(offer.ownerid);
		if(!auction)
			return false;

		//adding offer
		auction->AddOffer(offer);

		//adding 1 minute time when new offer is done in the last minute
		if (auction->GetInfo().duration == 0)
			auction->IncreaseDuration();

		// updating counters
		if (auto ch = CHARACTER_MANAGER::instance().FindByPID(offer.ownerid))
			if (ch->IsLookingShopOwner())
				SendBoardCountersClientPacket(ch);

		return true;
	}

	//updated 30/09/19
	bool CShopManager::RecvAuctionExpiredDBPacket(DWORD id)
	{
		// searching auction inside map
		auto it = m_mapAuctions.find(id);
		if (it == m_mapAuctions.end())
			return false;

#ifdef ENABLE_IKASHOP_ENTITIES
#ifdef EXTEND_IKASHOP_ULTIMATE
		DestroyAuctionEntity(it->second);
#endif
#endif

		// copying guest list & removing auction from auctionlist
		auto guests{ it->second->GetGuests() };
		m_mapAuctions.erase(it);

		// kicking auction guests
		for (auto& guest : guests){
			if(auto chGuest = CHARACTER_MANAGER::instance().FindByPID(guest))
			{
				chGuest->SetIkarusAuctionGuest(NULL);
				RecvAuctionListRequestClientPacket(chGuest);
			}
		}

		// setting auction to null on owner character
		if (auto owner = CHARACTER_MANAGER::instance().FindByPID(id)){
			owner->SetIkarusAuction(NULL);
			// updating counters
			if (owner->IsLookingShopOwner())
				SendBoardCountersClientPacket(owner);
		}


		return true;
	}

	bool CShopManager::RecvAuctionOfferSeenDBPacket(DWORD ownerid, DWORD buyerid, long long price)
	{
		if(auto auction = GetAuctionByOwnerID(ownerid))
		{
			for(auto& offer : auction->GetOffers())
			{
				if(offer->buyerid == buyerid && offer->ownerid == ownerid && offer->price.yang == price){
					offer->seenflag = true;
					break;
				}
			}

			if(auto ch = CHARACTER_MANAGER::instance().FindByPID(ownerid))
				if(ch->IsLookingShopOwner())
					SendBoardCountersClientPacket(ch);
		}

		return true;
	}

#ifdef EXTEND_IKASHOP_PRO
	bool CShopManager::RecvNotificationLoadDBPacket(DWORD owner, const std::vector<TNotificationInfo>& notifications)
	{
		m_notificationMap[owner] = notifications;
		return true;
	}

	bool CShopManager::RecvNotificationForwardDBPacket(DWORD owner, const TNotificationInfo& notification)
	{
		if(auto ch = CHARACTER_MANAGER::instance().FindByPID(owner))
		{
			auto& notifications = m_notificationMap[owner];
			notifications.emplace_back(notification);
			if (ch->IsLookingNotificationList())
				SendNotificationListClientPacket(ch);
			if(ch->IsLookingShopOwner())
				SendBoardCountersClientPacket(ch);
		}
		return true;
	}

	void CShopManager::SendShopRestoreDurationDBPacket(DWORD owner)
	{
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader = ikashop::SUBHEADER_GD_SHOP_RESTORE_DURATION;

		TEMP_BUFFER buffer;
		buffer.write(&pack, sizeof(pack));
		buffer.write(&owner, sizeof(owner));

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buffer.read_peek(), buffer.size());
	}
	
	bool CShopManager::RecvShopRestoreDurationDBPacket(DWORD owner)
	{
		if(auto shop = GetShopByOwnerID(owner))
		{
#ifdef EXTEND_IKASHOP_ULTIMATE
			auto oldDuration = shop->GetDuration();
			shop->ChangeDuration(OFFLINESHOP_DURATION_MAX_MINUTES);
#else
			shop->SetDuration(OFFLINESHOP_DURATION_MAX_MINUTES);
			shop->RefreshToOwner();
#endif

#ifdef EXTEND_IKASHOP_ULTIMATE
			if(oldDuration == 0)
#endif
			{
				DestroyShopEntity(shop);
				CreateShopEntity(shop);
			}
		}

		return true;
	}
#endif

#ifdef EXTEND_IKASHOP_ULTIMATE
	void CShopManager::RecvShopRegisterSaleHistoryDBPacket(const TSaleHistory& sale)
	{
		const auto currentTime = std::time(nullptr);
		auto& vector = m_saleHistoryMap[sale.vnum];
		std::erase_if(vector, [&currentTime](auto&& sale){ return sale.datetime + OFFLINESHOP_EXPIRING_SALE_HISTORY < currentTime; });
		vector.emplace_back(sale);
		m_priceAverageMap.erase(sale.vnum);
	}

	void CShopManager::RecvShopDecorationSetDBPacket(DWORD owner, int index, int remainTime)
	{
		if(auto shop = GetShopByOwnerID(owner))
		{
			shop->SetDecoration(index);
			shop->SetDecorationDuration(remainTime);
			shop->RefreshToOwner();
			_UpdateEntity(shop);
		}
	}

	void CShopManager::RecvShopMoveItemDBPacket(DWORD owner, DWORD itemid, int destCell)
	{
		// searching shop
		auto shop = GetShopByOwnerID(owner);
		if(!shop)
			return;

		// moving item and refreshing views
		shop->MoveItem(itemid, destCell);
	}

	void CShopManager::RecvShopUnlockCellDBPacket(DWORD owner, int lockIndex)
	{
		// searching shop
		auto shop = GetShopByOwnerID(owner);
		if (!shop)
			return;

		shop->ChangeLockIndex(lockIndex);
	}
#endif

#ifdef ENABLE_IKASHOP_ENTITIES
#ifdef EXTEND_IKASHOP_PRO
	void CShopManager::RecvMoveShopEntityDBPacket(DWORD owner, const TShopSpawn& spawn)
	{
		if(auto shop = GetShopByOwnerID(owner))
		{
			shop->SetSpawn(spawn);
			_UpdateEntity(shop);
		}
	}
#endif
#endif

	//client packets exchanging
	bool CShopManager::RecvShopCreateNewClientPacket(LPCHARACTER ch)
	{
		if(!ch)
			return false;

		if (!CheckGMLevel(ch))
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_CREATE_NEW))
			return false;

		if (!CheckCharacterActions(ch))
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_DO_NOW");
			return true;
		}

		// patch with warp check
		ch->SetIkarusShopUseTime();

		if(ch->GetGold() < OFFLINESHOP_RESTORE_DURATION_COST)
			return true;

#ifdef EXTEND_IKASHOP_PRO
#ifndef EXTEND_IKASHOP_ULTIMATE
		if (ch->GetIkarusShop() && ch->GetIkarusShop()->GetDuration() != 0)
			return true;
#endif
#endif
#ifdef ENABLE_IKASHOP_ENTITIES
		if(!ch->GetIkarusShop() && !ALLOWED_SPAWN_MAPS.contains(ch->GetMapIndex()))
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_CREATE_SHOP_ON_WRONG_PLACE");
			return false;
		}
#endif

		ch->PointChange(POINT_GOLD, -static_cast<int>(OFFLINESHOP_RESTORE_DURATION_COST));

#ifdef EXTEND_IKASHOP_PRO
		if(ch->GetIkarusShop())
		{
			SendShopRestoreDurationDBPacket(ch->GetPlayerID());
			return true;
		}
#endif

		// componing name
		auto shopname = std::string(ch->GetName());

		ikashop::TShopInfo shopinfo{};
		shopinfo.ownerid = ch->GetPlayerID();
		shopinfo.duration = OFFLINESHOP_DURATION_MAX_MINUTES;
		str_to_cstring(shopinfo.name, shopname.c_str());

#ifdef ENABLE_IKASHOP_ENTITIES
		shopinfo.spawn.map = ch->GetMapIndex();
		shopinfo.spawn.x = ch->GetX();
		shopinfo.spawn.y = ch->GetY();
#endif

		
		SendShopCreateNewDBPacket(shopinfo);
		return true;
	}

	bool CShopManager::RecvShopForceCloseClientPacket(LPCHARACTER ch)
	{
		if(!ch || !ch->GetIkarusShop())
			return false;

		if (!CheckGMLevel(ch))
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_FORCE_CLOSE_SHOP))
			return false;

		if(!CheckSafeboxSize(ch))
			return false;

		// patch with warp check
		ch->SetIkarusShopUseTime();

		// requesting of remove item to each item
		auto shop = ch->GetIkarusShop();
		for(auto& [id, item] : shop->GetItems())
			SendShopRemoveItemDBPacket(ch->GetPlayerID(), id);

		SendShopForceCloseDBPacket(ch->GetPlayerID());
		return true;
	}

	bool CShopManager::RecvShopOpenClientPacket(LPCHARACTER ch, DWORD ownerid)
	{
		if(!ch || !ch->GetDesc())
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_OPEN_SHOP))
			return false;

		auto shop = GetShopByOwnerID(ownerid);
		if(!shop)
			return false;

#ifdef EXTEND_IKASHOP_PRO
		if (shop->GetDuration() == 0)
			return false;
#endif

		// patch with warp check
		ch->SetIkarusShopUseTime();

		// skipping owner
		if (ch->GetPlayerID() == ownerid) {
			// sending board counters
			SendBoardCountersClientPacket(ch);
			GetManager().SendShopOpenMyShopClientPacket(ch);
			return true;
		}

		ch->SetIkarusShopGuest(shop);
		SendShopOpenClientPacket(ch, shop.get());
		return true;
	}

	bool CShopManager::RecvShopOpenMyShopClientPacket(LPCHARACTER ch)
	{
		if(!ch || !ch->GetDesc())
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_OPEN_MY_SHOP))
			return false;

		// patch with warp check
		ch->SetIkarusShopUseTime();

		// sending board counters
		SendBoardCountersClientPacket(ch);

		if (!ch->GetIkarusShop())
			SendShopOpenMyShopNoShopClientPacket(ch);
		else
			SendShopOpenMyShopClientPacket(ch);

		return true;
	}

	bool CShopManager::RecvShopBuyItemClientPacket(LPCHARACTER ch, DWORD ownerid, DWORD itemid, bool searching, long long seenprice)  //patch seen price check
	{
		if(!ch || !CheckGMLevel(ch))
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_BUY_ITEM))
			return false;

		if (!CheckCharacterActions(ch))
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_DO_NOW");
			return true;
		}

		// searching shop
		SHOP_HANDLE shop = GetShopByOwnerID(ownerid);
		if(!shop){
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_GENERIC_ERROR");
			return false;
		}

#ifdef EXTEND_IKASHOP_PRO
		if(shop->GetDuration() == 0)
			return false;
#endif

		// searching item
		auto pitem = shop->GetItem(itemid);
		if(!pitem){
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_ITEM_NOT_AVAILABLE");
			SendSearchResultDeleteClientPacket(ch, itemid);
			return false;
		}

		if(!pitem->CanBuy(ch))
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_BUY_NOT_ENOUGH_MONEY");
			return false;
		}

		//patch seen price check
		if (pitem->GetPrice().GetTotalYangAmount() != seenprice)
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_BUY_PRICE_CHANGED");
			return false;
		}

		// patch with warp check
		ch->SetIkarusShopUseTime();

		//patch seen price check
		SendShopLockBuyItemDBPacket(ch, ownerid, pitem, seenprice);
		return true;
	}

#ifdef ENABLE_IKASHOP_ENTITIES
#ifdef EXTEND_IKASHOP_ULTIMATE
	bool CShopManager::RecvAuctionClickEntityClientPacket(LPCHARACTER ch, DWORD evid)
	{
		return RecvAuctionOpenRequestClientPacket(ch, evid);
	}
#endif

	bool CShopManager::RecvShopClickEntityClientPacket(LPCHARACTER ch, DWORD evid)
	{
		auto iter = m_entityByVID.find(evid);
		if(iter != m_entityByVID.end()){
			auto entity = iter->second;
#ifdef EXTEND_IKASHOP_ULTIMATE
			entity->GetShop() ? 
				RecvShopOpenClientPacket(ch, entity->GetShop()->GetOwnerPID()) : 
				RecvAuctionOpenRequestClientPacket(ch, entity->GetAuction()->GetInfo().owner);
#else
			RecvShopOpenClientPacket(ch, entity->GetShop()->GetOwnerPID());
#endif
		}
		return true;
	}

#ifdef EXTEND_IKASHOP_PRO
	bool CShopManager::RecvMoveShopEntityClientPacket(LPCHARACTER ch)
	{
		if(!ch->GetIkarusShop() || ch->GetIkarusShop()->GetDuration() == 0)
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_MOVE_CLOSED_SHOP");
			return false;
		}

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_MOVE_SHOP))
			return false;

		if(!ALLOWED_SPAWN_MAPS.contains(ch->GetMapIndex()))
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_CREATE_SHOP_ON_WRONG_PLACE");
			return false;
		}

		if(ch->GetGold() < 100000)
			return false;

		ch->PointChange(POINT_GOLD, -100000);

		TShopSpawn spawn{};
		spawn.map = ch->GetMapIndex();
		spawn.x = ch->GetX();
		spawn.y = ch->GetY();

		// encoding packets to db
		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader = ikashop::SUBHEADER_GD_MOVE_SHOP_ENTITY;

		TSubPacketGDMoveShopEntity subpack{};
		subpack.owner = ch->GetPlayerID();
		subpack.spawn = spawn;
		
		TEMP_BUFFER buffer;
		buffer.write(pack);
		buffer.write(subpack);

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buffer.read_peek(), buffer.size());
		return true;
	}
#endif
#endif

	void CShopManager::SendSearchResultDeleteClientPacket(LPCHARACTER ch, DWORD itemid)
	{
		if (!ch || !ch->GetDesc())
			return;

		TPacketGCNewIkarusShop pack;
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.size = sizeof(pack) + sizeof(DWORD);
		pack.subheader = ikashop::SUBHEADER_GC_SEARCH_RESULT_DELETE;

		ch->GetDesc()->BufferedPacket(&pack, sizeof(pack));
		ch->GetDesc()->Packet(&itemid, sizeof(itemid));
	}

	void CShopManager::SendShopOpenClientPacket(LPCHARACTER ch, ikashop::CShop* shop)
	{
		if (!ch->GetDesc())
			return;

		const auto& items = shop->GetItems();

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader	= SUBHEADER_GC_SHOP_OPEN;
#ifdef EXTEND_IKASHOP_ULTIMATE
		pack.size = sizeof(pack) + sizeof(TSubPacketGCShopOpen) + sizeof(TShopItemView)* items.size();
#else
		pack.size = sizeof(pack) + sizeof(TSubPacketGCShopOpen) + sizeof(TShopItem)* items.size();
#endif

		TSubPacketGCShopOpen subpack{};
		subpack.shop.count = shop->GetItems().size();
		subpack.shop.duration = shop->GetDuration();
		subpack.shop.ownerid = shop->GetOwnerPID();
#ifdef EXTEND_IKASHOP_ULTIMATE
		subpack.shop.lock_index = shop->GetLockIndex();
#endif
		str_to_cstring(subpack.shop.name, shop->GetName());

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		for (auto& [id, item] : items)
		{
			auto& itemInfo = item->GetInfo();
#ifdef EXTEND_IKASHOP_ULTIMATE
			auto itemView = DerivedFromBase<TShopItemView>(itemInfo);
			itemView.priceavg = GetAveragePrice(itemView.vnum, itemView.count);
			buff.write(&itemView, sizeof(itemView));
#else
			buff.write(&itemInfo, sizeof(itemInfo));
#endif
		}

		ch->GetDesc()->Packet(buff.read_peek(), buff.size());
	}

	void CShopManager::SendShopOpenMyShopNoShopClientPacket(LPCHARACTER ch)
	{
		if (!ch->GetDesc())
			return;

		ch->SetLookingShopOwner(false);

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader	= SUBHEADER_GC_SHOP_OPEN_OWNER_NO_SHOP;
		pack.size = sizeof(pack);

		ch->GetDesc()->Packet(&pack, sizeof(pack));
	}

	void CShopManager::SendShopExpiredGuesting(DWORD ownerid, LPCHARACTER ch)
	{
		if (!ch->GetDesc())
			return;

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader = SUBHEADER_GC_SHOP_EXPIRED_GUESTING;
		pack.size = sizeof(pack) + sizeof(TSubPacketGCShopExpiredGuesting);

		TSubPacketGCShopExpiredGuesting subpack{};
		subpack.ownerid = ownerid;

		ch->GetDesc()->BufferedPacket(&pack, sizeof(pack));
		ch->GetDesc()->Packet(&subpack, sizeof(subpack));
	}

	void CShopManager::SendShopOpenMyShopClientPacket(LPCHARACTER ch)
	{
		// validating character and shop
		if (!ch->GetDesc())
			return;
		if(!ch->GetIkarusShop())
			return;

		// storing looking state
		ch->SetLookingShopOwner(true);

		// getting shop
		auto shop = ch->GetIkarusShop();
		auto owner = ch->GetPlayerID();

		const auto& items = shop->GetItems();

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader	= SUBHEADER_GC_SHOP_OPEN_OWNER;
#ifdef EXTEND_IKASHOP_ULTIMATE
		pack.size = sizeof(pack) + sizeof(TSubPacketGCShopOpenOwner) + sizeof(TShopItemView) * items.size();
#else
		pack.size = sizeof(pack) + sizeof(TSubPacketGCShopOpenOwner) + sizeof(TShopItem) * items.size();
#endif

		TSubPacketGCShopOpenOwner subpack{};
		subpack.shop.count = items.size();
		subpack.shop.duration = shop->GetDuration();
		subpack.shop.ownerid = owner;
#ifdef EXTEND_IKASHOP_ULTIMATE
		subpack.shop.decoration = shop->GetDecoration();
		subpack.shop.decoration_time = shop->GetDecorationTime();
		subpack.shop.lock_index = shop->GetLockIndex();
#endif
		str_to_cstring(subpack.shop.name, shop->GetName());

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));		

		for (auto& [id, item] : items)
		{			
			auto& itemInfo = item->GetInfo();
#ifdef EXTEND_IKASHOP_ULTIMATE
			auto itemView = DerivedFromBase<TShopItemView>(itemInfo);
			itemView.priceavg = GetAveragePrice(itemInfo.vnum, itemInfo.count);
			buff.write(&itemView, sizeof(itemView));
#else
			buff.write(&itemInfo, sizeof(itemInfo));
#endif
		}

		ch->GetDesc()->Packet(buff.read_peek(), buff.size());
	}

	void CShopManager::SendShopForceClosedClientPacket(DWORD ownerid)
	{
		if(auto ch = CHARACTER_MANAGER::instance().FindByPID(ownerid))
		{
			if(ch->GetDesc())
			{
				TPacketGCNewIkarusShop pack{};
				pack.header = HEADER_GC_NEW_OFFLINESHOP;
				pack.size = sizeof(pack);
				pack.subheader = SUBHEADER_GC_SHOP_OPEN_OWNER_NO_SHOP;
				ch->GetDesc()->Packet(&pack, sizeof(pack));
			}
		}		
	}

	//ITEMS
	bool CShopManager::RecvShopAddItemClientPacket(LPCHARACTER ch, const TItemPos& pos, const TPriceInfo& price, int destPos)
	{
		if(!ch || !ch->GetIkarusShop())
			return false;

		if(!CheckGMLevel(ch))
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_ADD_ITEM))
			return false;

		if (!CheckSafeboxSize(ch))
			return false;

		if (!CheckCharacterActions(ch))
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_DO_NOW");
			return true;
		}

		// patch with warp check
		ch->SetIkarusShopUseTime();

#ifdef EXTEND_IKASHOP_PRO
		if (ch->GetIkarusShop()->GetDuration() == 0)
			return false;
#endif

		auto item = ch->GetItem(pos);
		if(!item)
			return false;

		if (item->isLocked() || item->IsEquipped() || item->IsExchanging())
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_ADD_USED_ITEM");
			return true;
		}

#ifdef ENABLE_SOULBIND_SYSTEM
		if (pkItem->IsSealed()) {
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_ADD_SEALED_ITEM");
			return true;
		}
#endif

		if(IS_SET(item->GetAntiFlag(), ITEM_ANTIFLAG_GIVE))
			return false;
		if(IS_SET(item->GetAntiFlag(), ITEM_ANTIFLAG_MYSHOP))
			return false;

		if (!IsGoodSalePrice(price))
			return false;

		// checking space
		if(!ch->GetIkarusShop()->ReserveSpace(destPos, item->GetSize()))
			return false;

		TShopItem sitem{};
		sitem.pos = destPos;
		sitem.id = item->GetID();
		sitem.owner = ch->GetPlayerID();
		sitem.vnum = item->GetVnum();
		sitem.count = item->GetCount();
		sitem.expiration = GetItemExpiration(item);
		sitem.price = price;
#ifdef ENABLE_CHANGELOOK_SYSTEM
		sitem.item.dwTransmutation = item->GetTransmutation();
#endif
		memcpy(sitem.aAttr, item->GetAttributes(),	sizeof(sitem.aAttr));
		memcpy(sitem.alSockets, item->GetSockets(), sizeof(sitem.alSockets));

		MoveAndDestroyInstance(item, IKASHOP_OFFLINESHOP, destPos);
		SendShopAddItemDBPacket(ch->GetPlayerID(), sitem);
		return true;
	}

	bool CShopManager::RecvShopRemoveItemClientPacket(LPCHARACTER ch, DWORD itemid)
	{
		if(!ch || !ch->GetIkarusShop())
			return false;

		if(!CheckGMLevel(ch))
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_REMOVE_ITEM))
			return false;

		if (!CheckSafeboxSize(ch))
			return false;

		// checking if it is the last item
		auto shop = ch->GetIkarusShop();

		// checking shop has the item
		if(!shop->GetItem(itemid))
			return false;

		// patch with warp check
		ch->SetIkarusShopUseTime();
		SendShopRemoveItemDBPacket(shop->GetOwnerPID(), itemid);
		return true;
	}

	bool CShopManager::RecvShopEditItemDBPacket(DWORD ownerid, DWORD itemid, const TPriceInfo& price)
	{
		auto shop = GetShopByOwnerID(ownerid);
		if (!shop)
			return false;

		shop->ModifyItemPrice(itemid, price);
		return true;
	}

	//FILTER
	bool CShopManager::RecvShopFilterRequestClientPacket(LPCHARACTER ch, const TFilterInfo& filter)
	{
		if(!ch)
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_FILTER_REQUEST))
			return false;

		if (!CheckSearchCooldown(ch->GetPlayerID()))
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_SEARCH_COOLDOWN_STILL_ACTIVED");
			return true;
		}

		// patch with warp check
		ch->SetIkarusShopUseTime();

		std::vector<TSearchResultInfo> matches;
		const auto filtername = StringToLower(filter.name, strnlen(filter.name, sizeof(filter.name)));

		for (auto& [id, auction] : m_mapAuctions)
		{
			// skipping my own items
			if (auction->GetInfo().owner == ch->GetPlayerID())
				continue;

			const auto& info = auction->GetInfo();
			if (!MatchFilter(info, filter, filtername))
				continue;

			auto& result = matches.emplace_back(DerivedFromBase<TSearchResultInfo, TShopItem>(info));
			result.auction = true;
			result.duration = auction->GetInfo().duration;
#ifdef EXTEND_IKASHOP_ULTIMATE
			result.priceavg = GetAveragePrice(result.vnum, result.count);
#endif
			str_to_cstring(result.seller_name, auction->GetInfo().ownername);
		}

		for (auto& [id, shop] : m_mapShops)
		{
			// skipping my own items
			if(shop->GetOwnerPID() == ch->GetPlayerID())
				continue;

#ifdef EXTEND_IKASHOP_PRO
			if (shop->GetDuration() == 0)
				continue;
#endif

			for (auto& [id, sitem] : shop->GetItems())
			{
				const auto& info = sitem->GetInfo();
				if(!MatchFilter(info, filter, filtername))
					continue;

				auto& result = matches.emplace_back(DerivedFromBase<TSearchResultInfo>(info));
				result.auction = false;
				result.duration = shop->GetDuration();
#ifdef EXTEND_IKASHOP_ULTIMATE
				result.priceavg = GetAveragePrice(result.vnum, result.count);
#endif
				str_to_cstring(result.seller_name, shop->GetName());
				if (matches.size() >= OFFLINESHOP_MAX_SEARCH_RESULT)
					break;
			}

			if(matches.size() >= OFFLINESHOP_MAX_SEARCH_RESULT)
				break;
		}

		SendShopFilterResultClientPacket(ch, matches);
		return true;
	}

	bool CShopManager::RecvShopSearchFillRequestClientPacket(LPCHARACTER ch)
	{
		if (!ch)
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_FILTER_REQUEST))
			return false;

		if (!CheckSearchCooldown(ch->GetPlayerID()))
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_SEARCH_COOLDOWN_STILL_ACTIVED");
			return true;
		}

		// patch with warp check
		ch->SetIkarusShopUseTime();

		std::vector<TSearchResultInfo> matches;
		std::set<DWORD> matchedIds;

		for (int i = 0; i < 20; i++)
		{
			const auto shopCount = m_mapShops.size();
			if(shopCount == 0)
				break;

			auto shopIter = std::next(m_mapShops.begin(), number(0, shopCount-1));
			auto& shop = shopIter->second;

			// skipping my own items
			if (shop->GetOwnerPID() == ch->GetPlayerID())
				continue;

#ifdef EXTEND_IKASHOP_PRO
			if (shop->GetDuration() == 0)
				continue;
#endif

			auto& items = shop->GetItems();
			const auto itemCount = items.size();
			if(itemCount == 0)
				continue;

			auto itemIter = std::next(items.begin(), number(0, itemCount-1));
			auto& item = itemIter->second;

			if(matchedIds.contains(item->GetInfo().id))
				continue;

			const auto& info = item->GetInfo();
			auto& result = matches.emplace_back(DerivedFromBase<TSearchResultInfo>(info));
			result.auction = false;
			result.duration = shop->GetDuration();
#ifdef EXTEND_IKASHOP_ULTIMATE
			result.priceavg = GetAveragePrice(result.vnum, result.count);
#endif
			str_to_cstring(result.seller_name, shop->GetName());
			matchedIds.emplace(info.id);
		}

		for(int i = 0; i < 5; i++)
		{
			const auto auctionCount = m_mapAuctions.size();
			if (auctionCount == 0)
				break;

			auto auctionIter = std::next(m_mapAuctions.begin(), number(0, auctionCount - 1));
			auto& auction = auctionIter->second;

			// skipping my own items
			if (auction->GetInfo().owner == ch->GetPlayerID())
				continue;

			auto bestoff = auction->GetBestOffer();

			if (matchedIds.contains(auction->GetInfo().id))
				continue;

			const auto& info = auction->GetInfo();
			auto& result = matches.emplace_back(DerivedFromBase<TSearchResultInfo, TShopItem>(info));
			result.auction = true;
			result.price.yang = GetAuctionMinRaise(auction);
			result.duration = auction->GetInfo().duration;
#ifdef EXTEND_IKASHOP_ULTIMATE
			result.priceavg = GetAveragePrice(result.vnum, result.count);
#endif
			str_to_cstring(result.seller_name, auction->GetInfo().ownername);
			matchedIds.emplace(info.id);
		}

		SendShopFilterResultClientPacket(ch, matches);
		return true;
	}

	void CShopManager::SendShopFilterResultClientPacket(LPCHARACTER ch, const std::vector<TSearchResultInfo>& items)
	{
		if(!ch || !ch->GetDesc())
			return;

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader = SUBHEADER_GC_SHOP_FILTER_RESULT;
		pack.size = sizeof(pack) + sizeof(TSubPacketGCShopFilterResult) + sizeof(TSearchResultInfo)*items.size();

		TSubPacketGCShopFilterResult subpack{};
		subpack.count = items.size();

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		if(!items.empty())
			buff.write(items.data(), items.size() * sizeof(items[0]));

		ch->GetDesc()->Packet(buff.read_peek(), buff.size());
	}

	//OFFERS
	bool CShopManager::RecvShopCreateOfferClientPacket(LPCHARACTER ch, TOfferInfo offer)
	{
		if(!ch)
			return false;

		if(!CheckGMLevel(ch))
			return false;

		if (!CheckSafeboxSize(ch))
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_CREATE_OFFER))
			return false;

		// checking pending offer limit
		auto offersIter = m_mapOffersByBuyer.find(ch->GetPlayerID());
		if (offersIter != m_mapOffersByBuyer.end() && offersIter->second.size() >= OFFLINESHOP_MAX_PENDING_OFFERS)
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_PENDING_PRIVATE_OFFER_LIMIT");
			return false;
		}

		// checking for existing offers on this item
		if (offersIter != m_mapOffersByBuyer.end())
		{
			auto& offers = offersIter->second;
			auto iter = std::find_if(offers.begin(), offers.end(),
				[&offer](const OFFER_HANDLE& handle){ return handle->itemid == offer.itemid; });
			if(iter != offers.end())
			{
				SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_PENDING_PRIVATE_OFFER_ALREADY_EXIST");
				return false;
			}
		}

		// stopping me to offer to myself
		if(ch->GetPlayerID() == offer.ownerid)
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_GENERIC_ERROR");
			return false;
		}

		// searching shop
		auto shop = GetShopByOwnerID(offer.ownerid);
		if(!shop)
			return false;

#ifdef EXTEND_IKASHOP_PRO
		if (shop->GetDuration() == 0)
			return false;
#endif

		// searching item
		auto item = shop->GetItem(offer.itemid);
		if(!item)
			return false;

		// checking price min
		if(item->GetPrice().GetTotalYangAmount() < 10000)
			return false;

		// patch with warp check
		ch->SetIkarusShopUseTime();

		// fix flooding offers
		if (!CheckOfferCooldown(ch->GetPlayerID()))
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CREATE_OFFER_COOLDOWN_STILL_ACTIVED");
			return false;
		}
		
		if (!CheckOfferPrice(offer.price, item->GetPrice()))
			return false;

#ifdef ENABLE_CHEQUE_SYSTEM
		if (offer.price.yang < 0 || offer.price.cheque < 0)
			return false;
#else
		if (offer.price.yang < 0)
			return false;
#endif

		if (offer.price.GetTotalYangAmount() <= 0)
			return false;

		if( (long long) ch->GetGold() < offer.price.yang)
			return false;

#ifdef ENABLE_CHEQUE_SYSTEM
		if (ch->GetCheque() < offer.price.cheque)
			return false;
#endif

		ch->PointChange(POINT_GOLD, -offer.price.yang);
#ifdef ENABLE_CHEQUE_SYSTEM
		ch->PointChange(POINT_CHEQUE, -offer.price.cheque);
#endif
		offer.accepted = false;
		offer.notified = false;
		offer.buyerid	= ch->GetPlayerID();
#ifdef ENABLE_CHEQUE_SYSTEM
		offer.price.yang = offer.price.GetTotalYangAmount();
		offer.price.cheque = 0;
#endif
		str_to_cstring(offer.buyername, ch->GetName());

		SendShopOfferNewDBPacket(offer, item);
		return true;
	}

	bool CShopManager::RecvShopAcceptOfferClientPacket(LPCHARACTER ch, DWORD offerid)
	{
		if(!ch || !ch->GetIkarusShop())
			return false;

		if(!CheckGMLevel(ch))
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_ACCEPT_OFFER))
			return false;

		auto shop = ch->GetIkarusShop();
		auto& offers = shop->GetOffers();

#ifdef EXTEND_IKASHOP_PRO
		if (shop->GetDuration() == 0)
			return false;
#endif

		// searching offer
		auto offerIter = offers.find(offerid);
		if(offerIter == offers.end())
			return false;

		// chekcing item was already accepted
		auto info = offerIter->second;
		if(info->accepted)
			return false;

		// checking that accepting character is the owner of the shop
		if(ch->GetPlayerID() != info->ownerid)
			return false;

		// checking item exists
		if(!shop->GetItem(info->itemid))
			return false;

		// patch with warp check
		ch->SetIkarusShopUseTime();

		// forwarding to db
		info->accepted = true;
		SendShopOfferAcceptDBPacket(*info);
		return true;
	}

	bool CShopManager::RecvShopCancelOfferClientPacket(LPCHARACTER ch, DWORD offerid, DWORD ownerid)
	{
		if(!ch || !CheckGMLevel(ch))
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_CANCEL_OFFER))
			return false;

		// searching shop
		auto shop = GetShopByOwnerID(ownerid);
		if(!shop)
			return false;

#ifdef EXTEND_IKASHOP_PRO
		if (shop->GetDuration() == 0)
			return false;
#endif

		// searching offer
		auto& offers = shop->GetOffers();
		auto offerIter = offers.find(offerid);
		if(offerIter == offers.end())
			return false;

		// checking offer wasnt accepted
		auto offer = offerIter->second;
		if(offer->accepted)
			return false;

		// checking item exists
		if(!shop->GetItem(offer->itemid))
			return false;

		// checking who is rejecting the offer is the owner of the shop or the buyer
		if(ch->GetPlayerID() != shop->GetOwnerPID() && ch->GetPlayerID() != offer->buyerid)
			return false;

		// patch with warp check
		ch->SetIkarusShopUseTime();

		
		SendShopOfferCancelDBPacket(*offer, ch->GetPlayerID());
		return true;
	}

	bool CShopManager::RecvOfferListRequestPacket(LPCHARACTER ch) //ikashop-updated 03/08/19
	{
		if (!ch->GetDesc())
			return false;

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader = SUBHEADER_GC_OFFER_LIST;
		pack.size = sizeof(pack) + sizeof(TSubPacketGCShopOfferList);

		TSubPacketGCShopOfferList subpack{};
		subpack.offercount = 0;

		// storing offers inside a vector
		std::vector<TOfferListElement> offerlist;

		// searching offers as buyer
		auto it = m_mapOffersByBuyer.find(ch->GetPlayerID());
		if (it != m_mapOffersByBuyer.end())
		{
			auto& offers = it->second;
			for(const auto& offer : offers)
			{
				auto shop = GetShopByOwnerID(offer->ownerid);
				if (!shop)
					continue;

				auto item = shop->GetItem(offer->itemid);
				if (!item)
					continue;

				auto& listElement = offerlist.emplace_back(DerivedFromBase<TOfferListElement>(*offer));
				listElement.item = item->GetInfo();
				listElement.incoming = false;
#ifdef EXTEND_IKASHOP_ULTIMATE
				listElement.priceavg = GetAveragePrice(listElement.item.vnum, listElement.item.count);
#endif
				str_to_cstring(listElement.shopname, shop->GetName());
			}
		}

		// searching offers as seller
		if(auto shop = ch->GetIkarusShop())
		{
			for (const auto& [id, offer] : shop->GetOffers())
			{
				auto item = shop->GetItem(offer->itemid);
				if (!item)
					continue;

				auto& listElement = offerlist.emplace_back(DerivedFromBase<TOfferListElement>(*offer));
				listElement.item = item->GetInfo();
				listElement.incoming = true;
#ifdef EXTEND_IKASHOP_ULTIMATE
				listElement.priceavg = GetAveragePrice(listElement.item.vnum, listElement.item.count);
#endif
				str_to_cstring(listElement.shopname, shop->GetName());
			}
		}

		// updating pack & subpack
		pack.size += sizeof(TOfferListElement) * offerlist.size();
		subpack.offercount = offerlist.size();

		// writing bytes into a buffer
		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));
		for(auto& offer : offerlist)
			buff.write(&offer, sizeof(offer));

		// forwarding bytes
		ch->SetLookingIkarusShopOfferList(true);
		ch->GetDesc()->Packet(buff.read_peek(), buff.size());
		return true;
	}

	//SAFEBOX
	bool CShopManager::RecvShopSafeboxOpenClientPacket(LPCHARACTER ch)
	{
		if(!ch || ch->GetIkarusShopSafebox())
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_SAFEBOX_OPEN))
			return false;

		auto safebox = GetShopSafeboxByOwnerID(ch->GetPlayerID());
		if(!safebox)
			return false;

		// patch with warp check
		ch->SetIkarusShopUseTime();

		ch->SetIkarusShopSafebox(safebox);
		safebox->RefreshToOwner(ch);
		return true;
	}

	bool CShopManager::RecvShopSafeboxGetItemClientPacket(LPCHARACTER ch, DWORD itemid)
	{
		if(!ch || !ch->GetIkarusShopSafebox())
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_SAFEBOX_GET_ITEM))
			return false;

		// searching safebox & item
		auto safebox = ch->GetIkarusShopSafebox();
		auto sitem = safebox->GetItem(itemid);
		if(!sitem)
			return false;

		// creating temporary item
		auto item = sitem->CreateItem();
		if(!item)
			return false;

		// checking item can be stored
		TItemPos itemPos;
		auto ret = ch->CanTakeInventoryItem(item, &itemPos);
		
		// destroying temporary item
		item->SetSkipSave(true);
		M2_DESTROY_ITEM(item);

		// if item can't be store returning false
		if (!ret){
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_SAFEBOX_CANNOT_GET_ITEM");
			return false;
		}

		// patch with warp check
		ch->SetIkarusShopUseTime();
		SendShopSafeboxGetItemDBPacket(ch->GetPlayerID(), itemid);
		return true;
	}

	bool CShopManager::RecvShopSafeboxGetValutesClientPacket(LPCHARACTER ch)
	{
		if(!ch || !ch->GetIkarusShopSafebox())
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_SAFEBOX_GET_VALUTES))
			return false;

		// getting safebox
		auto safebox = ch->GetIkarusShopSafebox();
		if(!safebox)
			return false;

		const auto& ownedValute = safebox->GetValutes();
		auto claimingValute = ownedValute;

#ifdef ENABLE_CHEQUE_SYSTEM
		int64_t claimingYang = claimingValute.yang;
		int64_t claimingCheque = 0;
#endif

		if(claimingValute.yang + static_cast<int64_t>(ch->GetGold()) >= GOLD_MAX)
		{
#ifdef ENABLE_CHEQUE_SYSTEM
			// checking about yang & cheque overflow
			const int64_t yangSpace = GOLD_MAX - ch->GetGold() - 1;
			const int64_t chequeSpace = CHEQUE_MAX - ch->GetCheque() - 1;
			
			// getting claimable cheque
			claimingCheque = std::min(chequeSpace, claimingValute.yang / YANG_PER_CHEQUE);
			claimingYang = std::min(yangSpace, claimingValute.yang - claimingCheque*YANG_PER_CHEQUE);
			
			if(claimingCheque < 0)
				return false;
			if(claimingYang < 0)
				return false;
			if(claimingCheque == 0 && claimingYang == 0)
				return false;

			claimingValute.yang = claimingYang + claimingCheque * YANG_PER_CHEQUE;
#else
			claimingValute.yang = GOLD_MAX - ch->GetGold() - 1;
			if(claimingValute.yang <= 0)
				return false;
#endif
		}


		if(!safebox->RemoveValute(claimingValute))
			return false;

		// updating db
		SendShopSafeboxGetValutesDBPacket(ch->GetPlayerID(), claimingValute);

		if (ch->IsLookingShopOwner())
			SendBoardCountersClientPacket(ch);

		// patch with warp check
		ch->SetIkarusShopUseTime();
		safebox->RefreshToOwner();

#ifdef ENABLE_CHEQUE_SYSTEM
		if (claimingYang != 0)
			ch->PointChange(POINT_GOLD, claimingYang);
		if (claimingCheque != 0)
			ch->PointChange(POINT_CHEQUE, claimingCheque);
#else
		ch->PointChange(POINT_GOLD, claimingValute.yang);
#endif
		return true;
	}

	bool CShopManager::RecvShopSafeboxCloseClientPacket(LPCHARACTER ch)
	{
		if(!ch)
			return false;

		ch->SetIkarusShopSafebox(NULL);
		return true;
	}

	void CShopManager::SendShopSafeboxRefreshClientPacket(LPCHARACTER ch, const TValutesInfo& valute, const std::vector<ITEM_HANDLE>& vec)
	{
		if (!ch->GetDesc())
			return;

		if(!ch || !ch->GetIkarusShopSafebox())
			return;

		// patch with warp check
		ch->SetIkarusShopUseTime();

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.size = sizeof(pack) + sizeof(TSubPacketGCShopSafeboxRefresh) + sizeof(TShopPlayerItem) * vec.size();
		pack.subheader = SUBHEADER_GC_SHOP_SAFEBOX_REFRESH;

		TSubPacketGCShopSafeboxRefresh subpack{};
		subpack.itemcount = vec.size();
		subpack.valute = valute;

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));
		
		for (auto& sitem : vec)
		{
			auto& info = static_cast<const TShopPlayerItem&>(sitem->GetInfo());
			buff.write(&info, sizeof(info));
		}

		ch->GetDesc()->Packet(buff.read_peek() , buff.size());
	}

	bool CShopManager::RecvAuctionListRequestClientPacket(LPCHARACTER ch)
	{
		if(!ch)
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_AUCTION_LIST_REQUEST))
			return false;

		std::vector<TAuctionListElement> vec;
		vec.reserve(m_mapAuctions.size());

		for (auto& [id, auction] : m_mapAuctions)
		{
			auto bestOffer = auction->GetBestOffer();

			static TAuctionListElement temp;
			temp.auction = auction->GetInfo();
			temp.best = bestOffer ? bestOffer->price : temp.auction.price;
			temp.offercount = auction->GetOffers().size();
#ifdef EXTEND_IKASHOP_ULTIMATE
			temp.priceavg = GetAveragePrice(temp.auction.vnum, temp.auction.count);
#endif
			vec.push_back(temp);
		}

		SendAuctionListClientPacket(ch, vec);
		return true;
	}

	bool CShopManager::RecvAuctionOpenRequestClientPacket(LPCHARACTER ch, DWORD ownerid)
	{
		auto it = m_mapAuctions.find(ownerid);
		if(it == m_mapAuctions.end())
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_AUCTION_OPEN_REQUEST))
			return false;

		if(ch->GetPlayerID() == ownerid)
			return RecvMyAuctionOpenRequestClientPacket(ch);

		// patch with warp check
		ch->SetIkarusShopUseTime();

		auto& auction = it->second;
		ch->SetIkarusAuctionGuest(auction);
		return true;
	}

	bool CShopManager::RecvMyAuctionOpenRequestClientPacket(LPCHARACTER ch)
	{
		// patch with warp check
		ch->SetIkarusShopUseTime();
		
		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_MY_ACUTION_OPEN_REQUEST))
			return false;

		// refreshing to owner
		if(auto auction = ch->GetIkarusAuction())
		{
			ch->SetLookingAuctionOwner(true);
			auction->RefreshToOwner(ch);
		}

		else
			SendAuctionOpenMyAuctionNoAuctionClientPacket(ch);
		
		return true;
	}

	bool CShopManager::RecvMyAuctionCloseClientPacket(LPCHARACTER ch)
	{
		// patch with warp check
		ch->SetIkarusShopUseTime();
		ch->SetLookingAuctionOwner(false);
		return true;
	}

	bool CShopManager::RecvMyAuctionCancelClientPacket(LPCHARACTER ch)
	{
		if(auto auction = ch->GetIkarusAuction())
		{
			if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_MY_AUCTION_CANCEL))
				return false;

			if(!auction->GetOffers().empty())
				return false;

			TPacketGDNewIkarusShop pack;
			pack.bSubHeader = ikashop::SUBHEADER_GD_AUCTION_CANCEL;
			
			DWORD ownerid = ch->GetPlayerID();

			TEMP_BUFFER buff;
			buff.write(&pack, sizeof(pack));
			buff.write(&ownerid, sizeof(ownerid));

			db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buff.read_peek(), buff.size());
		}
		return true;
	}

	bool CShopManager::RecvAuctionCreateClientPacket(LPCHARACTER ch, const TPriceInfo& startprice, const TItemPos& pos)
	{
		if(!ch || ch->GetIkarusAuction())
			return false;

		if(!CheckGMLevel(ch))
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_AUCTION_CREATE))
			return false;

		if (!CheckCharacterActions(ch))
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_DO_NOW");
			return true;
		}

		if (!IsGoodSalePrice(startprice))
			return false;

#ifdef ENABLE_IKASHOP_ENTITIES
	#ifdef EXTEND_IKASHOP_ULTIMATE
		if(!ALLOWED_SPAWN_MAPS.contains(ch->GetMapIndex())){
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_CREATE_AUCTION_WRONG_PLACE");
			return true;
		}
	#endif
#endif

		//checking about duration limit
		auto duration = OFFLINESHOP_AUCTION_DURATION;
		auto item = ch->GetItem(pos);
		if(!item)
			return false;

		if(item->IsEquipped() || item->IsExchanging() || item->isLocked())
			return false;

		auto table= ITEM_MANAGER::instance().GetTable(item->GetVnum());
		if(!table)
			return false;

		if(IS_SET(table->dwAntiFlags, ITEM_ANTIFLAG_GIVE) || IS_SET(table->dwAntiFlags , ITEM_ANTIFLAG_MYSHOP))
			return false;

#ifdef ENABLE_SOULBIND_SYSTEM
		if (item->IsSealed())
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_CREATE_AUCTION_ITEM_SEALED");
			return false;
		}
#endif

		// patch with warp check
		ch->SetIkarusShopUseTime();

		//making info
		TAuctionInfo auction;
		auction.duration = duration;
		auction.owner = ch->GetPlayerID();
		auction.id = item->GetID();
		auction.price = startprice;
		auction.count = item->GetCount();
		auction.vnum = item->GetVnum();
		auction.expiration = GetItemExpiration(item);
#ifdef ENABLE_CHANGELOOK_SYSTEM
		auction.dwTransmutation = item->GetTransmutation();
#endif
#ifdef ENABLE_IKASHOP_ENTITIES
	#ifdef EXTEND_IKASHOP_ULTIMATE
		auction.spawn.map = ch->GetMapIndex();
		auction.spawn.x = ch->GetX();
		auction.spawn.y = ch->GetY();
	#endif
#endif
		str_to_cstring(auction.ownername, ch->GetName());
		std::memcpy(auction.aAttr, item->GetAttributes(), sizeof(auction.aAttr));
		std::memcpy(auction.alSockets, item->GetSockets(), sizeof(auction.alSockets));

		//removing from character and deleting item instance
		MoveAndDestroyInstance(item, IKASHOP_AUCTION, 0);

		SendAuctionCreateDBPacket(auction);
		return true;
	}

	bool CShopManager::RecvAuctionAddOfferClientPacket(LPCHARACTER ch, DWORD ownerid, const TPriceInfo& price)
	{
		//checking about guesting
		if(!ch || !ch->GetIkarusAuctionGuest() || ch->GetIkarusAuctionGuest()->GetInfo().owner != ownerid)
			return false;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_AUCTION_ADD_OFFER))
			return false;

		if(!CheckGMLevel(ch))
			return false;

		if (!CheckSafeboxSize(ch))
			return false;

		//check anti auto-offer
		if(ch->GetPlayerID() == ownerid)
			return false;

		//check about enough money
		if(static_cast<long long>(ch->GetGold()) < price.yang)
			return false;

#ifdef ENABLE_CHEQUE_SYSTEM
		if (ch->GetCheque() < price.cheque)
			return false;
#endif

		//checking about raise from best buyer
		auto auction = ch->GetIkarusAuctionGuest();
		if(auction->GetBestBuyer() == ch->GetPlayerID()){
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_CANNOT_RAISE_OVER_YOURSELF");
			return false;
		}

		// patch with warp check
		ch->SetIkarusShopUseTime();

		//checking about min raise amount (+10% by default)
		auto minRaise = GetAuctionMinRaise(auction);
		if (price.GetTotalYangAmount() < minRaise)
			return false;

		ch->PointChange(POINT_GOLD,-price.yang);
#ifdef ENABLE_CHEQUE_SYSTEM
		ch->PointChange(POINT_CHEQUE, -price.cheque);
#endif

		TAuctionOfferInfo offer;
		offer.buyerid = ch->GetPlayerID();
		offer.ownerid = ownerid;
		offer.datetime = static_cast<int64_t>(std::time(nullptr));
		offer.price = price;
		offer.seenflag = false;
#ifdef ENABLE_CHEQUE_SYSTEM
		// converting amount cheque in yang
		offer.price.yang = offer.price.GetTotalYangAmount();
		offer.price.cheque=0;
#endif
		str_to_cstring(offer.buyername, ch->GetName());

		SendAuctionAddOfferDBPacket(offer);
		return true;
	}

	bool CShopManager::RecvAuctionExitFromAuction(LPCHARACTER ch, DWORD ownerid)
	{
		if(ch->GetIkarusAuctionGuest() && ch->GetIkarusAuctionGuest()->GetInfo().owner == ownerid)
			ch->SetIkarusAuctionGuest(nullptr);
		ch->SetIkarusShopUseTime();
		return true;
	}

	void CShopManager::SendAuctionListClientPacket(LPCHARACTER ch, const std::vector<TAuctionListElement>& auctionVec)
	{
		if (!ch->GetDesc())
			return;

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader	= SUBHEADER_GC_AUCTION_LIST;
		pack.size = sizeof(pack) + sizeof(TSubPacketGCAuctionList) + (sizeof(TAuctionListElement)*auctionVec.size());

		TSubPacketGCAuctionList subpack{};
		subpack.count = auctionVec.size();

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));
		if(!auctionVec.empty())
			buff.write(&auctionVec[0], sizeof(auctionVec[0]) * auctionVec.size());

		ch->GetDesc()->Packet(buff.read_peek(), buff.size());
	}

	void CShopManager::SendAuctionOpenAuctionClientPacket(LPCHARACTER ch, const CAuction& auction)
	{
		if (!ch->GetDesc())
			return;

		const auto& info = auction.GetInfo();
		const auto& offers = auction.GetOffers();

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader = ch->GetPlayerID() != info.owner ? SUBHEADER_GC_OPEN_AUCTION : SUBHEADER_GC_OPEN_MY_AUCTION;
		pack.size = sizeof(pack)+ sizeof(TSubPacketGCAuctionOpen) + (sizeof(TAuctionOfferInfo) * offers.size());

		TSubPacketGCAuctionOpen subpack{};
		subpack.auction = info;
		subpack.offercount = offers.size();
#ifdef EXTEND_IKASHOP_ULTIMATE
		subpack.priceavg = GetAveragePrice(info.vnum, info.count);
#endif

		TEMP_BUFFER buff;
		buff.write(&pack, sizeof(pack));
		buff.write(&subpack, sizeof(subpack));

		for(auto& offer : offers)
			buff.write(offer.get(), sizeof(TAuctionOfferInfo));

		ch->GetDesc()->Packet(buff.read_peek(), buff.size());

		// checking for new auction offers forwarded to owner
		if(pack.subheader == SUBHEADER_GC_OPEN_MY_AUCTION)
		{
			for(auto& offer : auction.GetOffers())
			{
				if(offer->seenflag == false)
				{
					TPacketGDNewIkarusShop pack{};
					pack.bSubHeader = ikashop::SUBHEADER_GD_AUCTION_OFFER_SEEN;

					ikashop::TSubPacketGDAuctionOfferSeen subpack{};
					subpack.ownerid = auction.GetInfo().owner;
					subpack.buyerid = offer->buyerid;
					subpack.price = offer->price.yang;

					TEMP_BUFFER buffer;
					buffer.write(&pack, sizeof(pack));
					buffer.write(&subpack, sizeof(subpack));

					db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buffer.read_peek(), buffer.size());
				}
			}	
		}
	}

	void CShopManager::SendAuctionOpenMyAuctionNoAuctionClientPacket(LPCHARACTER ch)
	{
		if (!ch->GetDesc())
			return;

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader	= SUBHEADER_GC_OPEN_MY_AUCTION_NO_AUCTION;
		pack.size = sizeof(pack);

		ch->GetDesc()->Packet(&pack, sizeof(pack));
	}

	void CShopManager::RecvCloseMyShopBoardClientPacket(LPCHARACTER ch)
	{
		if(ch)
			ch->SetLookingShopOwner(false);
	}

	void CShopManager::RecvCloseOfferListClientPacket(LPCHARACTER ch)
	{
		if(ch)
			ch->SetLookingIkarusShopOfferList(false);
	}

	void CShopManager::RecvCloseShopGuestClientPacket(LPCHARACTER ch)
	{
		if(ch)
			ch->SetIkarusShopGuest(nullptr);
	}

#ifdef EXTEND_IKASHOP_PRO
	void CShopManager::SendNotificationListClientPacket(LPCHARACTER ch)
	{
		if(ch && ch->GetDesc())
		{
			auto& notifications = m_notificationMap[ch->GetPlayerID()];

			TPacketGCNewIkarusShop pack{};
			pack.header = HEADER_GC_NEW_OFFLINESHOP;
			pack.size = sizeof(pack) + sizeof(ikashop::TSubPacketGCNotificationList) + notifications.size() * sizeof(TNotificationInfo);
			pack.subheader = ikashop::SUBHEADER_GC_NOTIFICATION_LIST;

			ikashop::TSubPacketGCNotificationList subpack{};
			subpack.count = notifications.size();

			TEMP_BUFFER buffer;
			buffer.write(&pack, sizeof(pack));
			buffer.write(&subpack, sizeof(subpack));
			if (!notifications.empty())
				buffer.write(notifications.data(), notifications.size() * sizeof(TNotificationInfo));

			ch->GetDesc()->Packet(buffer.read_peek(), buffer.size());


			// removing notifications
			for(auto& notification : notifications)
			{
				TPacketGDNewIkarusShop pack{};
				pack.bSubHeader = ikashop::SUBHEADER_GD_NOTIFICATION_SEEN;

				ikashop::TSubPacketGDNotificationSeen subpack{};
				subpack.id = notification.id;
				subpack.owner = ch->GetPlayerID();

				TEMP_BUFFER buffer;
				buffer.write(&pack, sizeof(pack));
				buffer.write(&subpack, sizeof(subpack));

				db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buffer.read_peek(), buffer.size());
			}

			notifications.clear();

			// forwarding board counters
			if(ch->IsLookingShopOwner())
				SendBoardCountersClientPacket(ch);
		}
	}
#endif

	void CShopManager::SendBoardCountersClientPacket(LPCHARACTER ch)
	{
		if(!ch || !ch->GetDesc())
			return;

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.size = sizeof(pack) + sizeof(ikashop::TSubPacketGCBoardCounters);
		pack.subheader = ikashop::SUBHEADER_GC_BOARD_COUNTERS;

		ikashop::TSubPacketGCBoardCounters subpack{};

		// checking safebox items counter
		if(const auto safebox = ch->GetIkarusShopSafebox()){
			subpack.safebox = safebox->GetItems().size();
			if(safebox->GetValutes().yang != 0)
				subpack.safebox++;
		}

		// checking auction offers counter
		if(const auto auction = ch->GetIkarusAuction())
			subpack.auction = auction->GetNewOffersCounter();

		// checking incoming offers counter
		if(const auto shop = ch->GetIkarusShop())
			subpack.offers = shop->GetNewOffersCounter();

#ifdef EXTEND_IKASHOP_PRO
		const auto& notifications = m_notificationMap[ch->GetPlayerID()];
		subpack.notification = notifications.size();
#endif
		ch->GetDesc()->BufferedPacket(&pack, sizeof(pack));
		ch->GetDesc()->Packet(&subpack, sizeof(subpack));
	}

#ifdef EXTEND_IKASHOP_ULTIMATE
	void CShopManager::RecvPriceAverageRequestClientPacket(LPCHARACTER ch, DWORD vnum, DWORD count)
	{
		if(!ch || !ch->GetDesc())
			return;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_PRICE_AVERAGE_REQUEST))
			return;

		auto average = GetAveragePrice(vnum, count);

		TPacketGCNewIkarusShop pack;
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.subheader = ikashop::SUBHEADER_GC_PRICE_AVERAGE_RESPONSE;
		pack.size = sizeof(pack) + sizeof(average);

		ch->GetDesc()->BufferedPacket(pack);
		ch->GetDesc()->Packet(average);
	}

	void CShopManager::RecvShopDecorationUseClientPacket(LPCHARACTER ch, int index)
	{
		if(!CheckCharacterActions(ch))
			return;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_SHOP_DECORATION_USE))
			return;

		if(!ch->GetIkarusShop())
			return;

		if(ch->CountSpecifyItem(52000) == 0)
			return;

		ch->RemoveSpecifyItem(52000);

		TPacketGDNewIkarusShop pack{};
		pack.bSubHeader = ikashop::SUBHEADER_GD_SHOP_DECORATION_USE;

		ikashop::TSubPacketGDShopDecorationUse subpack{};
		subpack.index = index;
		subpack.owner = ch->GetPlayerID();

		TEMP_BUFFER buffer;
		buffer.write(&pack, sizeof(pack));
		buffer.write(&subpack, sizeof(subpack));

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buffer.read_peek(), buffer.size());
	}

	void CShopManager::RecvShopMoveItemClientPacket(LPCHARACTER ch, int srcPos, int destPos)
	{
		// getting the shop
		auto shop = ch->GetIkarusShop();
		if(!shop)
			return;

		if (!ch->IkarusShopFloodCheck(SHOP_ACTION_WEIGHT_SHOP_MOVE_ITEM))
			return;

		// getting item
		auto& items = shop->GetItems();
		auto iter = std::find_if(items.begin(), items.end(), [&](auto&& iter) { return iter.second->GetInfo().pos == srcPos; });
		if(iter == shop->GetItems().end())
			return;

		auto item = iter->second;

		// getting item size
		auto table = item->GetTable();
		if(!table)
			return;

		if (!shop->ReserveSpace(destPos, table->bSize))
			return;

		// sending db packet
		TPacketGDNewIkarusShop pack;
		pack.bSubHeader = ikashop::SUBHEADER_GD_SHOP_MOVE_ITEM;

		ikashop::TSubPacketGDShopMoveItem subpack;
		subpack.owner = ch->GetPlayerID();
		subpack.itemid = item->GetID();
		subpack.destCell = destPos;

		TEMP_BUFFER	buffer;
		buffer.write(&pack, sizeof(pack));
		buffer.write(&subpack, sizeof(subpack));

		db_clientdesc->DBPacket(HEADER_GD_NEW_OFFLINESHOP, 0, buffer.read_peek(), buffer.size());
	}
#endif

	void CShopManager::SendPopupMessage(LPCHARACTER ch, const std::string& message)
	{
		ikashop::TSubPacketGCPopupMessage subpack{};
		str_to_cstring(subpack.localeString, message.data());

		TPacketGCNewIkarusShop pack{};
		pack.header = HEADER_GC_NEW_OFFLINESHOP;
		pack.size = sizeof(pack) + sizeof(subpack);
		pack.subheader = ikashop::SUBHEADER_GC_POPUP_MESSAGE;

		ch->GetDesc()->BufferedPacket(pack);
		ch->GetDesc()->Packet(subpack);
	}

	void CShopManager::UpdateShopsDuration()
	{
		for(auto& [id, shop] : m_mapShops)
			if(shop->GetDuration() > 0)
				shop->DecreaseDuration();
	}

	void CShopManager::UpdateAuctionsDuration()
	{
		for(auto& [id, auction] : m_mapAuctions)
			auction->DecreaseDuration();
	}

	void CShopManager::ClearSearchTimeMap()
	{
		m_searchCooldownMap.clear();
		m_offerCooldownMap.clear();
	}

	bool CShopManager::CheckOfferCooldown(DWORD pid) {
		static const auto cooldown_seconds = OFFLINESHOP_SECONDS_PER_OFFER * 1000;
		const auto now = get_dword_time();

		auto it = m_offerCooldownMap.find(pid);
		if (it == m_offerCooldownMap.end()) {
			m_offerCooldownMap[pid] = now + cooldown_seconds ;
			return true;
		}

		auto& cooldown = it->second;
		if (cooldown > now)
			return false;

		cooldown = now + cooldown_seconds;
		return true;
	}

	bool CShopManager::CheckSafeboxSize(LPCHARACTER ch)
	{
		// validating character pointer
		if(!ch)
			return false;

		// searching for safebox
		auto safebox = ch->GetIkarusShopSafebox() ? ch->GetIkarusShopSafebox() : GetShopSafeboxByOwnerID(ch->GetPlayerID());
		if(!safebox)
			return false;

		// checking safebox size
		if(safebox->GetItems().size() >= OFFLINESHOP_MAX_SAFEBOX_SIZE)
		{
			SendPopupMessage(ch, "IKASHOP_SERVER_POPUP_MESSAGE_SAFEBOX_FULL");
			return false;
		}

		return true;
	}

	bool CShopManager::CheckSearchCooldown(DWORD pid)
	{
		static const auto cooldown_seconds = OFFLINESHOP_SECONDS_PER_SEARCH * 1000;
		const auto now = get_dword_time();

		auto it = m_searchCooldownMap.find(pid);
		if (it == m_searchCooldownMap.end()) {
			m_searchCooldownMap[pid] = now + cooldown_seconds;
			return true;
		}

		auto& cooldown = it->second;
		if (cooldown > now)
			return false;

		cooldown = now + cooldown_seconds;
		return true;
	}
}

#endif