# pr_contract — the PrintedReef wire/data contract (schema + ESP-NOW frame).
#
# The headers still physically live in ../../include so the existing
# `src/shared/include/schema.h` include path keeps working for repos that
# haven't migrated yet (notably the P4 build today). This component exposes
# the same headers under the IDF component system so new repos can simply
# `REQUIRES pr_contract`. When every consumer is migrated, the headers move
# into include/ here and the top-level ../../include copy is dropped.
idf_component_register(
    INCLUDE_DIRS "../../include"
)
