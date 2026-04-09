#include "MeshCache.h"

namespace chisel::csg {

MeshCache::MeshCache(std::size_t maxEntries)
    : m_maxEntries(maxEntries) {}

manifold::Manifold MeshCache::getOrCompute(const std::string& key,
                                            std::function<manifold::Manifold()> compute) {
    auto it = m_map.find(key);
    if (it != m_map.end()) {
        touch(key, it->second.second);
        return it->second.first.mesh;
    }

    // Cache miss — compute, then insert
    manifold::Manifold mesh = compute();
    if (m_map.size() >= m_maxEntries)
        evictLru();

    m_lru.push_front(key);
    m_map.emplace(key, MapEntry{{mesh}, m_lru.begin()});
    return mesh;
}

void MeshCache::clear() {
    m_map.clear();
    m_lru.clear();
}

void MeshCache::evictLru() {
    if (m_lru.empty()) return;
    const std::string& oldest = m_lru.back();
    m_map.erase(oldest);
    m_lru.pop_back();
}

void MeshCache::touch(const std::string& key, LruIter it) {
    m_lru.erase(it);
    m_lru.push_front(key);
    m_map.at(key).second = m_lru.begin();
}

} // namespace chisel::csg
