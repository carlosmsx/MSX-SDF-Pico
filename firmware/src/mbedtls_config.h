// mbedTLS para https.c: cliente TLS 1.2, lo justo para GitHub
// (actualizar.c). Lo incluye pico_mbedtls_config.h del SDK.
//
// api.github.com y github.com presentan ECDSA P-256 con raiz P-384 (Sectigo,
// USERTrust); release-assets.githubusercontent.com, RSA 2048 con raiz RSA
// 4096 (Let's Encrypt). Los tres aceptan ECDHE con X25519 y AES-128-GCM.
//
// Sin MBEDTLS_HAVE_TIME: no se revisan las fechas de los certificados (el
// reloj puede no estar puesto). La cadena, el nombre y la firma, si.

#pragma once

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY             // https.c: el bufer grande va prestado
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT        // pico_mbedtls.c, con pico_rand
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES              // las tablas en la flash, no en la RAM
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C

#define MBEDTLS_BIGNUM_C
#define MBEDTLS_MPI_MAX_SIZE                512     // RSA de 4096
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_WINDOW_SIZE             2       // menos RAM
#define MBEDTLS_ECP_FIXED_POINT_OPTIM       0
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_IN_CONTENT_LEN          16384   // el servidor manda registros enteros
#define MBEDTLS_SSL_OUT_CONTENT_LEN         2048    // solo se manda el pedido
