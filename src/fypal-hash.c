/*
 * fypal-hash.c - chained string hash for name lookups
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "fypal-internal.h"

#define HASH_INITIAL_BUCKETS	64

/* FNV-1a over the bytes of the key. */
static uint32_t hash_key(const char *key, size_t len)
{
	uint32_t h = 2166136261U;
	size_t i;

	for (i = 0; i < len; i++) {
		h ^= (unsigned char)key[i];
		h *= 16777619U;
	}
	return h;
}

struct fypal_hash_entry *fypal_hash_lookup_(const struct fypal_hash *h,
					    const char *key, size_t len)
{
	struct fypal_hash_entry *e;
	uint32_t hv;

	if (!h->nbuckets)
		return NULL;
	hv = hash_key(key, len);
	for (e = h->buckets[hv & (h->nbuckets - 1)]; e; e = e->next) {
		if (e->hash == hv && e->len == len && !memcmp(e->key, key, len))
			return e;
	}
	return NULL;
}

void *fypal_hash_find_(const struct fypal_hash *h, const char *key, size_t len)
{
	struct fypal_hash_entry *e = fypal_hash_lookup_(h, key, len);

	return e ? e->value : NULL;
}

static int hash_grow(struct fypal_hash *h)
{
	struct fypal_hash_entry **nb, *e, *next;
	size_t n, i;

	n = h->nbuckets ? h->nbuckets * 2 : HASH_INITIAL_BUCKETS;
	nb = calloc(n, sizeof(*nb));
	if (!nb)
		return -1;
	for (i = 0; i < h->nbuckets; i++) {
		for (e = h->buckets[i]; e; e = next) {
			next = e->next;
			e->next = nb[e->hash & (n - 1)];
			nb[e->hash & (n - 1)] = e;
		}
	}
	free(h->buckets);
	h->buckets = nb;
	h->nbuckets = n;
	return 0;
}

int fypal_hash_insert_(struct fypal_hash *h, const char *key, size_t len,
		       void *value, bool own_key)
{
	struct fypal_hash_entry *e;
	char *copy = NULL;

	/* keep the load factor at or under one entry per bucket */
	if (h->count >= h->nbuckets && hash_grow(h))
		return -1;
	e = malloc(sizeof(*e));
	if (!e)
		return -1;
	if (own_key) {
		copy = malloc(len + 1);
		if (!copy) {
			free(e);
			return -1;
		}
		memcpy(copy, key, len);
		copy[len] = '\0';
		key = copy;
	}
	e->key = key;
	e->len = len;
	e->hash = hash_key(key, len);
	e->value = value;
	e->own_key = own_key;
	e->next = h->buckets[e->hash & (h->nbuckets - 1)];
	h->buckets[e->hash & (h->nbuckets - 1)] = e;
	h->count++;
	return 0;
}

void fypal_hash_clear_(struct fypal_hash *h)
{
	struct fypal_hash_entry *e, *next;
	size_t i;

	for (i = 0; i < h->nbuckets; i++) {
		for (e = h->buckets[i]; e; e = next) {
			next = e->next;
			if (e->own_key)
				free((char *)e->key);
			free(e);
		}
		h->buckets[i] = NULL;
	}
	h->count = 0;
}

void fypal_hash_release_(struct fypal_hash *h)
{
	fypal_hash_clear_(h);
	free(h->buckets);
	h->buckets = NULL;
	h->nbuckets = 0;
}
