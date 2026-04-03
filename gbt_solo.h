#ifndef GBT_SOLO_H
#define GBT_SOLO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <jansson.h>

struct work;

/* Solo HTTP + getblocktemplate for Soteria (soterg / ALGO_X12R); does not touch hash kernels. */
bool solo_mining_use_gbt(void);
void build_gbt_solo_request(char *buf, size_t buflen);
bool gbt_solo_decode(json_t *result, struct work *work);
void work_gbt_free(struct work *work);

#ifdef __cplusplus
}
#endif

#endif
