#include <unity.h>

#include <math.h>

#include "DiceApp.h"

void setUp(void) {}
void tearDown(void) {}

// A game played tick by tick, every 20 ms.
struct Game {
    XorShift random{42};
    DiceApp app{random};
    uint32_t now = 0;

    Game() { app.begin(0); }

    void tick(const DiceApp::Input &in = {}) {
        now += 20;
        app.update(now, in);
    }
    void wait(uint32_t ms) {
        const uint32_t until = now + ms;
        while (now < until) {
            tick();
        }
    }
    void key1() {
        DiceApp::Input in{};
        in.key1 = true;
        tick(in);
    }
    void key2() {
        DiceApp::Input in{};
        in.key2 = true;
        tick(in);
    }
    void shake(uint32_t ms) {
        DiceApp::Input in{};
        in.shakeBegan = true;
        in.shaking = true;
        tick(in);
        in.shakeBegan = false;
        for (uint32_t t = 20; t < ms; t += 20) {
            tick(in);
        }
        DiceApp::Input end{};
        end.shakeEnded = true;
        tick(end);
    }
    DiceView view() const { return app.view(now); }
    void throwAndLand() {
        key1();
        TEST_ASSERT_TRUE(app.takeRollStarted());
        wait(RollPlanner::kDurationMs + DiceApp::kSoundLeadMs + 40);
    }
};

void test_starts_on_the_splash_with_two_floating_dice(void) {
    Game g;
    g.tick();
    const DiceView v = g.view();
    TEST_ASSERT_TRUE(v.screen == Screen::Splash);
    TEST_ASSERT_EQUAL(2, v.count);
    TEST_ASSERT_TRUE(v.moving);
}

void test_key1_on_the_splash_throws(void) {
    Game g;
    g.key1();
    TEST_ASSERT_TRUE(g.app.takeRollStarted());
    TEST_ASSERT_FALSE(g.app.takeRollStarted()); // only once
    TEST_ASSERT_TRUE(g.view().screen == Screen::Table);
    TEST_ASSERT_TRUE(g.app.rolling());
}

void test_starts_with_one_die(void) {
    Game g;
    TEST_ASSERT_EQUAL(1, g.app.mode());
    g.key1();
    g.wait(RollPlanner::kDepartMs);
    TEST_ASSERT_EQUAL(1, g.view().count);
}

void test_the_second_splash_die_flies_away(void) {
    Game g;
    g.wait(500);
    g.key1();
    DiceView v = g.view();
    TEST_ASSERT_EQUAL(2, v.count); // still there, leaving
    const float startX = v.dice[1].position.x;
    g.wait(300);
    v = g.view();
    TEST_ASSERT_EQUAL(2, v.count);
    TEST_ASSERT_TRUE(fabsf(v.dice[1].position.x) > fabsf(startX));
    g.wait(RollPlanner::kDepartMs);
    TEST_ASSERT_EQUAL(1, g.view().count);
}

void test_a_throw_lands_with_the_values_shown_on_top(void) {
    Game g;
    g.key2(); // to the table
    g.key2(); // two dice
    g.wait(DiceApp::kToastMs);
    g.throwAndLand();
    const DiceView v = g.view();
    TEST_ASSERT_FALSE(v.moving);
    TEST_ASSERT_TRUE(v.showResult);
    TEST_ASSERT_FALSE(v.showHint);
    // The result reads top to bottom.
    const int top = v.dice[0].position.y < v.dice[1].position.y ? 0 : 1;
    TEST_ASSERT_EQUAL(DiceMesh::topValue(v.dice[top].orientation), v.result[0]);
    TEST_ASSERT_EQUAL(DiceMesh::topValue(v.dice[1 - top].orientation), v.result[1]);
}

void test_keys_are_ignored_during_a_throw(void) {
    Game g;
    g.key1();
    g.app.takeRollStarted();
    g.wait(500);
    g.key1();
    g.key2();
    TEST_ASSERT_FALSE(g.app.takeRollStarted());
    TEST_ASSERT_EQUAL(1, g.view().mode);
}

void test_key2_on_the_splash_lays_the_dice_on_the_table(void) {
    Game g;
    g.key2();
    const DiceView v = g.view();
    TEST_ASSERT_TRUE(v.screen == Screen::Table);
    TEST_ASSERT_EQUAL(1, v.mode);
    TEST_ASSERT_EQUAL(1, v.count);
    TEST_ASSERT_TRUE(v.showHint);
    TEST_ASSERT_FALSE(v.toast);
}

void test_key2_on_the_table_switches_the_mode_with_a_toast(void) {
    Game g;
    g.throwAndLand();
    const uint32_t before = g.view().sceneVersion;
    g.key2();
    DiceView v = g.view();
    TEST_ASSERT_EQUAL(2, v.mode);
    TEST_ASSERT_EQUAL(2, v.count);
    TEST_ASSERT_TRUE(v.toast);
    TEST_ASSERT_TRUE(v.showHint);
    TEST_ASSERT_FALSE(v.showResult);
    TEST_ASSERT_TRUE(v.sceneVersion != before);
    g.wait(DiceApp::kToastMs);
    TEST_ASSERT_FALSE(g.view().toast);
    g.key2();
    TEST_ASSERT_EQUAL(1, g.view().count);
}

void test_shaking_rattles_and_throws_when_it_stops(void) {
    Game g;
    g.key2(); // to the table
    DiceApp::Input in{};
    in.shakeBegan = true;
    in.shaking = true;
    g.tick(in);
    TEST_ASSERT_TRUE(g.app.shaking());
    TEST_ASSERT_TRUE(g.view().moving);
    TEST_ASSERT_FALSE(g.app.takeRollStarted());
    g.shake(500);
    TEST_ASSERT_TRUE(g.app.takeRollStarted());
    TEST_ASSERT_TRUE(g.app.rolling());
}

void test_screen_sleeps_after_a_minute_of_nothing(void) {
    Game g;
    g.throwAndLand();
    g.wait(59000);
    TEST_ASSERT_TRUE(g.app.awake());
    g.wait(1500);
    TEST_ASSERT_FALSE(g.app.awake());
}

void test_waking_shows_the_last_throw_without_throwing(void) {
    Game g;
    g.throwAndLand();
    const DiceView before = g.view();
    g.wait(61000);
    DiceApp::Input pick{};
    pick.pickedUp = true;
    g.tick(pick);
    TEST_ASSERT_TRUE(g.app.awake());
    TEST_ASSERT_FALSE(g.app.takeRollStarted());
    const DiceView after = g.view();
    TEST_ASSERT_TRUE(after.showResult);
    TEST_ASSERT_EQUAL(before.result[0], after.result[0]);
    TEST_ASSERT_EQUAL(before.result[1], after.result[1]);
    TEST_ASSERT_EQUAL_FLOAT(before.dice[0].position.x, after.dice[0].position.x);
}

void test_a_key_that_wakes_does_nothing_else(void) {
    Game g;
    g.throwAndLand();
    g.wait(61000);
    g.key1();
    TEST_ASSERT_TRUE(g.app.awake());
    TEST_ASSERT_FALSE(g.app.takeRollStarted());
    g.key2();
    TEST_ASSERT_EQUAL(2, g.view().mode); // the next key works
}

void test_a_shake_that_wakes_does_not_throw(void) {
    Game g;
    g.throwAndLand();
    g.wait(61000);
    g.shake(600);
    TEST_ASSERT_TRUE(g.app.awake());
    TEST_ASSERT_FALSE(g.app.takeRollStarted());
    g.shake(600); // a new shake throws
    TEST_ASSERT_TRUE(g.app.takeRollStarted());
}

void test_splash_sleeps_too_and_wakes_on_the_table(void) {
    Game g;
    g.wait(61000);
    TEST_ASSERT_FALSE(g.app.awake());
    g.key2();
    const DiceView v = g.view();
    TEST_ASSERT_TRUE(v.screen == Screen::Table);
    TEST_ASSERT_TRUE(v.showHint);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_on_the_splash_with_two_floating_dice);
    RUN_TEST(test_key1_on_the_splash_throws);
    RUN_TEST(test_starts_with_one_die);
    RUN_TEST(test_the_second_splash_die_flies_away);
    RUN_TEST(test_a_throw_lands_with_the_values_shown_on_top);
    RUN_TEST(test_keys_are_ignored_during_a_throw);
    RUN_TEST(test_key2_on_the_splash_lays_the_dice_on_the_table);
    RUN_TEST(test_key2_on_the_table_switches_the_mode_with_a_toast);
    RUN_TEST(test_shaking_rattles_and_throws_when_it_stops);
    RUN_TEST(test_screen_sleeps_after_a_minute_of_nothing);
    RUN_TEST(test_waking_shows_the_last_throw_without_throwing);
    RUN_TEST(test_a_key_that_wakes_does_nothing_else);
    RUN_TEST(test_a_shake_that_wakes_does_not_throw);
    RUN_TEST(test_splash_sleeps_too_and_wakes_on_the_table);
    return UNITY_END();
}
