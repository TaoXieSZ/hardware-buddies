// Native (off-device) unit tests for src/stackchan_voice/wifi_store.h.
// Run with:  pio test -e native
//
// wifi_store 是小咪"带着上下班"的地基：家里/公司各存一次，到哪儿自动连哪儿。
// 槽位语义搞错的后果都很难在设备上察觉——改密码变成新增一条（吃掉槽位且让
// WiFiMulti 拿到冲突记录）、存满时静默淘汰掉用户存过的网络、删掉的出厂网络
// 下次启动又复活——所以在这里钉死。

#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "wifi_store.h"

using wifi_store::PutResult;
using wifi_store::Store;

void setUp() {}
void tearDown() {}

void test_starts_empty() {
    Store s;
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)s.count());
    TEST_ASSERT_NULL(s.at(0));
    TEST_ASSERT_FALSE(s.has("home"));
    TEST_ASSERT_FALSE(s.seeded());
}

void test_put_adds_and_reads_back() {
    Store s;
    TEST_ASSERT_TRUE(PutResult::Added == s.put("home", "pw123456"));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)s.count());
    TEST_ASSERT_TRUE(s.has("home"));
    const auto* e = s.at(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("home", e->ssid);
    TEST_ASSERT_EQUAL_STRING("pw123456", e->pass);
}

void test_same_ssid_updates_not_appends() {
    // 改密码必须覆盖：追加的话会吃掉槽位，且 WiFiMulti 会拿到两条冲突记录。
    Store s;
    s.put("office", "old");
    TEST_ASSERT_TRUE(PutResult::Updated == s.put("office", "new"));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)s.count());
    TEST_ASSERT_EQUAL_STRING("new", s.at(0)->pass);
}

void test_full_reports_error_not_silent_drop() {
    Store s;
    char name[8];
    for (size_t i = 0; i < wifi_store::MAX_NETWORKS; ++i) {
        snprintf(name, sizeof(name), "net%u", (unsigned)i);
        TEST_ASSERT_TRUE(PutResult::Added == s.put(name, "pw"));
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)wifi_store::MAX_NETWORKS, (uint32_t)s.count());
    // 满了必须明确报错 —— 静默淘汰用户存过的网络比报错更糟
    TEST_ASSERT_TRUE(PutResult::Full == s.put("onemore", "pw"));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)wifi_store::MAX_NETWORKS, (uint32_t)s.count());
    TEST_ASSERT_TRUE(s.has("net0"));   // 最早那条还在
    TEST_ASSERT_FALSE(s.has("onemore"));
}

void test_full_still_allows_updating_existing() {
    Store s;
    char name[8];
    for (size_t i = 0; i < wifi_store::MAX_NETWORKS; ++i) {
        snprintf(name, sizeof(name), "net%u", (unsigned)i);
        s.put(name, "pw");
    }
    // 槽位满不该妨碍改已存网络的密码
    TEST_ASSERT_TRUE(PutResult::Updated == s.put("net2", "newpw"));
    TEST_ASSERT_EQUAL_STRING("newpw", s.at(2)->pass);
}

void test_remove_frees_slot() {
    Store s;
    s.put("a", "1");
    s.put("b", "2");
    TEST_ASSERT_TRUE(s.remove("a"));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)s.count());
    TEST_ASSERT_FALSE(s.has("a"));
    TEST_ASSERT_TRUE(s.has("b"));
    TEST_ASSERT_FALSE(s.remove("a"));    // 删不存在的返回 false
    // 腾出来的槽位可以再用
    TEST_ASSERT_TRUE(PutResult::Added == s.put("c", "3"));
}

void test_open_network_empty_password_allowed() {
    Store s;
    TEST_ASSERT_TRUE(PutResult::Added == s.put("guest", ""));
    TEST_ASSERT_EQUAL_STRING("", s.at(0)->pass);
    TEST_ASSERT_TRUE(PutResult::Updated == s.put("guest", nullptr));
}

void test_invalid_ssid_rejected() {
    Store s;
    TEST_ASSERT_TRUE(PutResult::Invalid == s.put(nullptr, "pw"));
    TEST_ASSERT_TRUE(PutResult::Invalid == s.put("", "pw"));
    char too_long[wifi_store::SSID_CAP + 10];
    memset(too_long, 'x', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = 0;
    TEST_ASSERT_TRUE(PutResult::Invalid == s.put(too_long, "pw"));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)s.count());
}

void test_too_long_password_rejected() {
    Store s;
    char too_long[wifi_store::PASS_CAP + 10];
    memset(too_long, 'p', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = 0;
    TEST_ASSERT_TRUE(PutResult::Invalid == s.put("home", too_long));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)s.count());
}

void test_seed_flag_survives_removal() {
    // 出厂种子灌过一次后，用户删掉那个网络，下次启动不能复活它。
    Store s;
    s.put("factory", "pw");
    s.markSeeded();
    TEST_ASSERT_TRUE(s.seeded());
    s.remove("factory");
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)s.count());
    TEST_ASSERT_TRUE(s.seeded());   // 标记仍在 → 启动逻辑不会再灌
}

void test_clear_wipes_entries() {
    Store s;
    s.put("a", "1");
    s.put("b", "2");
    s.clear();
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)s.count());
    TEST_ASSERT_FALSE(s.has("a"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_empty);
    RUN_TEST(test_put_adds_and_reads_back);
    RUN_TEST(test_same_ssid_updates_not_appends);
    RUN_TEST(test_full_reports_error_not_silent_drop);
    RUN_TEST(test_full_still_allows_updating_existing);
    RUN_TEST(test_remove_frees_slot);
    RUN_TEST(test_open_network_empty_password_allowed);
    RUN_TEST(test_invalid_ssid_rejected);
    RUN_TEST(test_too_long_password_rejected);
    RUN_TEST(test_seed_flag_survives_removal);
    RUN_TEST(test_clear_wipes_entries);
    return UNITY_END();
}
