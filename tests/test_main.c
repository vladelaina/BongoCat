#include "test.h"

int bongo_cat_test_failures;

int main(void) {
    test_config();
    test_input();
    test_models();
    test_modifier_state();
    test_mver_pointer();
    test_shortcut();
    if (bongo_cat_test_failures) {
        fprintf(stderr, "%d checks failed\n", bongo_cat_test_failures);
        return 1;
    }
    puts("all core checks passed");
    return 0;
}
