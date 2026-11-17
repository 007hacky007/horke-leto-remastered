#include <doctest/doctest.h>
#include "render/cache.h"
using namespace hl;
TEST_SUITE("render.cache") {
TEST_CASE("LRU evicts unpinned entries and never exceeds its byte bound") {
 TextureCache<int> c(8);auto a=c.insert("original/a/0/palette/grade/atlas",4,std::make_shared<int>(10));REQUIRE(a.has_value());a.value().reset();
 auto b=c.insert("remastered/a/0/palette/grade/atlas",4,std::make_shared<int>(20));REQUIRE(b.has_value());
 CHECK_FALSE(c.insert("c",8,std::make_shared<int>(30)).has_value());CHECK(c.bytes()<=8);
 b.value().reset();auto d=c.insert("d",8,std::make_shared<int>(40));REQUIRE(d.has_value());CHECK_FALSE(c.find("original/a/0/palette/grade/atlas"));CHECK(c.bytes()==8);
 for(int i=0;i<100;++i){d.value().reset();d=c.insert(std::to_string(i),8,std::make_shared<int>(i));REQUIRE(d.has_value());CHECK(*d.value()==i);CHECK(c.bytes()==8);}
 CHECK_FALSE(c.insert("oversize",9,std::make_shared<int>(0)).has_value());
}
TEST_CASE("cache hits refresh recency and pinned textures survive repeated switches") {TextureCache<int> c(12);for(auto k:{"a","b","c"})REQUIRE(c.insert(k,4,std::make_shared<int>(1)).has_value());auto a=c.find("a");REQUIRE(a);REQUIRE(c.insert("d",4,std::make_shared<int>(2)).has_value());CHECK_FALSE(c.find("b"));CHECK(c.find("c"));CHECK(*a==1);}
}

TEST_SUITE("render.cache") {
TEST_CASE("upload and eviction counters distinguish warm hits from cold inserts") {
 TextureCache<int> cache(4);
 CHECK(cache.insertions()==0);CHECK(cache.evictions()==0);
 REQUIRE(cache.insert("a",4,std::make_shared<int>(1)).has_value());
 CHECK(cache.insertions()==1);CHECK(cache.evictions()==0);
 REQUIRE(cache.find("a"));
 REQUIRE(cache.insert("a",4,std::make_shared<int>(2)).has_value());
 CHECK(cache.insertions()==1);
 REQUIRE(cache.insert("b",4,std::make_shared<int>(3)).has_value());
 CHECK(cache.insertions()==2);CHECK(cache.evictions()==1);
 CHECK_FALSE(cache.insert("large",8,std::make_shared<int>(4)).has_value());
 CHECK(cache.insertions()==2);CHECK(cache.evictions()==1);
}
}
