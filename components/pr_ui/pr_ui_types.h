/* pr_ui_types.h — shared UI enums.
 *
 * Canonical home for the status / trend / liveness enums every PrintedReef
 * widget and screen uses. Device repos that previously defined these in their
 * own ui_tile.h now include this instead, so the whole fleet shares one set.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PR_LIVE, PR_STALE, PR_LOST }      pr_liveness_t;
typedef enum { PR_OK, PR_WARN, PR_ALERT }        pr_status_t;   /* status dot  */
typedef enum { PR_TREND_NONE, PR_UP, PR_DOWN }   pr_trend_t;    /* steady = none */

/* Reading provenance — drives a small per-card badge. The wire layer carries a
   raw source slug (apex|hydros|manual|hanna|strip|refract|icp|computed); the
   integration layer maps it to one of these so the UI stays slug-agnostic. */
typedef enum {
    PR_SRC_NONE = 0,   /* absent / unknown slug → render plain  */
    PR_SRC_PROBE,      /* apex | hydros        → probe glyph    */
    PR_SRC_MANUAL,     /* manual               → hand glyph     */
    PR_SRC_TESTKIT,    /* hanna|strip|refract  → "KIT" tag      */
    PR_SRC_ICP,        /* icp                  → "ICP" badge    */
    PR_SRC_COMPUTED,   /* computed             → "calc" tag     */
} pr_src_t;

/* Reading freshness — drives dim/strike on the value. Computed from a reading
   timestamp at the integration edge (the UI never does epoch math). */
typedef enum {
    PR_FRESH_UNKNOWN = 0, /* absent / non-number / future-skew → plain */
    PR_FRESH_FRESH,       /* recent                                    */
    PR_FRESH_DIM,         /* aging → subtle dim                        */
    PR_FRESH_STALE,       /* old   → grey + strikethrough              */
} pr_fresh_t;

#ifdef __cplusplus
}
#endif
