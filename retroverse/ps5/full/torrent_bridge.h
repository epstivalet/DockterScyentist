/* Local PS5Torrent bridge, GPL-3.0-or-later.
 * A deliberately restricted HTTP client: only 127.0.0.1:12389 is reachable.
 * Requires the separately installed PS5Torrent service.
 */
#ifndef RETROVERSE_TORRENT_BRIDGE_H
#define RETROVERSE_TORRENT_BRIDGE_H
#include <stddef.h>
int rv_torrent_request(const char *method,const char *route,const char *magnet,
                       int id,char **json_out,size_t *json_len,int *status,
                       char *err,size_t errlen);
#endif
