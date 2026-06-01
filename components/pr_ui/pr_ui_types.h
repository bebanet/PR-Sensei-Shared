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

#ifdef __cplusplus
}
#endif
