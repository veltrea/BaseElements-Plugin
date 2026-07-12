/*
 * evpcompat.c - forward OpenSSL 3.x "_get_" accessor names to the
 * historical exports present in this static libcrypto build.
 *
 * Do NOT include OpenSSL headers here: they #define the historical
 * names to the _get_ names, which would rename these definitions.
 * Prototypes are written by hand against opaque struct types.
 *
 * EVP_CIPHER_mode never existed as an exported function (it has always
 * been a macro over EVP_CIPHER_flags), so EVP_CIPHER_get_mode is
 * implemented via EVP_CIPHER_flags & EVP_CIPH_MODE (0xF0007).
 */

typedef struct evp_pkey_st EVP_PKEY;
typedef struct evp_cipher_st EVP_CIPHER;
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;

extern int EVP_PKEY_size(const EVP_PKEY *pkey);
extern int EVP_PKEY_id(const EVP_PKEY *pkey);
extern int EVP_CIPHER_block_size(const EVP_CIPHER *cipher);
extern int EVP_CIPHER_key_length(const EVP_CIPHER *cipher);
extern int EVP_CIPHER_iv_length(const EVP_CIPHER *cipher);
extern unsigned long EVP_CIPHER_flags(const EVP_CIPHER *cipher);
extern int EVP_CIPHER_CTX_block_size(const EVP_CIPHER_CTX *ctx);

#define EVPCOMPAT_CIPH_MODE 0xF0007UL

int EVP_PKEY_get_size(const EVP_PKEY *pkey)
{
    return EVP_PKEY_size(pkey);
}

int EVP_PKEY_get_id(const EVP_PKEY *pkey)
{
    return EVP_PKEY_id(pkey);
}

int EVP_CIPHER_get_block_size(const EVP_CIPHER *cipher)
{
    return EVP_CIPHER_block_size(cipher);
}

int EVP_CIPHER_get_key_length(const EVP_CIPHER *cipher)
{
    return EVP_CIPHER_key_length(cipher);
}

int EVP_CIPHER_get_iv_length(const EVP_CIPHER *cipher)
{
    return EVP_CIPHER_iv_length(cipher);
}

int EVP_CIPHER_get_mode(const EVP_CIPHER *cipher)
{
    return (int)(EVP_CIPHER_flags(cipher) & EVPCOMPAT_CIPH_MODE);
}

int EVP_CIPHER_CTX_get_block_size(const EVP_CIPHER_CTX *ctx)
{
    return EVP_CIPHER_CTX_block_size(ctx);
}
