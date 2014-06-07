#include "MockMc.h"

#include "mcrouter/lib/McRequest.h"

namespace facebook { namespace memcache {

void MockMc::CacheItem::updateToken() {
  static uint64_t leaseCounter = 100;
  token = leaseCounter++;
}

MockMc::Item::Item(McStringData v)
    : value(std::move(v)) {
}

MockMc::Item::Item(const McRequest& req)
    : value(req.value()),
      exptime(req.exptime() > 0 ? req.exptime() + time(nullptr) : 0),
      flags(req.flags()) {
}

MockMc::Item* MockMc::get(folly::StringPiece key) {
  auto it = findUnexpired(key);
  if (it == citems_.end() || it->second.state != CacheItem::CACHE) {
    return nullptr;
  }
  return &it->second.item;
}

void MockMc::set(folly::StringPiece key, Item item) {
  citems_.erase(key.str());
  citems_.insert(std::make_pair(key.str(), CacheItem(std::move(item))));
}

bool MockMc::add(folly::StringPiece key, Item item) {
  if (get(key)) {
    return false;
  }
  set(key, std::move(item));
  return true;
}

bool MockMc::replace(folly::StringPiece key, Item item) {
  if (!get(key)) {
    return false;
  }
  set(key, std::move(item));
  return true;
}

std::pair<bool, int64_t> MockMc::arith(folly::StringPiece key, int64_t delta) {
  auto item = get(key);
  if (!item) {
    return std::make_pair(false, 0);
  }

  auto oldval = folly::to<uint64_t>(item->value.dataRange());
  auto newval = folly::to<std::string>(oldval + delta);
  item->value = McStringData(std::move(newval));
  return std::make_pair(true, oldval + delta);
}

bool MockMc::del(folly::StringPiece key) {
  auto it = findUnexpired(key);
  if (it != citems_.end()) {
    bool deleted = false;
    /* Delete moves items from CACHE to TLRU, and always bumps lease tokens */
    if (it->second.state == CacheItem::CACHE) {
      deleted = true;
      it->second.state = CacheItem::TLRU;
    }
    it->second.updateToken();
    return deleted;
  }
  return false;
}

/**
 * @return:  (item, 0)            On a hit.
 *           (stale_item, token)  On a miss.
 *           (stale_item, 1)      On a hot miss (another lease outstanding).
 */
std::pair<MockMc::Item*, uint64_t> MockMc::leaseGet(folly::StringPiece key) {
  auto it = findUnexpired(key);
  if (it == citems_.end()) {
    /* Lease get on a non-existing item: create a new empty item and
       put it in TLRU with valid token */
    it = citems_.insert(
      std::make_pair(key.str(), CacheItem(Item(McStringData(""))))).first;
    it->second.state = CacheItem::TLRU;
    it->second.updateToken();
  }

  auto& citem = it->second;
  switch (citem.state) {
    case CacheItem::CACHE:
      /* Regular hit */
      return std::make_pair(&citem.item, 0);
    case CacheItem::TLRU:
      /* First lease-get for a TLRU item, return with a valid token */
      citem.state = CacheItem::TLRU_HOT;
      return std::make_pair(&citem.item, citem.token);
    case CacheItem::TLRU_HOT:
      /* TLRU item with other lease-gets pending, return a hot miss token
         (special value 1). Note: in real memcached this state would
         revert to TLRU after a timeout. */
      return std::make_pair(&citem.item, 1);
  }

  CHECK(false);
}

MockMc::LeaseSetResult MockMc::leaseSet(folly::StringPiece key, Item item,
                                        uint64_t token) {
  auto it = findUnexpired(key);
  if (it == citems_.end()) {
    /* Item doesn't exist in cache or TLRU */
    return NOT_STORED;
  }

  auto& citem = it->second;
  if (citem.state == CacheItem::CACHE ||
      citem.token == token) {
    /* Either the item is a hit, or the token is valid. Regular set */
    set(key, std::move(item));
    return STORED;
  } else {
    /* The token is not valid (expired or wrong), but the value is in TLRU.
       Update the value but don't promote to cache. */
    citem.item = std::move(item);
    return STALE_STORED;
  }
}

std::unordered_map<std::string, MockMc::CacheItem>::iterator
MockMc::findUnexpired(folly::StringPiece key) {
  auto it = citems_.find(key.str());
  if (it == citems_.end()) {
    return it;
  }

  if (it->second.item.exptime > 0
      && it->second.item.exptime <= time(nullptr)) {
    citems_.erase(it);
    return citems_.end();
  }

  return it;
}

}}  // facebook::memcache
