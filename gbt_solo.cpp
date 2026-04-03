/*
 * Solo mining via getblocktemplate + submitblock for Bitcoin-like chains.
 * Used for Soteria (soterg / ALGO_X12R): fills work->data / work->target for scanhash_x12r; does not change PoW.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <algorithm>
#include <vector>

#include <openssl/sha.h>
#include <jansson.h>

#include "algos.h"

extern "C" {
#include "sph/sph_ripemd.h"
#include "miner.h"
#include "compat.h"
#include "gbt_solo.h"
}

extern bool have_stratum;
extern bool allow_gbt;
extern char *opt_coinbase_addr;
extern void applog(int prio, const char *fmt, ...);

extern "C" void bn_nbits_to_uchar(const uint32_t nBits, unsigned char *target);

/* Jansson: json_integer_value is 0 for non-integers (e.g. JSON real). Parse all common forms. */
static bool gbt_json_to_int64(json_t *j, int64_t *out)
{
	if (!j || !out)
		return false;
	if (json_is_integer(j)) {
		*out = json_integer_value(j);
		return true;
	}
	if (json_is_real(j)) {
		double d = json_real_value(j);
		if (d < 1.0 || d > (double)UINT32_MAX)
			return false;
		*out = (int64_t)(uint32_t)(uint64_t)d;
		return true;
	}
	if (json_is_string(j)) {
		const char *s = json_string_value(j);
		if (!s || !*s)
			return false;
		char *end = NULL;
		errno = 0;
		/* base 0: C decimal or 0x hex */
		unsigned long long v = strtoull(s, &end, 0);
		if (end == s || errno == ERANGE)
			return false;
		*out = (int64_t)v;
		return true;
	}
	return false;
}

static void dsha256(const uint8_t *data, size_t len, uint8_t out[32])
{
	uint8_t t[32];
	SHA256(data, len, t);
	SHA256(t, 32, out);
}

static void hash160(const uint8_t *data, size_t len, uint8_t out[20])
{
	uint8_t h[32];
	sph_ripemd160_context ctx;
	SHA256(data, len, h);
	sph_ripemd160_init(&ctx);
	sph_ripemd160(&ctx, h, 32);
	sph_ripemd160_close(&ctx, out);
}

static void push_varint(std::vector<uint8_t> &v, uint64_t n)
{
	if (n < 0xfd) {
		v.push_back((uint8_t)n);
	} else if (n <= 0xffff) {
		v.push_back(0xfd);
		v.push_back((uint8_t)(n & 0xff));
		v.push_back((uint8_t)((n >> 8) & 0xff));
	} else if (n <= 0xffffffffu) {
		v.push_back(0xfe);
		for (int i = 0; i < 4; i++)
			v.push_back((uint8_t)((n >> (8 * i)) & 0xff));
	} else {
		v.push_back(0xff);
		for (int i = 0; i < 8; i++)
			v.push_back((uint8_t)((n >> (8 * i)) & 0xff));
	}
}

static void append_u32_le(std::vector<uint8_t> &v, uint32_t x)
{
	for (int i = 0; i < 4; i++)
		v.push_back((uint8_t)((x >> (8 * i)) & 0xff));
}

static void append_u64_le(std::vector<uint8_t> &v, uint64_t x)
{
	for (int i = 0; i < 8; i++)
		v.push_back((uint8_t)((x >> (8 * i)) & 0xff));
}

static void append_push_data(std::vector<uint8_t> &s, const uint8_t *d, size_t len)
{
	if (len < 76) {
		s.push_back((uint8_t)len);
	} else if (len < 256) {
		s.push_back(76);
		s.push_back((uint8_t)len);
	} else {
		s.push_back(77);
		s.push_back((uint8_t)(len & 0xff));
		s.push_back((uint8_t)((len >> 8) & 0xff));
	}
	s.insert(s.end(), d, d + len);
}

/* BIP34 height in coinbase */
static void script_height(std::vector<uint8_t> &s, int64_t height)
{
	uint8_t tmp[8];
	int n = 0;
	int64_t x = height;
	if (x == 0) {
		tmp[0] = 0;
		n = 1;
	} else {
		while (x > 0 && n < 8) {
			tmp[n++] = (uint8_t)(x & 0xff);
			x >>= 8;
		}
	}
	append_push_data(s, tmp, (size_t)n);
}

static const char b58digits[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

/* Base58Check decode (Bitcoin-style). */
static bool b58check_decode(const char *addr, std::vector<uint8_t> &out)
{
	out.clear();
	std::vector<uint8_t> vch;
	for (const char *p = addr; *p; p++) {
		const char *q = strchr(b58digits, *p);
		if (!q)
			return false;
		int carry = (int)(q - b58digits);
		for (size_t i = 0; i < vch.size(); i++) {
			carry += 58 * (int)vch[i];
			vch[i] = (uint8_t)(carry & 0xff);
			carry >>= 8;
		}
		while (carry > 0) {
			vch.push_back((uint8_t)(carry & 0xff));
			carry >>= 8;
		}
	}
	for (const char *p = addr; *p == '1'; p++)
		out.push_back(0);
	std::reverse(vch.begin(), vch.end());
	out.insert(out.end(), vch.begin(), vch.end());
	if (out.size() < 4)
		return false;
	size_t n = out.size() - 4;
	uint8_t h1[32], h2[32];
	SHA256(out.data(), n, h1);
	SHA256(h1, 32, h2);
	if (memcmp(h2, out.data() + n, 4) != 0)
		return false;
	out.resize(n);
	return out.size() >= 21;
}

static bool addr_to_pubkey_hash(const char *addr, uint8_t out20[20])
{
	std::vector<uint8_t> p;
	if (!b58check_decode(addr, p) || p.size() < 21)
		return false;
	memcpy(out20, p.data() + 1, 20);
	return true;
}

static void p2pkh_script(std::vector<uint8_t> &s, const uint8_t h160[20])
{
	s.push_back(0x76);
	s.push_back(0xa9);
	s.push_back(0x14);
	s.insert(s.end(), h160, h160 + 20);
	s.push_back(0x88);
	s.push_back(0xac);
}

static void hex_prev_to_work_words(const char *hex64, uint32_t *out8)
{
	uint8_t b[32];
	if (!hex2bin(b, hex64, 32))
		memset(b, 0, 32);
	/* GetHex() fully reverses the 32 bytes (MSB first), so words come out
	   in reversed order; fix by storing word i from the end. */
	for (int i = 0; i < 8; i++)
		out8[7 - i] = le32dec(b + i * 4);
}

/* Bitcoin merkle tree over 32-byte txids (double SHA256 pairs). */
static void merkle_compute(std::vector<std::vector<uint8_t> > &hashes, uint8_t root[32])
{
	while (hashes.size() > 1) {
		std::vector<std::vector<uint8_t> > next;
		for (size_t i = 0; i < hashes.size(); i += 2) {
			uint8_t cat[64];
			memcpy(cat, hashes[i].data(), 32);
			if (i + 1 < hashes.size())
				memcpy(cat + 32, hashes[i + 1].data(), 32);
			else
				memcpy(cat + 32, hashes[i].data(), 32);
			uint8_t h[32];
			dsha256(cat, 64, h);
			std::vector<uint8_t> hv(h, h + 32);
			next.push_back(hv);
		}
		hashes.swap(next);
	}
	if (hashes.empty())
		memset(root, 0, 32);
	else
		memcpy(root, hashes[0].data(), 32);
}

extern "C" bool solo_mining_use_gbt(void)
{
	return !have_stratum && opt_algo == ALGO_X12R;
}

extern "C" void build_gbt_solo_request(char *buf, size_t buflen)
{
	snprintf(buf, buflen,
		"{\"method\":\"getblocktemplate\",\"params\":[{\"capabilities\":"
		"[\"coinbasetxn\",\"coinbasevalue\",\"longpoll\",\"workid\"],"
		"\"rules\":[\"segwit\"]}],\"id\":0}\r\n");
}

extern "C" void work_gbt_free(struct work *work)
{
	if (!work)
		return;
	free(work->gbt_txs_hex);
	work->gbt_txs_hex = NULL;
	free(work->gbt_workid);
	work->gbt_workid = NULL;
	work->gbt_ntime_cap = 0;
}

/*
 * Build legacy serialization (no witness) for txid.
 */
static void serialize_tx_legacy(const std::vector<uint8_t> &vin, const std::vector<uint8_t> &vout,
	uint32_t version, uint32_t locktime, std::vector<uint8_t> &out)
{
	out.clear();
	append_u32_le(out, version);
	out.insert(out.end(), vin.begin(), vin.end());
	out.insert(out.end(), vout.begin(), vout.end());
	append_u32_le(out, locktime);
}

/*
 * Full segwit serialization: version | marker+flag | vin | vout | witness | locktime
 */
static void serialize_tx_witness(const std::vector<uint8_t> &vin, const std::vector<uint8_t> &vout,
	const std::vector<uint8_t> &witness,
	uint32_t version, uint32_t locktime, std::vector<uint8_t> &out)
{
	out.clear();
	append_u32_le(out, version);
	out.push_back(0x00);
	out.push_back(0x01);
	out.insert(out.end(), vin.begin(), vin.end());
	out.insert(out.end(), vout.begin(), vout.end());
	out.insert(out.end(), witness.begin(), witness.end());
	append_u32_le(out, locktime);
}

extern "C" bool gbt_solo_decode(json_t *result, struct work *work)
{
	json_t *j;
	const char *ph, *bitsstr, *tstr, *wcommit = NULL;
	int64_t curtime = 0, height = 0;
	int64_t cval = 0, fval = 0;
	uint32_t ver = 2;
	uint8_t merkle[32];

	if (!result || !json_is_object(result))
		return false;

	if (!opt_coinbase_addr || !strlen(opt_coinbase_addr)) {
		applog(LOG_ERR, "GBT solo: set --coinbase-addr to your payout address.");
		return false;
	}

	ph = json_string_value(json_object_get(result, "previousblockhash"));
	if (!ph || strlen(ph) != 64)
		return false;

	j = json_object_get(result, "version");
	if (j && json_is_integer(j))
		ver = (uint32_t)json_integer_value(j);

	j = json_object_get(result, "curtime");
	if (!gbt_json_to_int64(j, &curtime) || curtime <= 0) {
		applog(LOG_ERR, "GBT solo: missing or invalid curtime (need integer ntime for X12R)");
		return false;
	}

	j = json_object_get(result, "height");
	if (j && json_is_integer(j))
		height = json_integer_value(j);

	bitsstr = json_string_value(json_object_get(result, "bits"));
	if (!bitsstr)
		return false;

	uint8_t bits_raw[4];
	if (!hex2bin(bits_raw, bitsstr, 4))
		return false;
	uint32_t nbits = be32dec(bits_raw); /* native compact-target value */

	/* Amounts are chain base units (Bitcoin-style: 1e8 per coin). E.g. 1800000 -> 0.018,
	 * 4200000 -> 0.042 — we serialize those integers as-is in outputs. */
	j = json_object_get(result, "coinbasevalue");
	if (j && json_is_integer(j))
		cval = json_integer_value(j);

	const char *faddr = json_string_value(json_object_get(result, "FoundationReserveAddress"));
	j = json_object_get(result, "FoundationReserveValue");
	if (j && json_is_integer(j))
		fval = json_integer_value(j);

	j = json_object_get(result, "default_witness_commitment");
	if (j && json_is_string(j))
		wcommit = json_string_value(j);

	uint8_t miner_h160[20], fund_h160[20];
	if (!addr_to_pubkey_hash(opt_coinbase_addr, miner_h160)) {
		applog(LOG_ERR, "GBT solo: bad --coinbase-addr");
		return false;
	}
	if (faddr && strlen(faddr) && fval > 0) {
		if (!addr_to_pubkey_hash(faddr, fund_h160)) {
			applog(LOG_ERR, "GBT solo: bad FoundationReserveAddress from template");
			return false;
		}
	} else
		memset(fund_h160, 0, sizeof(fund_h160));

	/* Coinbase script: height + extranonce */
	std::vector<uint8_t> coinbase_script;
	script_height(coinbase_script, height);
	uint8_t xn[8];
	for (int i = 0; i < 8; i++)
		xn[i] = (uint8_t)(rand() & 0xff);
	append_push_data(coinbase_script, xn, 8);

	/* vin: single coinbase */
	std::vector<uint8_t> vin;
	push_varint(vin, 1);
	for (int i = 0; i < 32; i++)
		vin.push_back(0);
	append_u32_le(vin, 0xffffffffu);
	push_varint(vin, coinbase_script.size());
	vin.insert(vin.end(), coinbase_script.begin(), coinbase_script.end());
	append_u32_le(vin, 0xffffffffu);

	/* vouts: miner, optional foundation, optional witness commitment (last). */
	std::vector<uint8_t> vout;
	int nout = 1;
	if (faddr && fval > 0)
		nout++;
	if (wcommit && strlen(wcommit))
		nout++;
	push_varint(vout, (uint64_t)nout);

	/* Soteria template: coinbasevalue pays --coinbase-addr; FoundationReserveValue is a second
	 * output (not deducted from coinbasevalue). Total coinbase = miner + foundation + witness. */
	int64_t miner_val = cval;
	append_u64_le(vout, (uint64_t)miner_val);
	std::vector<uint8_t> spk1;
	p2pkh_script(spk1, miner_h160);
	push_varint(vout, spk1.size());
	vout.insert(vout.end(), spk1.begin(), spk1.end());

	if (faddr && fval > 0) {
		append_u64_le(vout, (uint64_t)fval);
		std::vector<uint8_t> spk2;
		p2pkh_script(spk2, fund_h160);
		push_varint(vout, spk2.size());
		vout.insert(vout.end(), spk2.begin(), spk2.end());
	}

	if (wcommit && strlen(wcommit)) {
		std::vector<uint8_t> wc;
		size_t wlen = strlen(wcommit) / 2;
		wc.resize(wlen);
		if (hex2bin(wc.data(), wcommit, (int)wlen)) {
			append_u64_le(vout, 0);
			push_varint(vout, wc.size());
			vout.insert(vout.end(), wc.begin(), wc.end());
		}
	}

	uint32_t locktime = 0;
	std::vector<uint8_t> tx_legacy;
	serialize_tx_legacy(vin, vout, 2, locktime, tx_legacy);

	uint8_t txhash[32];
	dsha256(tx_legacy.data(), tx_legacy.size(), txhash);

	/* Per-input witness: 1 stack item of 32 zero bytes (BIP141). */
	std::vector<uint8_t> witness;
	push_varint(witness, 1);
	push_varint(witness, 32);
	for (int i = 0; i < 32; i++)
		witness.push_back(0);

	std::vector<uint8_t> tx_wit;
	serialize_tx_witness(vin, vout, witness, 2, locktime, tx_wit);

	/* Collect txids: coinbase + template txs */
	std::vector<std::vector<uint8_t> > mh;
	std::vector<uint8_t> cbid(txhash, txhash + 32);
	mh.push_back(cbid);

	json_t *txs = json_object_get(result, "transactions");
	if (txs && json_is_array(txs)) {
		size_t idx;
		json_t *txo;
		json_array_foreach(txs, idx, txo) {
			const char *d = json_string_value(json_object_get(txo, "data"));
			if (!d)
				continue;
			size_t bl = strlen(d) / 2;
			std::vector<uint8_t> raw(bl);
			if (!hex2bin(raw.data(), d, bl))
				continue;
			uint8_t th[32];
			dsha256(raw.data(), raw.size(), th);
			std::vector<uint8_t> tv(th, th + 32);
			mh.push_back(tv);
		}
	}

	uint8_t mroot[32];
	merkle_compute(mh, mroot);

	work_gbt_free(work);

	/* All scalar header fields stored as swab32(native_value), matching the
	   stratum convention (le32dec of big-endian hex from pool).  The kernel
	   reconstructs wire-format bytes via be32enc(pdata[k]) and derives the
	   X12R timestamp via swab32(pdata[17]). */
	work->data[0] = swab32(ver);
	hex_prev_to_work_words(ph, work->data + 1);
	for (int i = 0; i < 8; i++)
		work->data[9 + i] = be32dec(mroot + i * 4);
	work->data[17] = swab32((uint32_t)((uint64_t)curtime & 0xffffffffu));
	work->data[18] = swab32(nbits);
	work->data[19] = 0;
	work->data[20] = 0x80000000;
	work->data[31] = 0x00000280;

	tstr = json_string_value(json_object_get(result, "target"));
	if (tstr && strlen(tstr) == 64) {
		uint8_t tb[32];
		if (hex2bin(tb, tstr, 32)) {
			/* Daemon returns target via GetHex() which is MSB-first;
			   reverse word order + swap bytes to get internal LE words. */
			for (int i = 0; i < 8; i++)
				work->target[7 - i] = be32dec(tb + i * 4);
		}
	} else {
		unsigned char tbytes[32];
		bn_nbits_to_uchar(nbits, tbytes);
		/* bn_nbits_to_uchar uses ToString()/GetHex() (MSB-first). */
		for (int i = 0; i < 8; i++)
			work->target[7 - i] = be32dec(tbytes + i * 4);
	}

	/* Compute block difficulty directly from nBits (native compact target).
	   target_to_diff() uses a pool-oriented constant that is 256x too small
	   for the standard Bitcoin block difficulty formula. */
	{
		uint32_t mant = nbits & 0x007fffffu;
		int exp = (int)((nbits >> 24) & 0xffu);
		if (mant) {
			double d = (double)0x0000ffff / (double)mant;
			for (int s = 0x1d - exp; s > 0; s--) d *= 256.0;
			for (int s = 0x1d - exp; s < 0; s++) d /= 256.0;
			work->targetdiff = d;
		} else {
			work->targetdiff = 0.0;
		}
	}
	work->height = (uint32_t)height;

	snprintf(work->job_id, sizeof(work->job_id), "%x%.16s", (unsigned)height, ph);

	const char *wid = json_string_value(json_object_get(result, "workid"));
	if (wid && strlen(wid))
		work->gbt_workid = strdup(wid);

	/* Block tail: varint(tx count) || coinbase raw || other txs raw */
	std::vector<uint8_t> tail;
	push_varint(tail, 1 + (txs && json_is_array(txs) ? json_array_size(txs) : 0));
	tail.insert(tail.end(), tx_wit.begin(), tx_wit.end());
	if (txs && json_is_array(txs)) {
		size_t idx;
		json_t *txo;
		json_array_foreach(txs, idx, txo) {
			const char *d = json_string_value(json_object_get(txo, "data"));
			if (!d)
				continue;
			size_t bl = strlen(d) / 2;
			std::vector<uint8_t> raw(bl);
			if (hex2bin(raw.data(), d, bl))
				tail.insert(tail.end(), raw.begin(), raw.end());
		}
	}

	char *hx = bin2hex(tail.data(), tail.size());
	if (!hx)
		return false;
	work->gbt_txs_hex = hx;

	/* Allow local ntime rolls (like pool jobs) before refetching template; Bitcoin allows +2h. */
	{
		int64_t cap = curtime + 7200;
		j = json_object_get(result, "maxtime");
		if (j && gbt_json_to_int64(j, &cap) && cap > curtime)
			;
		else
			cap = curtime + 7200;
		work->gbt_ntime_cap = (uint32_t)((uint64_t)cap & 0xffffffffu);
	}

	return true;
}
