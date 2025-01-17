/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "s2n_test.h"
#include "testlib/s2n_testlib.h"
#include "tls/extensions/s2n_cert_status.h"

const uint8_t ocsp_data[] = "OCSP DATA";
struct s2n_cert_chain_and_key *chain_and_key;

int s2n_test_enable_sending_extension(struct s2n_connection *conn)
{
    conn->mode = S2N_SERVER;
    conn->status_type = S2N_STATUS_REQUEST_OCSP;
    conn->handshake_params.our_chain_and_key = chain_and_key;
    EXPECT_SUCCESS(s2n_cert_chain_and_key_set_ocsp_data(chain_and_key, ocsp_data, s2n_array_len(ocsp_data)));
    conn->x509_validator.state = VALIDATED;
    return S2N_SUCCESS;
}

int main(int argc, char **argv)
{
    BEGIN_TEST();

    EXPECT_SUCCESS(s2n_test_cert_chain_and_key_new(&chain_and_key,
            S2N_DEFAULT_TEST_CERT_CHAIN, S2N_DEFAULT_TEST_PRIVATE_KEY));

    /* should_send */
    {
        struct s2n_config *config;
        EXPECT_NOT_NULL(config = s2n_config_new());

        struct s2n_connection *conn;
        EXPECT_NOT_NULL(conn = s2n_connection_new(S2N_CLIENT));
        EXPECT_SUCCESS(s2n_connection_set_config(conn, config));

        /* Don't send by default */
        EXPECT_FALSE(s2n_cert_status_extension.should_send(conn));

        /* Send if all prerequisites met */
        EXPECT_SUCCESS(s2n_test_enable_sending_extension(conn));
        EXPECT_TRUE(s2n_cert_status_extension.should_send(conn));

        /* Don't send if client */
        EXPECT_SUCCESS(s2n_test_enable_sending_extension(conn));
        conn->mode = S2N_CLIENT;
        EXPECT_FALSE(s2n_cert_status_extension.should_send(conn));

        /* Don't send if no status request configured */
        EXPECT_SUCCESS(s2n_test_enable_sending_extension(conn));
        conn->status_type = S2N_STATUS_REQUEST_NONE;
        EXPECT_FALSE(s2n_cert_status_extension.should_send(conn));

        /* Don't send if no certificate set */
        EXPECT_SUCCESS(s2n_test_enable_sending_extension(conn));
        conn->handshake_params.our_chain_and_key = NULL;
        EXPECT_FALSE(s2n_cert_status_extension.should_send(conn));

        /* Don't send if no ocsp data */
        EXPECT_SUCCESS(s2n_test_enable_sending_extension(conn));
        EXPECT_SUCCESS(s2n_free(&conn->handshake_params.our_chain_and_key->ocsp_status));
        EXPECT_FALSE(s2n_cert_status_extension.should_send(conn));

        EXPECT_SUCCESS(s2n_connection_free(conn));
        EXPECT_SUCCESS(s2n_config_free(config));
    };

    /* Test send */
    {
        struct s2n_connection *conn;
        EXPECT_NOT_NULL(conn = s2n_connection_new(S2N_SERVER));
        EXPECT_SUCCESS(s2n_test_enable_sending_extension(conn));

        struct s2n_stuffer stuffer = { 0 };
        EXPECT_SUCCESS(s2n_stuffer_growable_alloc(&stuffer, 0));

        EXPECT_SUCCESS(s2n_cert_status_extension.send(conn, &stuffer));

        uint8_t request_type;
        EXPECT_SUCCESS(s2n_stuffer_read_uint8(&stuffer, &request_type));
        EXPECT_EQUAL(request_type, S2N_STATUS_REQUEST_OCSP);

        uint32_t ocsp_size;
        EXPECT_SUCCESS(s2n_stuffer_read_uint24(&stuffer, &ocsp_size));
        EXPECT_EQUAL(ocsp_size, s2n_stuffer_data_available(&stuffer));
        EXPECT_EQUAL(ocsp_size, s2n_array_len(ocsp_data));

        uint8_t *actual_ocsp_data;
        EXPECT_NOT_NULL(actual_ocsp_data = s2n_stuffer_raw_read(&stuffer, ocsp_size));
        EXPECT_BYTEARRAY_EQUAL(actual_ocsp_data, ocsp_data, ocsp_size);

        EXPECT_EQUAL(s2n_stuffer_data_available(&stuffer), 0);

        EXPECT_SUCCESS(s2n_stuffer_free(&stuffer));
        EXPECT_SUCCESS(s2n_connection_free(conn));
    };

    /* Test recv */
    {
        struct s2n_connection *conn;
        EXPECT_NOT_NULL(conn = s2n_connection_new(S2N_SERVER));
        EXPECT_SUCCESS(s2n_test_enable_sending_extension(conn));

        struct s2n_stuffer stuffer = { 0 };
        EXPECT_SUCCESS(s2n_stuffer_growable_alloc(&stuffer, 0));

        EXPECT_SUCCESS(s2n_cert_status_extension.send(conn, &stuffer));

        EXPECT_EQUAL(conn->status_response.size, 0);
        EXPECT_SUCCESS(s2n_cert_status_extension.recv(conn, &stuffer));
        EXPECT_BYTEARRAY_EQUAL(conn->status_response.data, ocsp_data, s2n_array_len(ocsp_data));

        EXPECT_EQUAL(s2n_stuffer_data_available(&stuffer), 0);

        EXPECT_SUCCESS(s2n_stuffer_free(&stuffer));
        EXPECT_SUCCESS(s2n_connection_free(conn));
    };

    /* Test recv - not ocsp */
    {
        struct s2n_connection *conn;
        EXPECT_NOT_NULL(conn = s2n_connection_new(S2N_SERVER));
        EXPECT_SUCCESS(s2n_test_enable_sending_extension(conn));

        struct s2n_stuffer stuffer = { 0 };
        EXPECT_SUCCESS(s2n_stuffer_growable_alloc(&stuffer, 0));

        EXPECT_SUCCESS(s2n_stuffer_write_uint8(&stuffer, S2N_STATUS_REQUEST_NONE));

        EXPECT_EQUAL(conn->status_response.size, 0);
        EXPECT_SUCCESS(s2n_cert_status_extension.recv(conn, &stuffer));
        EXPECT_EQUAL(conn->status_response.size, 0);

        EXPECT_SUCCESS(s2n_stuffer_free(&stuffer));
        EXPECT_SUCCESS(s2n_connection_free(conn));
    };

    /* Test recv - bad ocsp data */
    {
        struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);
        EXPECT_NOT_NULL(conn);
        EXPECT_SUCCESS(s2n_test_enable_sending_extension(conn));

        DEFER_CLEANUP(struct s2n_stuffer stuffer = { 0 }, s2n_stuffer_free);
        EXPECT_SUCCESS(s2n_stuffer_growable_alloc(&stuffer, 0));

        EXPECT_SUCCESS(s2n_cert_status_extension.send(conn, &stuffer));

        if (s2n_x509_ocsp_stapling_supported()) {
            EXPECT_FAILURE_WITH_ERRNO(s2n_cert_status_extension.recv(conn, &stuffer),
                    S2N_ERR_INVALID_OCSP_RESPONSE);
        } else {
            /* s2n_x509_validator_validate_cert_stapled_ocsp_response returns untrusted error if ocsp is not supported */
            EXPECT_FAILURE_WITH_ERRNO(s2n_cert_status_extension.recv(conn, &stuffer),
                    S2N_ERR_CERT_UNTRUSTED);
        }

        EXPECT_SUCCESS(s2n_connection_free(conn));
    };

    END_TEST();
    return 0;
}
