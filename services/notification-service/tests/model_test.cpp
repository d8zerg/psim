#include <gtest/gtest.h>

#include <string>

#include "psim/platform/security/http.hpp"
#include "transport.hpp"
using namespace psim::notification_service;

TEST(NotificationTransport, EmailHeadersAndHmac) {
  EXPECT_TRUE(valid_email("operator+site@example.test"));
  for (const auto* value : {"", "@x", "x@", "a@@b", "a@b\r\nBcc: a@b", "a<b>@x", "a b@x"}) {
    EXPECT_FALSE(valid_email(value));
  }
  EXPECT_TRUE(signature("key", "The quick brown fox jumps over the lazy dog")
              == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
  const Transport transport({.hmac_secret = std::string(32, 'x'), .webhook_urls = {"https://receiver.example/hook"}});
  EXPECT_TRUE(transport.validate_webhook("https://receiver.example/hook"));
  EXPECT_FALSE(transport.validate_webhook("https://receiver.example/other"));
  EXPECT_FALSE(transport.validate_webhook("http://receiver.example/hook"));
}

TEST(NotificationTransport, RejectsInternalNetworks) {
  for (const auto* value : {"0.0.0.0",
                            "10.0.0.1",
                            "100.64.0.1",
                            "127.0.0.1",
                            "169.254.169.254",
                            "172.31.2.3",
                            "192.168.1.2",
                            "192.0.2.1",
                            "192.88.99.1",
                            "198.18.0.1",
                            "198.19.255.255",
                            "198.51.100.2",
                            "203.0.113.2",
                            "224.0.0.1",
                            "255.255.255.255",
                            "::1",
                            "fc00::1",
                            "fe80::1",
                            "::ffff:8.8.8.8",
                            "64:ff9b::808:808",
                            "2001:db8::1",
                            "2001:2::1",
                            "3fff::1",
                            "2002:808:808::1",
                            "bad"}) {
    EXPECT_FALSE(p::security::public_address(value));
  }
  for (const auto* value : {"8.8.8.8", "192.31.196.1", "198.1.1.1", "203.1.1.1"}) {
    EXPECT_TRUE(p::security::public_address(value));
  }
  EXPECT_TRUE(p::security::public_address("2606:4700:4700::1111"));
}
