#include "check.h"

int g_checks, g_failures;

int main(void)
{
    test_sand();
    test_piece();
    test_game();
    test_save();
    printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
