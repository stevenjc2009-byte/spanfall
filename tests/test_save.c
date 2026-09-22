#include <stdio.h>
#include <string.h>

#include "../src/save.h"
#include "check.h"

#define PATH "tests/build/save_test.dat"

static void remove_all(void)
{
    remove(PATH);
    remove(PATH ".tmp");
    remove(PATH ".bak");
}

static void write_raw(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, n, f);
        fclose(f);
    }
}

static void encode_round_trips(void)
{
    static const uint32_t values[] = {0, 1, 1280, 0xFFFFFFFFu};
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
        uint8_t blob[SAVE_SIZE];
        uint32_t back = 12345;
        save_encode(values[i], blob);
        CHECK_EQ(save_decode(blob, SAVE_SIZE, &back), 0);
        CHECK_EQ(back, values[i]);
    }
}

static void decode_rejects_damage(void)
{
    uint8_t blob[SAVE_SIZE];
    uint32_t back = 7;
    save_encode(500, blob);
    CHECK_EQ(save_decode(blob, SAVE_SIZE - 1, &back), -1); /* short */
    for (int i = 0; i < SAVE_SIZE; i++) {                /* any flipped bit */
        blob[i] ^= 0x10;
        CHECK_EQ(save_decode(blob, SAVE_SIZE, &back), -1);
        blob[i] ^= 0x10;
    }
    CHECK_EQ(back, 7); /* untouched on failure */
}

static void write_then_load(void)
{
    uint32_t best = 99;
    remove_all();
    CHECK_EQ(save_load(PATH, &best), SAVE_MISSING);
    CHECK_EQ(best, 0);

    CHECK_EQ(save_write(PATH, 1280), 0);
    CHECK_EQ(save_load(PATH, &best), SAVE_OK);
    CHECK_EQ(best, 1280);

    CHECK_EQ(save_write(PATH, 4000), 0); /* replaces an existing save */
    CHECK_EQ(save_load(PATH, &best), SAVE_OK);
    CHECK_EQ(best, 4000);
    remove_all();
}

static void damaged_save_falls_back_to_the_backup(void)
{
    uint32_t best = 0;
    remove_all();
    CHECK_EQ(save_write(PATH, 300), 0);
    CHECK_EQ(save_write(PATH, 800), 0); /* 300 is now the .bak */
    write_raw(PATH, "garbage-garbage", 15);
    CHECK_EQ(save_load(PATH, &best), SAVE_OK);
    CHECK_EQ(best, 300);

    write_raw(PATH ".bak", "junk", 4); /* nothing valid left */
    CHECK_EQ(save_load(PATH, &best), SAVE_CORRUPT);
    CHECK_EQ(best, 0);
    remove_all();
}

void test_save(void)
{
    encode_round_trips();
    decode_rejects_damage();
    write_then_load();
    damaged_save_falls_back_to_the_backup();
}
