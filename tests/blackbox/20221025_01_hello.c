#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "hello world 2022724 11.550000!!!");
}

int main(void) {
    TEST_BASIC
}