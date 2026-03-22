#ifndef CLAP_NR_VERSION_H
#define CLAP_NR_VERSION_H

/* -----------------------------------------------------------------------
 * Version number -- single source of truth for the entire project.
 *
 * To release a new build:
 *   1. Increment the appropriate number below.
 *   2. Rebuild (build.bat).
 *   3. The new version string is automatically reflected in the CLAP
 *      descriptor, the About dialog, and any other place that includes
 *      this header.
 * --------------------------------------------------------------------- */

#define CLAP_NR_VERSION_MAJOR  1
#define CLAP_NR_VERSION_MINOR  0
#define CLAP_NR_VERSION_PATCH  0

/* Stringification helpers */
#define CLAP_NR_STR_(x)  #x
#define CLAP_NR_STR(x)   CLAP_NR_STR_(x)

#define CLAP_NR_VERSION_STR \
    CLAP_NR_STR(CLAP_NR_VERSION_MAJOR) "." \
    CLAP_NR_STR(CLAP_NR_VERSION_MINOR) "." \
    CLAP_NR_STR(CLAP_NR_VERSION_PATCH)

#endif /* CLAP_NR_VERSION_H */
