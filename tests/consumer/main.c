// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/c_api.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    if (gbs_abi_version() != GBS_ABI_VERSION) {
        fputs("ABI version mismatch\n", stderr);
        return 1;
    }
    if (strcmp(gbs_contract_identifier(), GBS_CONTRACT_IDENTIFIER) != 0) {
        fputs("contract identifier mismatch\n", stderr);
        return 1;
    }
    if (gbs_product_version() == NULL || gbs_product_version()[0] == '\0') {
        fputs("product version is unavailable\n", stderr);
        return 1;
    }
    printf(
        "libgetbiblesword %s ABI %u %s\n",
        gbs_product_version(),
        (unsigned int)gbs_abi_version(),
        gbs_contract_identifier());
    return 0;
}
