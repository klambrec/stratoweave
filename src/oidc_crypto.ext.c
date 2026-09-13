#include <uv.h>
#include <mbedtls/sha256.h>
#include <mbedtls/rsa.h>
#include <mbedtls/bignum.h>
#include <mbedtls/platform_util.h>
#include <yyjson.h>

void stratoweaveQ_oidc_cryptoQ___ext_init__(void) {}

bool stratoweaveQ_oidc_cryptoQ_durable_runtime_enabled(void) {
#ifdef ACTON_DB
    return true;
#else
    return false;
#endif
}

static void sw_oidc_crypto_error(const char *message) {
    $RAISE((B_BaseException)B_ValueErrorG_new(to$str((char *)message)));
}

B_bytes stratoweaveQ_oidc_cryptoQ_random_bytes(void) {
    unsigned char data[32];
    if (uv_random(NULL, NULL, data, sizeof(data), 0, NULL) != 0)
        sw_oidc_crypto_error("Operating system random generator failed");
    B_bytes result = to$bytesD_len((char *)data, sizeof(data));
    mbedtls_platform_zeroize(data, sizeof(data));
    return result;
}

B_bytes stratoweaveQ_oidc_cryptoQ_sha256(B_bytes data) {
    unsigned char digest[32];
    if (mbedtls_sha256(data->str, data->nbytes, digest, 0) != 0)
        sw_oidc_crypto_error("SHA-256 failed");
    return to$bytesD_len((char *)digest, sizeof(digest));
}

bool stratoweaveQ_oidc_cryptoQ_constant_time_equal(B_str left, B_str right) {
    if (left->nbytes != right->nbytes) return false;
    volatile unsigned char difference = 0;
    for (size_t i = 0; i < left->nbytes; ++i)
        difference |= left->str[i] ^ right->str[i];
    return difference == 0 ? true : false;
}

bool stratoweaveQ_oidc_cryptoQ_verify_rs256(B_bytes message, B_bytes signature,
                                             B_bytes modulus, B_bytes exponent) {
    if (modulus->nbytes < 256 || modulus->nbytes > 1024 ||
        exponent->nbytes == 0 || exponent->nbytes > 8 ||
        signature->nbytes != modulus->nbytes || modulus->str[0] == 0 ||
        (modulus->nbytes == 256 && (modulus->str[0] & 0x80) == 0))
        return false;
    mbedtls_rsa_context rsa;
    mbedtls_rsa_init(&rsa);
    int result = mbedtls_rsa_import_raw(&rsa, modulus->str, modulus->nbytes,
                                       NULL, 0, NULL, 0, NULL, 0,
                                       exponent->str, exponent->nbytes);
    if (result == 0) result = mbedtls_rsa_complete(&rsa);
    if (result == 0) result = mbedtls_rsa_check_pubkey(&rsa);
    unsigned char hash[32];
    if (result == 0) result = mbedtls_sha256(message->str, message->nbytes, hash, 0);
    if (result == 0)
        result = mbedtls_rsa_rsassa_pkcs1_v15_verify(&rsa, MBEDTLS_MD_SHA256,
                                                   sizeof(hash), hash, signature->str);
    mbedtls_rsa_free(&rsa);
    return result == 0 ? true : false;
}

bool stratoweaveQ_oidc_cryptoQ_password_input_valid(B_str password) {
    return password->nbytes <= 72 && memchr(password->str, 0, password->nbytes) == NULL;
}

static bool sw_oidc_json_value_valid(yyjson_val *value, unsigned depth) {
    if (depth > 32) return false;
    if (yyjson_is_str(value))
        return memchr(yyjson_get_str(value), 0, yyjson_get_len(value)) == NULL;
    if (yyjson_is_arr(value)) {
        if (yyjson_arr_size(value) > 1024) return false;
        size_t index, count;
        yyjson_val *item;
        yyjson_arr_foreach(value, index, count, item)
            if (!sw_oidc_json_value_valid(item, depth + 1)) return false;
    }
    if (yyjson_is_obj(value)) {
        if (yyjson_obj_size(value) > 128) return false;
        size_t index, count;
        yyjson_val *key, *item;
        yyjson_obj_foreach(value, index, count, key, item) {
            if (!sw_oidc_json_value_valid(key, depth + 1) ||
                !sw_oidc_json_value_valid(item, depth + 1)) return false;
            size_t other_index, other_count;
            yyjson_val *other_key, *other_item;
            yyjson_obj_foreach(value, other_index, other_count, other_key, other_item) {
                if (other_index >= index) break;
                if (yyjson_get_len(key) == yyjson_get_len(other_key) &&
                    memcmp(yyjson_get_str(key), yyjson_get_str(other_key), yyjson_get_len(key)) == 0)
                    return false;
            }
        }
    }
    return true;
}

bool stratoweaveQ_oidc_cryptoQ_json_document_valid(B_bytes data) {
    if (data->nbytes > 262144) return false;
    yyjson_doc *doc = yyjson_read((const char *)data->str, data->nbytes, 0);
    if (doc == NULL) return false;
    bool valid = sw_oidc_json_value_valid(yyjson_doc_get_root(doc), 0);
    yyjson_doc_free(doc);
    return valid;
}
