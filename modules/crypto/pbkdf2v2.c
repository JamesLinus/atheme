/*
 * Copyright (C) 2015 Aaron Jones <aaronmdjones@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "atheme.h"

#ifdef HAVE_OPENSSL

#include <openssl/evp.h>

/*
 * Do not change anything below this line unless you know what you are doing,
 * AND how it will (possibly) break backward-, forward-, or cross-compatibility
 *
 * In particular, the salt length SHOULD NEVER BE CHANGED. 128 bits is more than
 * sufficient.
 */

#define PBKDF2_FN_PREFIX            "$z$%u$%u$"
#define PBKDF2_FN_BASE62            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"

#define PBKDF2_FN_LOADSALT          PBKDF2_FN_PREFIX "%16[" PBKDF2_FN_BASE62 "]$"
#define PBKDF2_FN_SAVESALT          PBKDF2_FN_PREFIX "%s$"
#define PBKDF2_FN_SAVEHASH          PBKDF2_FN_SAVESALT "%s"

#define PBKDF2_PRF_HMAC_SHA1        4U
#define PBKDF2_PRF_HMAC_SHA2_256    5U
#define PBKDF2_PRF_HMAC_SHA2_512    6U

#define PBKDF2_SALTLEN  16

#define PBKDF2_C_MIN    10000
#define PBKDF2_C_MAX    5000000
#define PBKDF2_C_DEF    64000

static const char salt_chars[62] = PBKDF2_FN_BASE62;

static unsigned int pbkdf2v2_digest = 6; /* SHA512 */
static unsigned int pbkdf2v2_rounds = PBKDF2_C_DEF;

static const char *
atheme_pbkdf2v2_salt(void)
{
	/* Fill salt array with random bytes */
	unsigned char rawsalt[PBKDF2_SALTLEN];
	(void) arc4random_buf(rawsalt, sizeof rawsalt);

	/* Use random byte as index into printable character array, turning it into a printable string */
	char salt[sizeof rawsalt + 1];
	for (size_t i = 0; i < sizeof rawsalt; i++)
		salt[i] = salt_chars[rawsalt[i] % sizeof salt_chars];

	/* NULL-terminate the string */
	salt[sizeof rawsalt] = 0x00;

	/* Format and return the result */
	static char res[PASSLEN];
	if (snprintf(res, PASSLEN, PBKDF2_FN_SAVESALT, pbkdf2v2_digest, pbkdf2v2_rounds, salt) >= PASSLEN)
		return NULL;

	return res;
}

static const char *
atheme_pbkdf2v2_crypt(const char *const restrict password, const char *const restrict parameters)
{
	/*
	 * Attempt to extract the PRF, iteration count and salt
	 *
	 * If this fails, we're trying to verify a hash not produced by
	 * this module - just bail out, libathemecore can handle NULL
	 */
	unsigned int prf;
	unsigned int iter;
	char salt[PBKDF2_SALTLEN + 1];
	if (sscanf(parameters, PBKDF2_FN_LOADSALT, &prf, &iter, salt) != 3)
		return NULL;

	/*
	 * Look up the digest method corresponding to the PRF
	 *
	 * If this fails, we are trying to verify a hash that we don't
	 * know how to compute, just bail out like above.
	 */
	const EVP_MD *md = NULL;

	if (prf == PBKDF2_PRF_HMAC_SHA1)
		md = EVP_sha1();
	else if (prf == PBKDF2_PRF_HMAC_SHA2_256)
		md = EVP_sha256();
	else if (prf == PBKDF2_PRF_HMAC_SHA2_512)
		md = EVP_sha512();

	if (!md)
		return NULL;

	/* Compute the PBKDF2 digest */
	const int pl = (int) strlen(password);
	const int sl = (int) strlen(salt);
	unsigned char digest[EVP_MAX_MD_SIZE];
	const int ret = PKCS5_PBKDF2_HMAC(password, pl, (unsigned char *) salt, sl, (int) iter, md,
	                                  EVP_MD_size(md), digest);
	if (!ret)
		return NULL;

	/* Convert the digest to Base 64 */
	char digest_b64[(EVP_MAX_MD_SIZE * 2) + 5];
	(void) base64_encode((const char *) digest, (size_t) EVP_MD_size(md), digest_b64, sizeof digest_b64);

	/* Format the result */
	static char res[PASSLEN];
	if (snprintf(res, PASSLEN, PBKDF2_FN_SAVEHASH, prf, iter, salt, digest_b64) >= PASSLEN)
		return NULL;

	return res;
}

static bool
atheme_pbkdf2v2_recrypt(const char *const restrict parameters)
{
	unsigned int prf;
	unsigned int iter;
	char salt[PBKDF2_SALTLEN + 1];

	if (sscanf(parameters, PBKDF2_FN_LOADSALT, &prf, &iter, salt) != 3)
		return false;

	if (prf != pbkdf2v2_digest)
		return true;

	if (iter != pbkdf2v2_rounds)
		return true;

	return false;
}

static int
c_ci_pbkdf2v2_digest(mowgli_config_file_entry_t *const restrict ce)
{
	if (ce->vardata == NULL)
	{
		conf_report_warning(ce, "no parameter for configuration option");
		return 0;
	}

	if (!strcasecmp(ce->vardata, "SHA1"))
		pbkdf2v2_digest = PBKDF2_PRF_HMAC_SHA1;
	else if (!strcasecmp(ce->vardata, "SHA256"))
		pbkdf2v2_digest = PBKDF2_PRF_HMAC_SHA2_256;
	else if (!strcasecmp(ce->vardata, "SHA512"))
		pbkdf2v2_digest = PBKDF2_PRF_HMAC_SHA2_512;
	else
		conf_report_warning(ce, "invalid parameter for configuration option");

	return 0;
}

static crypt_impl_t crypto_pbkdf2v2_impl = {

	.id         = "pbkdf2v2",
	.salt       = &atheme_pbkdf2v2_salt,
	.crypt      = &atheme_pbkdf2v2_crypt,
	.recrypt    = &atheme_pbkdf2v2_recrypt,
};

static mowgli_list_t pbkdf2v2_conf_table;

static void
crypto_pbkdf2v2_modinit(module_t __attribute__((unused)) *const restrict m)
{
	(void) crypt_register(&crypto_pbkdf2v2_impl);

	(void) add_subblock_top_conf("PBKDF2V2", &pbkdf2v2_conf_table);
	(void) add_conf_item("DIGEST", &pbkdf2v2_conf_table, c_ci_pbkdf2v2_digest);
	(void) add_uint_conf_item("ROUNDS", &pbkdf2v2_conf_table, 0, &pbkdf2v2_rounds,
	                          PBKDF2_C_MIN, PBKDF2_C_MAX, PBKDF2_C_DEF);
}

static void
crypto_pbkdf2v2_moddeinit(const module_unload_intent_t __attribute__((unused)) intent)
{
	(void) del_conf_item("DIGEST", &pbkdf2v2_conf_table);
	(void) del_conf_item("ROUNDS", &pbkdf2v2_conf_table);
	(void) del_top_conf("PBKDF2V2");

	(void) crypt_unregister(&crypto_pbkdf2v2_impl);
}

DECLARE_MODULE_V1("crypto/pbkdf2v2", false, crypto_pbkdf2v2_modinit, crypto_pbkdf2v2_moddeinit,
                  PACKAGE_VERSION, "Aaron Jones <aaronmdjones@gmail.com>");

#endif /* HAVE_OPENSSL */
