# as_core — device-agnostic AquaSensei connection core (MQTT/TLS → AWS IoT Core).
#
# Holds the wire stack shared by every AquaSensei display device: MQTT/TLS
# transport + identity, telemetry.v1 uplink, account.v1 downlink, the claim URL.
# Device-specific bits (which sources are live, Apex outlets/feed, device_type)
# are injected by the shell via pr_cloud_set_telemetry_provider/_device_type — so
# this component depends on NO device-specific component (note: no `apex`).
#
# The 3 AWS IoT PEMs are embedded at build time (NULL-terminated via
# EMBED_TXTFILES → symbols _binary_aws_*_pem_start). They are gitignored;
# copy your own from the AWS thing's downloaded credentials into this folder:
#   aws_device_cert.pem   (xxxx-certificate.pem.crt)
#   aws_private_key.pem    (xxxx-private.pem.key)
#   aws_root_ca.pem        (AmazonRootCA1.pem)
idf_component_register(
    SRCS "pr_cloud.cpp"
    INCLUDE_DIRS "."
    REQUIRES mqtt esp-tls json nvs_flash esp_timer esp_netif esp_app_format
    EMBED_TXTFILES
        "aws_device_cert.pem"
        "aws_private_key.pem"
        "aws_root_ca.pem"
)
