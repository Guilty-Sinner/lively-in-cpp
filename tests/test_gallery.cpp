// GalleryClient tests: fake backend exercises auth (incl. the C# quirk of
// both providers POSTing to auth/google-token), token refresh on 401, search
// URL construction, subscriptions and health.

#include <catch2/catch_test_macros.hpp>

#include "test_http_server.h"

#include <lively/gallery/gallery_client.h>

#include <string>

using namespace lively;

TEST_CASE("GalleryClient search URL matches C# construction", "[gallery]") {
    auto store = std::make_shared<gallery::MemoryTokenStore>();
    store->set("tok", "ref", "GOOGLE", "9999-01-01T00:00:00Z");

    std::string captured_target;
    lively_test::TestHttpServer server([&](const std::string& method, const std::string& target,
                                           const std::map<std::string, std::string>& headers,
                                           const std::string&) {
        captured_target = target;
        CHECK(method == "GET");
        CHECK(headers.at("Authorization") == "Bearer tok");

        const std::string body =
            "{\"Success\":true,\"StatusCode\":200,"
            "\"Data\":{\"Number\":1,\"NextPageAvailable\":true,\"Data\":[" 
            "{\"Id\":\"w1\",\"Title\":\"Sunset\",\"IsPreviewAvailable\":true,"
            "\"VoteCount\":42,\"Type\":\"video\",\"Tags\":[\"nature\",\"4k\"],"
            "\"Author\":{\"Id\":\"u1\",\"DisplayName\":\"alice\"}}]}}";
        return lively_test::http_response(200, body);
    });

    gallery::GalleryClient client(server.base_url() + "/", "https://auth/google", "https://auth/github", store);

    gallery::SearchQueryBuilder builder;
    builder.sort_by(models::gallery::SortingType::newest).set_page(1).set_limit(10);
    builder.with_tags({"nature", "4k"}).with_search_query("sunset");
    const auto page = client.search_wallpapers(builder.build());

    // C# string interpolation — NOT url-encoded, sortBy is the enum name.
    CHECK(captured_target == "/gallery/search?sortBy=Newest&page=1&perPage=10"
                             "&query=sunset&tags=nature,4k");

    REQUIRE(page.data.size() == 1);
    CHECK(page.data[0].id == "w1");
    CHECK(page.data[0].vote_count == 42);
    // C# client-side URL assignment.
    REQUIRE(page.data[0].preview.has_value());
    CHECK(*page.data[0].preview == server.base_url() + "/gallery/w1/preview");
    REQUIRE(page.data[0].thumbnail.has_value());
    CHECK(*page.data[0].thumbnail == server.base_url() + "/gallery/w1/thumbnail");
    CHECK(page.next_page_available);
}

TEST_CASE("GalleryClient refreshes tokens once on 401", "[gallery]") {
    auto store = std::make_shared<gallery::MemoryTokenStore>();
    store->set("expired", "refresh-1", "GOOGLE", "9999-01-01T00:00:00Z");

    int profile_calls = 0;
    std::string auth_seen;
    std::string refresh_body;
    lively_test::TestHttpServer server([&](const std::string& method, const std::string& target,
                                           const std::map<std::string, std::string>& headers,
                                           const std::string& body) {
        if (target == "/users/@me") {
            ++profile_calls;
            auth_seen = headers.at("Authorization");
            if (profile_calls == 1) return lively_test::http_response(401, "{}");
            return lively_test::http_response(
                200, "{\"Success\":true,\"Data\":{\"Id\":\"u1\",\"DisplayName\":\"alice\"}}");
        }
        if (target == "/auth/refresh") {
            CHECK(method == "POST");
            refresh_body = body;
            return lively_test::http_response(
                200, "{\"Success\":true,\"Data\":{\"AccessToken\":\"fresh\",\"RefreshToken\":\"refresh-2\","
                     "\"Provider\":\"GOOGLE\",\"Expiration\":\"9999-01-01T00:00:00Z\"}}");
        }
        return lively_test::http_response(404, "{}");
    });

    gallery::GalleryClient client(server.base_url(), "a", "g", store);
    // C# populates CurrentUser via InitializeAsync (which calls GetMeAsync).
    client.initialize();
    const auto me = client.current_user();

    REQUIRE(me.has_value());
    CHECK(me->display_name == "alice");
    CHECK(profile_calls == 2);
    CHECK(auth_seen == "Bearer fresh");  // retried with the refreshed token
    CHECK(client.tokens().access_token == "fresh");
    CHECK(client.tokens().refresh_token == "refresh-2");
    CHECK(client.is_logged_in());
    // Refresh request body carries the old tokens (C# WithJsonContent(Tokens)).
    CHECK(refresh_body.find("\"AccessToken\":\"expired\"") != std::string::npos);
    CHECK(refresh_body.find("\"RefreshToken\":\"refresh-1\"") != std::string::npos);
}

TEST_CASE("GalleryClient auth endpoint quirk (both providers -> google-token)", "[gallery]") {
    auto store = std::make_shared<gallery::MemoryTokenStore>();
    std::string last_target;
    std::string last_method;

    lively_test::TestHttpServer server([&](const std::string& method, const std::string& target,
                                           const std::map<std::string, std::string>&,
                                           const std::string&) {
        if (target.rfind("/auth/google-token", 0) == 0) {
            last_target = target;
            last_method = method;
            return lively_test::http_response(
                200, "{\"Success\":true,\"Data\":{\"AccessToken\":\"t\",\"RefreshToken\":\"r\","
                     "\"Provider\":\"GITHUB\",\"Expiration\":\"9999-01-01T00:00:00Z\"}}");
        }
        if (target == "/users/@me") {
            return lively_test::http_response(
                200, "{\"Success\":true,\"Data\":{\"Id\":\"u2\",\"DisplayName\":\"bob\"}}");
        }
        return lively_test::http_response(404, "{}");
    });

    gallery::GalleryClient client(server.base_url(), "a", "g", store);
    const auto tokens = client.authenticate_github("code123");

    CHECK(last_method == "POST");
    CHECK(last_target == "/auth/google-token?code=code123&provider=GITHUB");
    CHECK(tokens.access_token == "t");
    REQUIRE(client.current_user().has_value());
    CHECK(client.current_user()->display_name == "bob");
}

TEST_CASE("GalleryClient subscription events and error semantics", "[gallery]") {
    auto store = std::make_shared<gallery::MemoryTokenStore>();
    store->set("tok", "ref", "GOOGLE", "9999-01-01T00:00:00Z");

    lively_test::TestHttpServer server([&](const std::string& method, const std::string& target,
                                           const std::map<std::string, std::string>&,
                                           const std::string&) {
        // Subscribe is PUT users/@me/wallpapers/{id} — the "already subscribed"
        // case is a 409 for that exact resource.
        if (target == "/users/@me/wallpapers/dup") {
            return lively_test::http_response(
                409, "{\"Success\":false,\"Errors\":[\"ALREADY_SUBSCRIBED_TO_WALLPAPER\"]}");
        }
        if (method == "PUT") return lively_test::http_response(200, "{\"Success\":true}");
        if (method == "DELETE") return lively_test::http_response(200, "{\"Success\":true}");
        if (target == "/users/@me/wallpapers") {
            return lively_test::http_response(
                200, "{\"Success\":true,\"Data\":[{\"Id\":\"w9\",\"IsPreviewAvailable\":false,\"VoteCount\":1}]}");
        }
        if (target == "/health") {
            return lively_test::http_response(
                200, "{\"Name\":\"lively\",\"Status\":\"Healthy\",\"Duration\":\"00:00:00.1\","
                     "\"Info\":[{\"Key\":\"db\",\"Status\":\"Healthy\",\"Duration\":\"00:00:00.01\"}]}");
        }
        return lively_test::http_response(404, "{}");
    });

    gallery::GalleryClient client(server.base_url(), "a", "g", store);

    bool subscribed = false, unsubscribed = false;
    std::string sub_id, unsub_id;
    client.wallpaper_subscribed.subscribe([&](const std::string& id) { subscribed = true; sub_id = id; });
    client.wallpaper_unsubscribed.subscribe([&](const std::string& id) { unsubscribed = true; unsub_id = id; });

    CHECK(client.subscribe_to_wallpaper("w1"));
    CHECK(subscribed);
    CHECK(sub_id == "w1");

    CHECK(client.unsubscribe_from_wallpaper("w1"));
    CHECK(unsubscribed);
    CHECK(unsub_id == "w1");

    const auto subs = client.get_wallpaper_subscriptions();
    REQUIRE(subs.size() == 1);
    CHECK(subs[0].id == "w9");
    CHECK_FALSE(subs[0].preview.has_value());  // preview only when available
    REQUIRE(subs[0].thumbnail.has_value());

    // AlreadySubscribed: event raised, then rethrow (C# behaviour).
    bool raised_before_throw = false;
    client.wallpaper_subscribed.subscribe([&](const std::string&) { raised_before_throw = true; });
    CHECK_THROWS_AS(client.subscribe_to_wallpaper("dup"), gallery::ApiException);
    CHECK(raised_before_throw);

    auto health = client.get_backend_health();
    REQUIRE(health.has_value());
    CHECK(health->status == "Healthy");
    REQUIRE(health->info.has_value());
    CHECK(health->info->size() == 1);
}

TEST_CASE("GalleryClient no-auth requests throw before any HTTP call", "[gallery]") {
    auto store = std::make_shared<gallery::MemoryTokenStore>();  // empty
    lively_test::TestHttpServer server([&](const std::string&, const std::string&,
                                           const std::map<std::string, std::string>&,
                                           const std::string&) {
        FAIL("no request should be sent without a token");
        return lively_test::http_response(500, "");
    });

    gallery::GalleryClient client(server.base_url(), "a", "g", store);
    CHECK_THROWS_AS(client.get_me(), gallery::UnauthorizedException);
}
