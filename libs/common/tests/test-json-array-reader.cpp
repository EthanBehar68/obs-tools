#include <doctest.h>

#include "core/json-array-reader.hpp"

using namespace unified_chat;

TEST_CASE("JsonArrayReader yields each object as soon as it closes")
{
	JsonArrayReader reader;
	CHECK(reader.Feed("[{\"a\":1}").size() == 1);
	auto next = reader.Feed("\r\n,{\"b\":{\"c\":[1,2]}}");
	REQUIRE(next.size() == 1);
	CHECK(next[0] == "{\"b\":{\"c\":[1,2]}}");
	CHECK_FALSE(reader.Finished());
	CHECK(reader.Feed("\n]\n").empty());
	CHECK(reader.Finished());
	CHECK_FALSE(reader.Failed());
}

TEST_CASE("JsonArrayReader handles objects split at any byte")
{
	const std::string body = R"([{"text":"a } ] { [ \" \\ b","n":{"x":1}} , {"y":2}])";
	for (size_t split = 0; split <= body.size(); ++split) {
		CAPTURE(split);
		JsonArrayReader reader;
		auto objects = reader.Feed(std::string_view(body).substr(0, split));
		auto rest = reader.Feed(std::string_view(body).substr(split));
		objects.insert(objects.end(), rest.begin(), rest.end());
		REQUIRE(objects.size() == 2);
		CHECK(objects[0] == R"({"text":"a } ] { [ \" \\ b","n":{"x":1}})");
		CHECK(objects[1] == R"({"y":2})");
		CHECK(reader.Finished());
	}
}

TEST_CASE("JsonArrayReader rejects what isn't an array of objects")
{
	JsonArrayReader error;
	error.Feed(R"({"error":{"code":403}})");
	CHECK(error.Failed());

	JsonArrayReader scalars;
	scalars.Feed("[1,2]");
	CHECK(scalars.Failed());

	JsonArrayReader trailing;
	trailing.Feed("[{}]x");
	CHECK(trailing.Failed());

	JsonArrayReader empty;
	CHECK(empty.Feed(" [ ] ").empty());
	CHECK(empty.Finished());
}
